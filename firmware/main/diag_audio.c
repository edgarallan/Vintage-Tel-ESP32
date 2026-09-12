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
 * Genera una spazzata di frequenza invece di un tono fisso.
 *
 * Un tono a 425 Hz e un ronzio si confondono facilmente: sono entrambi un
 * suono continuo, e chi ascolta deve giudicare un timbro. Una frequenza che
 * SALE non si confonde con niente — nessun disturbo di alimentazione o di
 * clock cambia altezza in modo regolare.
 *
 * Se senti una sirena che sale, la catena audio e' sana e il segnale arriva
 * intatto. Se senti sempre lo stesso ronzio, i dati arrivano mangiati e il
 * problema e' nel formato o nel clock, non nel volume.
 */
static void spazzata(int16_t *buf, uint32_t n, float *fase, float *hz)
{
    for (uint32_t i = 0; i < n; i++) {
        *fase += 2.0f * (float)M_PI * (*hz) / (float)TONE_SAMPLE_RATE;
        if (*fase > 2.0f * (float)M_PI) {
            *fase -= 2.0f * (float)M_PI;
        }
        /* 0,8 del fondo scala: e' una prova, serve sentire bene. In
           esercizio i toni restano a TONE_AMPLITUDE, piu' prudente. */
        buf[i] = (int16_t)(sinf(*fase) * 26000.0f);

        /* Da 200 a 2000 Hz in quattro secondi, poi si ricomincia. */
        *hz += 1800.0f / (4.0f * (float)TONE_SAMPLE_RATE);
        if (*hz > 2000.0f) {
            *hz = 200.0f;
        }
    }
}

void diag_audio_run(void)
{
    hal_codec_init();
    hal_audio_init();

    ESP_LOGW(TAG, "=== PROVA AUDIO ===");
    ESP_LOGW(TAG, "spazzata da 200 a 2000 Hz in 4 s, poi silenzio 2 s, in ciclo");
    ESP_LOGW(TAG, "se senti una sirena che SALE, la catena e' sana");
    ESP_LOGW(TAG, "se senti sempre lo stesso ronzio, i dati arrivano mangiati");

    static int16_t buf[CAMPIONI];
    float fase = 0.0f, hz = 200.0f;

    const uint32_t buf_sirena  = 4 * TONE_SAMPLE_RATE / CAMPIONI;
    const uint32_t buf_silenzio = 2 * TONE_SAMPLE_RATE / CAMPIONI;

    for (;;) {
        ESP_LOGI(TAG, "SIRENA 200 -> 2000 Hz");
        hz = 200.0f;
        for (uint32_t i = 0; i < buf_sirena; i++) {
            spazzata(buf, CAMPIONI, &fase, &hz);
            if (!hal_audio_play(buf, CAMPIONI)) {
                ESP_LOGE(TAG, "l'I2S non accetta dati");
                vTaskDelay(pdMS_TO_TICKS(1000));
                break;
            }
        }

        ESP_LOGI(TAG, "SILENZIO");
        memset(buf, 0, sizeof(buf));
        for (uint32_t i = 0; i < buf_silenzio; i++) {
            hal_audio_play(buf, CAMPIONI);
        }
    }
}
