/*
 * diag_dial.c — strumento di misura per il disco combinatore.
 *
 * Sostituisce l'applicazione quando si compila con VT_DIAG_DIAL definito.
 * Non usa il PCNT: al PCNT interessa il conteggio, a noi interessano i
 * singoli fronti e la loro distanza nel tempo, che e' cio' che distingue un
 * impulso vero da un rimbalzo.
 *
 * Il repo v1 su Raspberry aveva src/test_hardware.py per questo mestiere;
 * questa e' la sua controparte.
 *
 * Come si legge l'uscita:
 *
 *   IMP  1 dopo    98234 us   <- fronte a salire, 98 ms dopo il precedente
 *   IMP  0 dopo     1180 us   <- 1,1 ms: troppo vicino, e' un rimbalzo
 *
 * Un disco a 10 impulsi al secondo produce fronti "veri" distanti circa
 * 100 ms. Tutto cio' che sta sotto le poche decine di millisecondi non puo'
 * essere un impulso composto: e' il contatto che rimbalza.
 */

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"

#define DIAG_PIN_IMP  4
#define DIAG_PIN_NSI  32

static const char *TAG = "diag";

typedef struct {
    uint8_t  pin;
    uint8_t  level;
    int64_t  us;
} fronte_t;

static QueueHandle_t s_q;

static void IRAM_ATTR isr(void *arg)
{
    const uint32_t pin = (uint32_t)(uintptr_t)arg;
    fronte_t f = {
        .pin   = (uint8_t)pin,
        .level = (uint8_t)gpio_get_level(pin),
        .us    = esp_timer_get_time(),
    };
    BaseType_t woken = pdFALSE;
    xQueueSendFromISR(s_q, &f, &woken);
    if (woken) {
        portYIELD_FROM_ISR();
    }
}

static void diag_task(void *arg)
{
    (void)arg;
    fronte_t f;
    int64_t  ultimo[40] = {0};   /* ultimo fronte per pin */
    uint32_t n_imp = 0, n_nsi = 0;

    for (;;) {
        if (xQueueReceive(s_q, &f, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        const int64_t prec = ultimo[f.pin];
        const int64_t delta = prec ? (f.us - prec) : 0;
        ultimo[f.pin] = f.us;

        if (f.pin == DIAG_PIN_IMP) {
            n_imp++;
            ESP_LOGI(TAG, "IMP  %u  dopo %8lld us   (fronte #%lu)",
                     f.level, (long long)delta, (unsigned long)n_imp);
        } else {
            n_nsi++;
            ESP_LOGW(TAG, "NSI  %u  dopo %8lld us   (fronte #%lu)",
                     f.level, (long long)delta, (unsigned long)n_nsi);
        }
    }
}

void diag_dial_run(void)
{
    s_q = xQueueCreate(256, sizeof(fronte_t));
    configASSERT(s_q);

    gpio_config_t in = {
        .pin_bit_mask = (1ULL << DIAG_PIN_IMP) | (1ULL << DIAG_PIN_NSI),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .intr_type    = GPIO_INTR_ANYEDGE,
    };
    ESP_ERROR_CHECK(gpio_config(&in));

    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    ESP_ERROR_CHECK(gpio_isr_handler_add(DIAG_PIN_IMP, isr, (void *)DIAG_PIN_IMP));
    ESP_ERROR_CHECK(gpio_isr_handler_add(DIAG_PIN_NSI, isr, (void *)DIAG_PIN_NSI));

    xTaskCreate(diag_task, "diag", 4096, NULL, 6, NULL);

    ESP_LOGW(TAG, "=== DIAGNOSTICO DISCO ===");
    ESP_LOGW(TAG, "impulsi=GPIO%d  NSI=GPIO%d  — livelli a riposo: IMP=%d NSI=%d",
             DIAG_PIN_IMP, DIAG_PIN_NSI,
             gpio_get_level(DIAG_PIN_IMP), gpio_get_level(DIAG_PIN_NSI));
    ESP_LOGW(TAG, "componi una cifra alla volta e osserva le distanze fra i fronti");
}
