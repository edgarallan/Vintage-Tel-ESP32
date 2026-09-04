/*
 * diag_input.c — strumento di misura per i contatti meccanici del telefono.
 *
 * Sostituisce l'applicazione quando si compila con VT_DIAG_INPUT definito.
 * Non usa il PCNT: al PCNT interessa il conteggio, a noi interessano i
 * singoli fronti e la loro distanza nel tempo, che e' cio' che distingue un
 * impulso vero da un rimbalzo.
 *
 * Il repo v1 su Raspberry aveva src/test_hardware.py per questo mestiere;
 * questa e' la sua controparte.
 *
 * Nato per il solo disco combinatore, ora guarda tutti e quattro gli ingressi:
 * il 27/08/2026 una costante di assestamento unica ha reso il gancio
 * inservibile mentre il disco funzionava, e si e' scoperto che ogni contatto
 * rimbalza a modo suo. Ogni soglia va quindi misurata sul suo pin.
 *
 * Come si legge l'uscita. Per ogni fronte:
 *
 *   GANCIO   1  dopo    12480 us   (fronte #3)
 *
 * e alla fine di ogni raffica, quando la linea sta ferma per mezzo secondo:
 *
 *   GANCIO   raffica chiusa: 7 fronti in 243 ms, livello finale 0
 *   GANCIO   raffica piu' lunga finora: 243 ms
 *
 * Il numero da guardare NON e' pero' la durata totale della raffica, ma il
 * PEGGIOR INTERVALLO fra due fronti consecutivi al suo interno: l'attesa in
 * hal_input.c e' ritriggerabile, e ogni fronte fa ripartire il conto. Per
 * quello serve la colonna "dopo N us" dei singoli fronti.
 *
 * ATTENZIONE alla colonna del livello: qui l'ISR chiama gpio_get_level(), e
 * durante un rimbalzo veloce la linea cambia prima che la lettura avvenga.
 * Si vedono percio' fronti consecutivi con lo stesso livello - artefatto
 * della misura, non del contatto. Di queste righe fidati dei TEMPI, non dei
 * livelli. Il driver vero non ha il problema: l'ISR marca solo l'istante, e
 * il livello viene letto dal task dopo l'assestamento.
 */

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "hal_priv.h"

/* Quanto la linea deve stare ferma perche' la raffica sia considerata finita.
   Mezzo secondo e' molto piu' del rimbalzo piu' lento misurato (~260 ms) e
   molto meno della pausa fra due gesti umani, quindi separa le raffiche senza
   spezzarle. */
#define QUIETE_US   500000

static const char *TAG = "diag";

typedef struct {
    uint8_t     pin;
    const char *nome;
} ingresso_t;

static const ingresso_t s_in[] = {
    { PIN_DIAL_PULSE, "IMPULSI" },
    { PIN_DIAL_NSI,   "NSI    " },
    { PIN_HOOK,       "GANCIO " },
    { PIN_BUTTON,     "PULSANT" },
};
#define N_IN  (sizeof(s_in) / sizeof(s_in[0]))

/* Una raffica in corso su un ingresso. */
typedef struct {
    int64_t  primo_us;   /* primo fronte della raffica */
    int64_t  ultimo_us;  /* fronte piu' recente */
    uint32_t fronti;     /* quanti fronti nella raffica; 0 = nessuna aperta */
    int64_t  peggiore_us;/* durata della raffica piu' lunga vista finora */
    uint8_t  livello;    /* livello dell'ultimo fronte */
} raffica_t;

static raffica_t     s_raf[N_IN];
static QueueHandle_t s_q;

typedef struct {
    uint8_t  idx;
    uint8_t  livello;
    int64_t  us;
} fronte_t;

static void IRAM_ATTR isr(void *arg)
{
    const uint32_t idx = (uint32_t)(uintptr_t)arg;
    fronte_t f = {
        .idx     = (uint8_t)idx,
        .livello = (uint8_t)gpio_get_level(s_in[idx].pin),
        .us      = esp_timer_get_time(),
    };
    BaseType_t woken = pdFALSE;
    xQueueSendFromISR(s_q, &f, &woken);
    if (woken) {
        portYIELD_FROM_ISR();
    }
}

/* Chiude le raffiche rimaste ferme abbastanza a lungo e stampa il verdetto. */
static void chiudi_raffiche_ferme(int64_t ora_us)
{
    for (size_t i = 0; i < N_IN; i++) {
        raffica_t *r = &s_raf[i];
        if (r->fronti == 0 || (ora_us - r->ultimo_us) < QUIETE_US) {
            continue;
        }

        const int64_t durata = r->ultimo_us - r->primo_us;
        if (durata > r->peggiore_us) {
            r->peggiore_us = durata;
        }

        ESP_LOGW(TAG, "%s  raffica chiusa: %lu fronti in %lld ms, livello finale %u",
                 s_in[i].nome, (unsigned long)r->fronti,
                 (long long)(durata / 1000), r->livello);
        ESP_LOGE(TAG, "%s  raffica piu' lunga finora: %lld ms",
                 s_in[i].nome, (long long)(r->peggiore_us / 1000));

        r->fronti = 0;
    }
}

static void diag_task(void *arg)
{
    (void)arg;
    fronte_t f;

    for (;;) {
        /* Il timeout serve a chiudere le raffiche anche quando non arrivano
           piu' fronti: senza, il verdetto resterebbe in sospeso. */
        if (xQueueReceive(s_q, &f, pdMS_TO_TICKS(100)) == pdTRUE) {
            raffica_t *r = &s_raf[f.idx];
            const int64_t delta = r->fronti ? (f.us - r->ultimo_us) : 0;

            if (r->fronti == 0) {
                r->primo_us = f.us;
            }
            r->fronti++;
            r->ultimo_us = f.us;
            r->livello   = f.livello;

            ESP_LOGI(TAG, "%s  %u  dopo %8lld us   (fronte #%lu)",
                     s_in[f.idx].nome, f.livello, (long long)delta,
                     (unsigned long)r->fronti);
        }

        chiudi_raffiche_ferme(esp_timer_get_time());
    }
}

void diag_input_run(void)
{
    s_q = xQueueCreate(512, sizeof(fronte_t));
    configASSERT(s_q);

    uint64_t maschera = 0;
    for (size_t i = 0; i < N_IN; i++) {
        maschera |= (1ULL << s_in[i].pin);
    }

    gpio_config_t in = {
        .pin_bit_mask = maschera,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .intr_type    = GPIO_INTR_ANYEDGE,
    };
    ESP_ERROR_CHECK(gpio_config(&in));

    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    for (size_t i = 0; i < N_IN; i++) {
        ESP_ERROR_CHECK(gpio_isr_handler_add(s_in[i].pin, isr,
                                             (void *)(uintptr_t)i));
    }

    xTaskCreate(diag_task, "diag", 4096, NULL, 6, NULL);

    ESP_LOGW(TAG, "=== DIAGNOSTICO INGRESSI ===");
    for (size_t i = 0; i < N_IN; i++) {
        ESP_LOGW(TAG, "%s  GPIO%-2d  livello a riposo: %d",
                 s_in[i].nome, s_in[i].pin, gpio_get_level(s_in[i].pin));
    }
    ESP_LOGW(TAG, "muovi un contatto alla volta; il verdetto arriva dopo mezzo "
                  "secondo di quiete");
}
