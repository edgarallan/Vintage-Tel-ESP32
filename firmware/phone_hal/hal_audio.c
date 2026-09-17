/*
 * hal_audio.c — I2S e configurazione del codec WM8960.
 *
 * Riproduzione e registrazione, in full duplex su un solo peripheral I2S.
 *
 * La riproduzione e' stata costruita e verificata per prima, da sola: sono meno
 * registri, e l'esito e' netto — o si sente il tono o non si sente. Con
 * entrambi i versi insieme fin dall'inizio, un silenzio non avrebbe detto da
 * che parte guardare.
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
#include "freertos/ringbuf.h"
#include "freertos/task.h"

#include "tones.h"

static const char *TAG = "hal_audio";

#define SAMPLE_RATE   16000     /* come i toni e come mSBC a banda larga */

/* Campioni per giro del task audio: 8 ms a 16 kHz. Corto abbastanza da non
   aggiungere latenza percepibile a una conversazione, lungo abbastanza da non
   sprecare la CPU in commutazioni. */
#define FRAME         128

/* Circa 100 ms di capienza per verso. Serve ad assorbire il fatto che il
   Bluetooth consegna a pacchetti e l'I2S consuma a flusso costante. */
#define RB_BYTES      3200

/* Quanta scorta accumulare prima di cominciare a suonare, e a cui tornare dopo
   ogni svuotamento: 30 ms.
   Senza, il task attacca a consumare appena arriva il primo pacchetto e resta
   perennemente sul filo: al primo ritardo la coda si svuota, entra silenzio, e
   si sente un gracchio. Con la scorta il ritardo viene assorbito.
   Trenta millisecondi si pagano in latenza ed e' un prezzo onesto: in una
   conversazione non si percepiscono, mentre i buchi si sentono tutti. */
#define RX_SCORTA     960

/* --- Registri del WM8960 usati qui ---------------------------------------- */
#define R_LIN_VOL       0x00
#define R_RIN_VOL       0x01
#define R_LOUT1_VOL     0x02
#define R_ROUT1_VOL     0x03
#define R_CLOCKING1     0x04
#define R_ADCDAC_CTL1   0x05
#define R_IFACE1        0x07
#define R_LDAC_VOL      0x0A
#define R_RDAC_VOL      0x0B
#define R_PWR_MGMT1     0x19
#define R_PWR_MGMT2     0x1A
#define R_LADC_VOL      0x15
#define R_RADC_VOL      0x16
#define R_ADCL_PATH     0x20
#define R_ADCR_PATH     0x21
#define R_LEFT_OUTMIX   0x22
#define R_RIGHT_OUTMIX  0x25
#define R_PWR_MGMT3     0x2F

static i2s_chan_handle_t s_tx;
static i2s_chan_handle_t s_rx;
static bool              s_pronto;

/*
 * Le due code fra Bluetooth e cornetta.
 *
 * Non si puo' scrivere sull'I2S direttamente dai callback dello stack: girano
 * nel task di Bluedroid, che non deve mai bloccarsi. Le code disaccoppiano i
 * due mondi, e il task audio qui sotto fa da pompa.
 */
static RingbufHandle_t s_rb_rx;   /* dal cellulare -> capsula d'ascolto */
static RingbufHandle_t s_rb_tx;   /* microfono -> al cellulare */
static volatile bool   s_in_chiamata;

/* Falso finche' la coda in ricezione non ha accumulato la scorta. */
static bool s_rx_avviato;

/* Generatore dei toni di sistema. Suona solo FUORI dalla chiamata: durante la
   conversazione l'uscita appartiene alla voce. */
static tone_gen_t s_toni;

static bool configura_codec(void)
{
    struct { uint8_t reg; uint16_t val; const char *cosa; } seq[] = {
        /* Riferimento di tensione e VMID a 50 kOhm — senza, il codec resta
           spento qualunque altra cosa gli si scriva — piu' i due ADC, i mixer
           d'ingresso, e MICBIAS.
           MICBIAS non e' opzionale qui: la capsula che andra' nella cornetta e'
           un ELECTRET, e un electret senza tensione di polarizzazione non e'
           un microfono debole, e' un microfono muto. */
        { R_PWR_MGMT1,    0x0FE, "VREF + VMID + ADC + ingressi + MICBIAS" },

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

        /* Mixer d'uscita e preamplificatori d'ingresso. */
        { R_PWR_MGMT3,    0x03C, "mixer d'uscita + preamplificatori mic" },

        /* Collega INPUT1 all'ingresso invertente del preamplificatore, e il
           preamplificatore al mixer di boost. Sono due passaggi distinti: senza
           il secondo il segnale entra e non arriva da nessuna parte.
           INPUT1 e' dove andra' il filo bianco della cornetta.

           Il boost del microfono e' a 0 dB (bit 5:4 = 00).

           GUADAGNO DA TARARE CON LA CAPSULA VERA. Il 16/09/2026, provando con
           +20 dB di preamplificatore piu' +20 dB di boost, l'ingresso andava a
           saturazione da solo: fondo a 20075 su 32767 con nessuna sorgente
           collegata, cioe' il 61% del fondo scala occupato dal nulla. Sopra non
           resta spazio per un segnale. */
        { R_ADCL_PATH,    0x108, "INPUT1 -> preamp -> boost 0 dB, sinistra" },
        { R_ADCR_PATH,    0x108, "INPUT1 -> preamp -> boost 0 dB, destra" },

        /* Preamplificatore a +20 dB e ingresso non muto. Dopo il reset gli
           ingressi sono MUTI, quindi il bit 7 a zero non e' ridondante.

           +20 dB SCELTO MISURANDO, con la capsula della cornetta collegata, il
           17/09/2026. Il confronto con +30 dB non lascia dubbi:

                        fondo   voce   rapporto   satura
             +20 dB        70   4248       60x    mai
             +30 dB      1800   4500      2,5x    si', due volte

           A +30 dB il fondo sale di venticinque volte mentre la voce resta
           ferma: il preamplificatore amplifica soprattutto il proprio rumore,
           e sui picchi tosa. E' la stessa lezione del guadagno digitale, in
           forma diversa — oltre un certo punto aggiungere guadagno PEGGIORA il
           rapporto segnale/rumore invece di migliorarlo. */
        { R_LIN_VOL,      0x132, "preamp sinistro, +20 dB, non muto" },
        { R_RIN_VOL,      0x132, "preamp destro, +20 dB, non muto" },

        /* Volume digitale degli ADC a 0 dB. */
        { R_LADC_VOL,     0x1C3, "volume ADC sinistro" },
        { R_RADC_VOL,     0x1C3, "volume ADC destro" },

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
    ESP_ERROR_CHECK(i2s_new_channel(&ch, &s_tx, &s_rx));

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
            .din  = PIN_I2S_DIN,
        },
    };
    /* Le due direzioni condividono peripheral, pin di clock e configurazione:
       e' un solo bus I2S percorso nei due versi. */
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_tx, &std));
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_rx, &std));
    ESP_ERROR_CHECK(i2s_channel_enable(s_tx));
    ESP_ERROR_CHECK(i2s_channel_enable(s_rx));

    /* Un momento perche' i clock si assestino prima di mandare campioni. */
    vTaskDelay(pdMS_TO_TICKS(50));

    s_pronto = true;
    ESP_LOGI(TAG, "I2S full duplex a %d Hz, MCLK su GPIO%d, codec configurato",
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

/* Legge `n` campioni mono dal microfono. Il codec manda stereo: si tiene il
   canale sinistro, che e' quello dove arriva LINPUT1 — il filo bianco della
   cornetta. */
bool hal_audio_record(int16_t *mono, size_t n)
{
    if (!s_pronto || !mono || n == 0) {
        return false;
    }

    int16_t stereo[128 * 2];
    while (n > 0) {
        const size_t blocco = n > 128 ? 128 : n;
        size_t letti = 0;
        const esp_err_t err = i2s_channel_read(s_rx, stereo,
                                               blocco * 2 * sizeof(int16_t),
                                               &letti, 200);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "lettura I2S fallita: %s", esp_err_to_name(err));
            return false;
        }
        if (letti == 0) {
            ESP_LOGE(TAG, "lettura I2S vuota: nessun campione dal codec");
            return false;
        }
        const size_t campioni = letti / (2 * sizeof(int16_t));
        for (size_t i = 0; i < campioni; i++) {
            mono[i] = stereo[i * 2];
        }
        mono += campioni;
        n    -= campioni;
    }
    return true;
}

/* --- il ponte fra Bluetooth e cornetta ------------------------------------ */

void hal_audio_rx_push(const uint8_t *pcm, size_t n)
{
    if (s_rb_rx) {
        /* Timeout zero: se la coda e' piena si scarta. Meglio perdere 8 ms di
           audio che bloccare il task dello stack Bluetooth. */
        xRingbufferSend(s_rb_rx, pcm, n, 0);
    }
}

/*
 * Estrae fino a `n` byte da una coda, ANCHE SE SI AVVOLGONO.
 *
 * Una coda circolare tiene i dati in un anello, e quando la richiesta cade a
 * cavallo della fine xRingbufferReceiveUpTo restituisce solo il primo tratto
 * contiguo. Serve percio' una seconda estrazione per il resto.
 *
 * La prima stesura non la faceva e in quel caso BUTTAVA VIA il frammento
 * restituendo zero. Succedeva a ogni giro dell'anello, cioe' regolarmente, e
 * si sentiva come un gracchio periodico all'altro capo della conversazione.
 */
static size_t estrai(RingbufHandle_t rb, uint8_t *dst, size_t n)
{
    size_t presi = 0;
    while (presi < n) {
        size_t tratto = 0;
        uint8_t *d = xRingbufferReceiveUpTo(rb, &tratto, 0, n - presi);
        if (!d || tratto == 0) {
            break;
        }
        memcpy(dst + presi, d, tratto);
        vRingbufferReturnItem(rb, d);
        presi += tratto;
    }
    return presi;
}

size_t hal_audio_tx_pop(uint8_t *pcm, size_t n)
{
    if (!s_rb_tx) {
        return 0;
    }
    /* Lo stack vuole esattamente n byte o niente: un frame parziale lo
       manderebbe fuori sincrono. Ma "parziale" deve significare che i dati non
       c'erano, non che erano a cavallo della fine dell'anello. */
    const size_t presi = estrai(s_rb_tx, pcm, n);
    return (presi == n) ? presi : 0;
}

void hal_audio_set_chiamata(bool attiva)
{
    /* Ogni chiamata riparte con la coda da riempire: quella precedente ha
       lasciato residui che non c'entrano niente con questa conversazione. */
    s_rx_avviato = false;
    s_in_chiamata = attiva;
    ESP_LOGI(TAG, "audio %s", attiva ? "in chiamata" : "a riposo");
}

void hal_out_play_tone(tone_t tone)
{
    tone_set(&s_toni, tone);
}

/*
 * Il task audio, e l'unico punto in cui si tocca l'I2S.
 *
 * Il ritmo lo detta la LETTURA dal microfono: l'I2S consegna i campioni a
 * 16 kHz esatti, quindi il ciclo gira da solo alla velocita' giusta senza
 * bisogno di temporizzatori. E' anche il motivo per cui si legge sempre, anche
 * fuori dalla chiamata.
 */
static void audio_task(void *arg)
{
    (void)arg;
    static int16_t mic[FRAME];
    static int16_t capsula[FRAME];

    for (;;) {
        if (!hal_audio_record(mic, FRAME)) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        if (s_in_chiamata) {
            /* Verso il cellulare. */
            xRingbufferSend(s_rb_tx, mic, sizeof(mic), 0);
            hal_bt_audio_pronto();

            /* Dal cellulare, con scorta.
               Finche' non c'e' abbastanza materiale accumulato si manda
               silenzio invece di consumare: e' il riempimento iniziale, e si
               rifa' ogni volta che la coda si svuota. */
            size_t n = 0;
            const size_t in_coda = RB_BYTES - xRingbufferGetCurFreeSize(s_rb_rx);

            if (!s_rx_avviato) {
                if (in_coda >= RX_SCORTA) {
                    s_rx_avviato = true;
                }
            } else if (in_coda < sizeof(capsula)) {
                /* Svuotata: si torna ad accumulare invece di procedere a
                   singhiozzo. */
                s_rx_avviato = false;
            }

            if (s_rx_avviato) {
                n = estrai(s_rb_rx, (uint8_t *)capsula, sizeof(capsula));
            }
            if (n < sizeof(capsula)) {
                memset((uint8_t *)capsula + n, 0, sizeof(capsula) - n);
            }
        } else {
            /* Fuori dalla chiamata l'uscita e' dei toni di sistema. */
            tone_fill(&s_toni, capsula, FRAME);
        }

        hal_audio_play(capsula, FRAME);
    }
}

void hal_audio_start(void)
{
    if (!s_pronto) {
        return;
    }
    s_rb_rx = xRingbufferCreate(RB_BYTES, RINGBUF_TYPE_BYTEBUF);
    s_rb_tx = xRingbufferCreate(RB_BYTES, RINGBUF_TYPE_BYTEBUF);
    if (!s_rb_rx || !s_rb_tx) {
        ESP_LOGE(TAG, "code audio non create");
        return;
    }
    tone_init(&s_toni);
    xTaskCreate(audio_task, "audio", 4096, NULL, 7, NULL);
    ESP_LOGI(TAG, "pompa audio avviata, %d campioni per giro", FRAME);
}
