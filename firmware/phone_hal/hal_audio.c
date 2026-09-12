/*
 * hal_audio.c — I2S e configurazione del codec WM8960.
 *
 * STATO: solo RIPRODUZIONE. Il percorso del microfono arrivera' dopo, quando
 * questo sara' verificato. E' voluto: sono meno di venti registri invece di
 * quaranta, e soprattutto l'esito e' netto — o si sente il tono o non si sente.
 * Con entrambi i versi insieme, un silenzio non direbbe da che parte guardare.
 *
 * IL CODEC NON SI PUO' RILEGGERE (vedi hal_codec.c): i registri sono di sola
 * scrittura e non esiste modo di verificare che una configurazione sia stata
 * presa. Ogni valore qui sotto e' quindi da considerare **da validare
 * all'orecchio**, e i commenti dicono a cosa serve ciascuno proprio per poterli
 * correggere uno alla volta invece che a tentativi.
 *
 * L'MCLK esce da GPIO 0 perche' su ESP32 non c'e' alternativa: solo 0, 1 e 3
 * possono portarlo, e 1 e 3 sono la console seriale. GPIO 0 e' un pin di
 * strapping ma viene campionato solo all'istante del reset — vedi
 * hardware/pinout.md per il rischio e perche' e' accettabile.
 */

#include <string.h>

#include "hal_priv.h"

#include "driver/i2s_std.h"
#include "esp_log.h"
#include "freertos/task.h"

static const char *TAG = "hal_audio";

#define SAMPLE_RATE   16000     /* come i toni e come mSBC a banda larga */

/* --- Registri del WM8960 usati qui ---------------------------------------- */
#define R_LOUT1_VOL     0x02
#define R_ROUT1_VOL     0x03
#define R_CLOCKING1     0x04
#define R_ADCDAC_CTL1   0x05
#define R_IFACE1        0x07
#define R_LDAC_VOL      0x0A
#define R_RDAC_VOL      0x0B
#define R_PWR_MGMT1     0x19
#define R_PWR_MGMT2     0x1A
#define R_LEFT_OUTMIX   0x22
#define R_RIGHT_OUTMIX  0x25
#define R_PWR_MGMT3     0x2F

static i2s_chan_handle_t s_tx;
static bool              s_pronto;

static bool configura_codec(void)
{
    struct { uint8_t reg; uint16_t val; const char *cosa; } seq[] = {
        /* Riferimento di tensione e VMID a 50 kOhm: senza, il codec resta
           spento qualunque altra cosa gli si scriva. */
        { R_PWR_MGMT1,    0x0C0, "VREF + VMID" },

        /* Accende i due DAC e le due uscite cuffia. La capsula della cornetta
           andra' sull'uscita CUFFIA e non su quella speaker, che e' a ponte e
           non ha una massa comune (vedi hardware/pinout.md). */
        /* DAC, uscite cuffia, e OUT3.
         *
         * OUT3 non e' una terza uscita: e' il buffer che fa da MASSA VIRTUALE
         * alla cuffia. Senza, il ritorno del segnale e' debole e il volume
         * risulta bassissimo pur essendo tutto il resto configurato bene —
         * sintomo osservato il 13/09/2026. */
        { R_PWR_MGMT2,    0x1E2, "DAC L/R + cuffia + massa virtuale OUT3" },

        /* Mixer d'uscita alimentati: sono lo stadio fra DAC e cuffia. */
        { R_PWR_MGMT3,    0x00C, "mixer d'uscita" },

        /* SYSCLK preso direttamente da MCLK, nessun PLL, nessuna divisione.
           Con MCLK = 256 x 16 kHz = 4,096 MHz i conti tornano esatti e non
           serve la catena PLL, che e' la parte piu' facile da sbagliare. */
        { R_CLOCKING1,    0x000, "clock da MCLK, niente PLL" },

        /* Formato I2S, 16 bit, codec in slave: i clock li genera l'ESP32. */
        { R_IFACE1,       0x002, "I2S 16 bit, codec slave" },

        /* Dopo il reset il DAC e' MUTO. Questa riga lo smuta, ed e' la prima
           da sospettare se tutto sembra a posto e non si sente niente. */
        { R_ADCDAC_CTL1,  0x000, "DAC non piu' muto" },

        /* Volume digitale dei DAC a 0 dB. Il bit 8 e' l'aggiornamento: senza,
           il valore viene scritto ma non applicato. */
        { R_LDAC_VOL,     0x1FF, "volume DAC sinistro" },
        { R_RDAC_VOL,     0x1FF, "volume DAC destro" },

        /* Instrada il DAC verso il mixer d'uscita. Saltare questa lascia un
           codec perfettamente configurato che non collega niente a niente. */
        { R_LEFT_OUTMIX,  0x100, "DAC sinistro -> mixer" },
        { R_RIGHT_OUTMIX, 0x100, "DAC destro -> mixer" },

        /* Volume analogico della cuffia, 0 dB. Bit 8 = applica. */
        /* Volume cuffia a -12 dB. Con +6 dB il livello e' risultato
           altissimo in cuffia (misurato all'orecchio il 13/09/2026): la
           capsula della cornetta e' piu' sensibile di quanto previsto. Questo
           e' il numero da ritoccare quando l'audio sara' in cornetta. */
        { R_LOUT1_VOL,    0x16D, "volume cuffia sinistra" },
        { R_ROUT1_VOL,    0x16D, "volume cuffia destra" },
    };

    for (size_t i = 0; i < sizeof(seq) / sizeof(seq[0]); i++) {
        const esp_err_t err = hal_codec_write(seq[i].reg, seq[i].val);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "registro 0x%02X (%s) rifiutato: %s",
                     seq[i].reg, seq[i].cosa, esp_err_to_name(err));
            return false;
        }
    }
    return true;
}

void hal_audio_init(void)
{
    if (!hal_codec_presente()) {
        ESP_LOGW(TAG, "codec assente: nessun audio");
        return;
    }

    /* I registri PRIMA dell'I2S.
     *
     * Il primo tentativo faceva l'inverso e il codec smetteva di rispondere sul
     * bus di controllo appena l'I2S partiva: probe e reset a 311 ms andati a
     * buon fine, e la scrittura successiva a 371 ms rifiutata con
     * ESP_ERR_INVALID_STATE, cioe' transazione mai completata.
     *
     * Configurare prima e' comunque l'ordine corretto: i registri del WM8960 si
     * scrivono a prescindere dal clock — l'MCLK serve a far girare i
     * convertitori, non al bus di controllo. */
    if (!configura_codec()) {
        return;
    }

    i2s_chan_config_t ch = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&ch, &s_tx, NULL));

    i2s_std_config_t std = {
        .clk_cfg = {
            .sample_rate_hz = SAMPLE_RATE,
            /* APLL, non il clock di sistema.
             *
             * Il codec e' configurato per assumere MCLK = 256 x fs ESATTI
             * (CLKSEL=0, DACDIV=000). Ricavando l'MCLK dal PLL di sistema a
             * 160 MHz servirebbe dividere per 39,0625: il divisore frazionario
             * ci va vicino ma non esatto, e il convertitore ricostruisce male —
             * l'altezza del suono resta giusta ma il timbro e' sporco, che e'
             * esattamente cio' che si sentiva il 13/09/2026 con la spazzata di
             * prova. L'APLL esiste per generare frequenze audio precise. */
            .clk_src        = I2S_CLK_SRC_APLL,
            .mclk_multiple  = I2S_MCLK_MULTIPLE_256,
        },
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                        I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = PIN_I2S_MCLK,
            .bclk = PIN_I2S_BCLK,
            .ws   = PIN_I2S_WS,
            .dout = PIN_I2S_DOUT,
            .din  = I2S_GPIO_UNUSED,
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_tx, &std));
    ESP_ERROR_CHECK(i2s_channel_enable(s_tx));

    /* Un momento perche' i clock si assestino prima di mandare campioni. */
    vTaskDelay(pdMS_TO_TICKS(50));

    s_pronto = true;
    ESP_LOGI(TAG, "I2S a %d Hz, MCLK su GPIO%d, codec configurato in uscita",
             SAMPLE_RATE, PIN_I2S_MCLK);
}

/* Manda `n` campioni mono. Il codec vuole stereo, quindi ogni campione viene
   duplicato sui due canali: la cornetta ne usera' uno solo. */
bool hal_audio_play(const int16_t *mono, size_t n)
{
    if (!s_pronto || !mono || n == 0) {
        return false;
    }

    int16_t stereo[128 * 2];
    while (n > 0) {
        const size_t blocco = n > 128 ? 128 : n;
        for (size_t i = 0; i < blocco; i++) {
            stereo[i * 2]     = mono[i];
            stereo[i * 2 + 1] = mono[i];
        }
        size_t scritti = 0;
        if (i2s_channel_write(s_tx, stereo, blocco * 2 * sizeof(int16_t),
                              &scritti, 200) != ESP_OK) {
            return false;
        }
        mono += blocco;
        n    -= blocco;
    }
    return true;
}
