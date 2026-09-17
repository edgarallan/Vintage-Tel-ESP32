/*
 * diag_audio.c — prova della catena di riproduzione.
 *
 * Sostituisce l'applicazione quando si compila con VT_DIAG_AUDIO definito.
 *
 * Suona in continuo il tono di libero italiano — 425 Hz — usando il generatore
 * di core/tones.c. La scelta della sorgente non e' casuale: quel codice e' gia'
 * coperto dai test che girano sul PC, quindi se non si sente niente il
 * colpevole non e' lui. Restano MCLK, I2S, i registri del codec e il
 * cablaggio, che e' esattamente cio' che questa prova deve interrogare.
 *
 * COME ASCOLTARLO SENZA SALDARE NIENTE. La scheda WM8960 ha un jack cuffia da
 * 3,5 mm a bordo, collegato alla stessa uscita a cui andra' la capsula della
 * cornetta. Infilaci un paio di cuffie: se il tono si sente li', la catena
 * funziona tutta, e la cornetta diventa solo una questione di fili.
 *
 * Cosa dice il risultato:
 *
 *   tono pulito e continuo   tutto a posto
 *   silenzio                 MCLK assente, o DAC ancora muto (R5), o il DAC
 *                            non e' instradato al mixer (R34/R37)
 *   ronzio o crepitio        clock sbagliato: MCLK non a 256 x fs, oppure
 *                            l'I2S sta girando a un rate diverso dal codec
 *   tono ma distorto         volumi troppo alti, o formato dati non allineato
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <math.h>
#include <string.h>

#include "esp_log.h"

#include "hal_priv.h"
#include "tones.h"

static const char *TAG = "diag_audio";

#define CAMPIONI  256   /* 16 ms a 16 kHz: abbastanza corti da non far scattare
                           il watchdog, abbastanza lunghi da non sprecare CPU */

/*
 * Il WM8960 ha tre ingressi per canale e la scheda ne usa uno, ma quale non e'
 * scritto da nessuna parte raggiungibile: la documentazione Waveshare risponde
 * 403 e il driver Linux non lo dice, perche' e' informazione della scheda e non
 * del chip.
 *
 * Quindi si chiede alla scheda. INPUT1 passa dal preamplificatore; INPUT2 e
 * INPUT3 entrano direttamente nel mixer di boost, con un guadagno proprio.
 */
#define R_ADCL_PATH   0x20
#define R_ADCR_PATH   0x21
#define R_LIN_BOOST   0x2B
#define R_RIN_BOOST   0x2C

static void scegli_ingresso(int quale)
{
    switch (quale) {
    case 1:   /* INPUT1 attraverso il preamplificatore, boost +20 dB */
        hal_codec_write(R_ADCL_PATH, 0x128);
        hal_codec_write(R_ADCR_PATH, 0x128);
        hal_codec_write(R_LIN_BOOST, 0x000);
        hal_codec_write(R_RIN_BOOST, 0x000);
        break;
    case 2:   /* INPUT2 dritto nel mixer di boost, guadagno massimo */
        hal_codec_write(R_ADCL_PATH, 0x000);
        hal_codec_write(R_ADCR_PATH, 0x000);
        hal_codec_write(R_LIN_BOOST, 0x00E);
        hal_codec_write(R_RIN_BOOST, 0x00E);
        break;
    default:  /* INPUT3, idem */
        hal_codec_write(R_ADCL_PATH, 0x000);
        hal_codec_write(R_ADCR_PATH, 0x000);
        hal_codec_write(R_LIN_BOOST, 0x070);
        hal_codec_write(R_RIN_BOOST, 0x070);
        break;
    }
}

/*
 * I segnali acustici che scandiscono la prova.
 *
 * NON due altezze diverse: due RITMI. Il primo tentativo usava il tono di
 * conferma tasto per dire "taci" e quello di libero per dire "parla", ma il
 * primo dura 50 millisecondi e passa per un clic, non per un segnale. Il ritmo
 * invece si riconosce anche distrattamente, ed e' quello che serve a chi ha le
 * mani sul telefono e non guarda lo schermo.
 *
 *   TRE bip corti  ->  stai zitto
 *   UN bip lungo   ->  parla
 */
static void suona(tone_gen_t *gen, int ms)
{
    static int16_t b[CAMPIONI];
    tone_set(gen, TONE_DIAL);
    for (uint32_t i = 0; i < (uint32_t)ms * TONE_SAMPLE_RATE / 1000 / CAMPIONI; i++) {
        tone_fill(gen, b, CAMPIONI);
        hal_audio_play(b, CAMPIONI);
    }
}

static void pausa(int ms)
{
    static int16_t z[CAMPIONI];
    memset(z, 0, sizeof(z));
    for (uint32_t i = 0; i < (uint32_t)ms * TONE_SAMPLE_RATE / 1000 / CAMPIONI; i++) {
        hal_audio_play(z, CAMPIONI);
    }
}

static void segnale_taci(tone_gen_t *gen)
{
    for (int i = 0; i < 3; i++) {
        suona(gen, 120);
        pausa(120);
    }
    pausa(300);
}

static void segnale_parla(tone_gen_t *gen)
{
    suona(gen, 900);
    pausa(300);
}

/* Media del valore assoluto su `secondi` di registrazione.
 *
 * NON rimanda niente in cuffia. La versione precedente lo faceva, e con +40 dB
 * di guadagno microfono e cuffia si innescavano: un fischio fortissimo che
 * copriva tutto e rendeva la misura inutile. Per misurare un microfono non
 * serve risentirlo.
 *
 * La media e non il picco: un disturbo isolato alza il picco quanto una voce,
 * la media no. */
__attribute__((unused)) static int32_t livello_medio(int16_t *buf, int secondi)
{
    int64_t somma = 0;
    uint32_t n = 0;
    for (uint32_t i = 0; i < (uint32_t)secondi * TONE_SAMPLE_RATE / CAMPIONI; i++) {
        if (!hal_audio_record(buf, CAMPIONI)) {
            return -1;
        }
        for (uint32_t k = 0; k < CAMPIONI; k++) {
            somma += buf[k] < 0 ? -buf[k] : buf[k];
            n++;
        }
    }
    return n ? (int32_t)(somma / n) : 0;
}

#define R_LADC_VOL    0x15
#define R_RADC_VOL    0x16

/* Picco del valore assoluto. Per un colpetto serve il picco e non la media:
   un transitorio dura pochi millisecondi e nella media sparisce. */
static int32_t picco_su(int16_t *buf, int secondi)
{
    int32_t picco = 0;
    for (uint32_t i = 0; i < (uint32_t)secondi * TONE_SAMPLE_RATE / CAMPIONI; i++) {
        if (!hal_audio_record(buf, CAMPIONI)) {
            return -1;
        }
        for (uint32_t k = 0; k < CAMPIONI; k++) {
            const int32_t a = buf[k] < 0 ? -buf[k] : buf[k];
            if (a > picco) {
                picco = a;
            }
        }
    }
    return picco;
}

/*
 * Spazzata del guadagno del preamplificatore, su INPUT1.
 *
 * Che il jack arrivi su INPUT1 lo dicono le misure: INPUT2 e INPUT3 leggono 4
 * su 32767, cioe' silenzio digitale, mentre INPUT1 mostra un fondo vivo. E che
 * il microfono sia sano lo dice il multimetro: 1,09 kOhm fra i due morsetti e
 * 2,1 V di polarizzazione contro i ~2,5 a vuoto, cioe' una capsula che assorbe
 * corrente.
 *
 * Restava il guadagno, e i due estremi erano gia' noti: a 0 dB il segnale resta
 * sepolto, a +40 dB l'ingresso satura da solo (fondo a 20075 su 32767). La
 * risposta sta in mezzo, e si trova spazzando invece di indovinare — la stessa
 * cosa fatta per la frequenza del campanello.
 *
 * LINVOL: 0x17 e' 0 dB, ogni passo vale 0,75 dB.
 */
/*
 * Monitor continuo: nessuna fase, nessun segnale da seguire.
 *
 * Tutte le versioni precedenti chiedevano di alternare silenzio e parlato a
 * comando, e tutte hanno prodotto numeri incoerenti — una volta per un errore
 * di tempismo, un'altra perche' i bip escono dalla CAPSULA D'ASCOLTO DELLA
 * CORNETTA, a pochi centimetri dal microfono, e il diagnostico finiva per
 * misurare il proprio eco invece del silenzio.
 *
 * Qui non c'e' niente da sincronizzare: stampa il picco ogni mezzo secondo,
 * per sempre. Si parla quando si vuole e si tace quando si vuole, e la
 * sequenza dei numeri mostra da sola dove c'era voce. Nessun segnale acustico,
 * quindi nessun eco da misurare.
 */
#define R_LIN_VOL   0x00
#define R_RIN_VOL   0x01
#define GUADAGNO    0x13F   /* +30 dB sul preamplificatore */

void diag_audio_run(void)
{
    hal_codec_init();
    hal_audio_init();
    scegli_ingresso(1);

    hal_codec_write(R_LIN_VOL, GUADAGNO);
    hal_codec_write(R_RIN_VOL, GUADAGNO);

    ESP_LOGW(TAG, "=== MONITOR CONTINUO, INPUT1 a +30 dB ===");
    ESP_LOGW(TAG, "picco ogni mezzo secondo. Parla e taci quando vuoi.");
    ESP_LOGW(TAG, "nessun bip: cosi' il microfono non sente l'eco della capsula");

    static int16_t buf[CAMPIONI];

    for (;;) {
        const int32_t p = picco_su(buf, 1);   /* ~1 s di finestra */
        if (p < 0) {
            ESP_LOGE(TAG, "il microfono non manda dati");
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }
        /* Una barra oltre al numero: la forma si legge a colpo d'occhio nella
           colonna dei log, e una voce si riconosce dal profilo. */
        char barra[41];
        int n = (int)(p * 40 / 32767);
        if (n > 40) { n = 40; }
        for (int i = 0; i < n; i++)  { barra[i] = '#'; }
        barra[n] = '\0';
        ESP_LOGI(TAG, "%5ld |%s", (long)p, barra);
    }
}
