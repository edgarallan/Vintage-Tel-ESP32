/*
 * diag_bell.c — collaudo e taratura del campanello.
 *
 * Sostituisce l'applicazione quando si compila con VT_DIAG_BELL definito.
 *
 * Serve a due cose diverse. La prima e' banale: vedere se il campanello suona,
 * senza bisogno che qualcuno chiami. La seconda e' il motivo per cui esiste.
 *
 * Le bobine e il martelletto formano un sistema meccanico con una frequenza di
 * risonanza propria, che dipende dalle molle, dalla massa del martelletto e da
 * cinquant'anni di grasso indurito. Sta fra i 20 e i 25 Hz, ma DOVE esattamente
 * non e' deducibile: va sentito. Alla frequenza giusta il martelletto sbatte
 * pieno con la stessa corrente; a un paio di Hz di distanza tintinna e basta.
 *
 * Questo diagnostico spazza da 16 a 28 Hz, un gradino ogni tre secondi,
 * annunciando ciascuno a log. Si ascolta, si sceglie quello che suona meglio e
 * si mette in BELL_HALF_PERIOD_US di hal_bell.c. Stessa disciplina usata per
 * l'antirimbalzo: misurare invece di stimare.
 *
 * ATTENZIONE: a valle ci sono 27 V. Prima di collegare, leggere le due
 * trappole dei ponticelli in hardware/bell_driver.md.
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"

#include "hal_priv.h"

static const char *TAG = "diag_bell";

/* Estremi della spazzata, in Hz. Il campanello e' progettato per i 25 Hz della
   centrale telefonica, ma un apparecchio vecchio puo' essersi spostato. */
#define HZ_MIN     16
#define HZ_MAX     28
#define HZ_PASSO   1

/* Quanto suona ogni gradino. Tre secondi bastano a giudicare e non sono cosi'
   tanti da rendere la spazzata interminabile. */
#define SUONO_MS   3000
#define PAUSA_MS   1500

void diag_bell_run(void)
{
    hal_bell_init();

    ESP_LOGW(TAG, "=== TARATURA CAMPANELLO ===");
    ESP_LOGW(TAG, "spazzata da %d a %d Hz, %d s per gradino", HZ_MIN, HZ_MAX,
             SUONO_MS / 1000);
    ESP_LOGW(TAG, "ascolta e dimmi a quale suona piu' pieno");

    for (;;) {
        for (int hz = HZ_MIN; hz <= HZ_MAX; hz += HZ_PASSO) {
            ESP_LOGI(TAG, "%2d Hz", hz);
            hal_bell_set_hz(hz);
            hal_bell_start();
            vTaskDelay(pdMS_TO_TICKS(SUONO_MS));
            hal_bell_stop();
            vTaskDelay(pdMS_TO_TICKS(PAUSA_MS));
        }
        ESP_LOGW(TAG, "--- spazzata finita, ricomincio ---");
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}
