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

void diag_audio_run(void)
{
    hal_codec_init();
    hal_audio_init();

    tone_gen_t gen;
    tone_init(&gen);

    ESP_LOGW(TAG, "=== PROVA AUDIO ===");
    ESP_LOGW(TAG, "i toni italiani veri, tre fasi da 3 s, in ciclo");

    static const struct { tone_t tono; const char *nome; } fasi[] = {
        { TONE_DIAL, "LIBERO   425 Hz continuo" },
        { TONE_BUSY, "OCCUPATO 425 Hz, 0,5 s si / 0,5 s no" },
        { TONE_NONE, "SILENZIO" },
    };

    const uint32_t buffer_per_fase = 3 * TONE_SAMPLE_RATE / CAMPIONI;
    static int16_t buf[CAMPIONI];

    for (;;) {
        for (size_t f = 0; f < sizeof(fasi) / sizeof(fasi[0]); f++) {
            ESP_LOGI(TAG, "%s", fasi[f].nome);
            tone_set(&gen, fasi[f].tono);
            for (uint32_t i = 0; i < buffer_per_fase; i++) {
                tone_fill(&gen, buf, CAMPIONI);
                if (!hal_audio_play(buf, CAMPIONI)) {
                    ESP_LOGE(TAG, "l'I2S non accetta dati");
                    vTaskDelay(pdMS_TO_TICKS(1000));
                    break;
                }
            }
        }
    }
}
