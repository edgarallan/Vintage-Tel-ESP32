/*
 * hal_bell.c — il campanello elettromeccanico originale.
 *
 * Le bobine vogliono corrente alternata: alimentate in continua restano
 * attirate e il martelletto non si muove. L'onda quadra a ~22 Hz nasce
 * alternando IN1 e IN2 del ponte H in antifase, cosi' la tensione ai capi
 * della bobina cambia segno a ogni mezzo periodo.
 *
 * La cadenza 1 s / 4 s NON sta qui: e' logica, vive in core/ring_pattern.c
 * ed e' testata sul PC. Qui c'e' solo il rubinetto.
 *
 * Il ponte montato e' un L298N con ENA tenuto alto da un ponticello. Il codice
 * sarebbe identico con un DRV8871 — dal lato firmware i due si comandano allo
 * stesso modo — ma il comportamento a riposo cambia: con ENA alto l'L298N non
 * va in alta impedenza, e IN1=IN2=0 accende entrambi i lati bassi
 * CORTOCIRCUITANDO la bobina. E' una frenata, non un coast, e per un
 * martelletto e' meglio: la corrente residua si smorza e smette di vibrare
 * subito invece di continuare per inerzia.
 *
 * ATTENZIONE: a valle di questi due pin c'e' una linea a 26-28 V generata
 * dall'XL6009, e sull'L298N ci sono due ponticelli che vanno messi giusti prima
 * di collegare qualsiasi cosa, pena distruggere il modulo. Le modifiche al
 * cablaggio vanno in hardware/bell_driver.md.
 */

#include "hal_priv.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "hal_bell";

/* ~22 Hz: mezzo periodo = 1/(22*2) s ≈ 22,7 ms. La frequenza di risonanza
   delle bobine sta tra 20 e 25 Hz — vedi hardware/bell_driver.md. */
#define BELL_HALF_PERIOD_US 22727

static esp_timer_handle_t s_timer;
static bool               s_phase;

static void bell_tick(void *arg)
{
    (void)arg;
    s_phase = !s_phase;
    gpio_set_level(PIN_BELL_IN1, s_phase ? 1 : 0);
    gpio_set_level(PIN_BELL_IN2, s_phase ? 0 : 1);
}

void hal_bell_init(void)
{
    gpio_config_t out = {
        .pin_bit_mask = (1ULL << PIN_BELL_IN1) | (1ULL << PIN_BELL_IN2),
        .mode         = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&out));

    /* Entrambi bassi = ponte a riposo, nessuna corrente nelle bobine. */
    gpio_set_level(PIN_BELL_IN1, 0);
    gpio_set_level(PIN_BELL_IN2, 0);

    const esp_timer_create_args_t args = {
        .callback = bell_tick,
        .name     = "bell",
        /* Il callback tocca due GPIO e nient'altro: sta nel task del timer
           senza bisogno di finire in IRAM. */
        .dispatch_method = ESP_TIMER_TASK,
    };
    ESP_ERROR_CHECK(esp_timer_create(&args, &s_timer));
}

void hal_bell_start(void)
{
    if (esp_timer_is_active(s_timer)) {
        return;
    }
    s_phase = false;
    ESP_ERROR_CHECK(esp_timer_start_periodic(s_timer, BELL_HALF_PERIOD_US));
    ESP_LOGI(TAG, "campanello ON");
}

void hal_bell_stop(void)
{
    if (esp_timer_is_active(s_timer)) {
        esp_timer_stop(s_timer);
    }
    /* Lasciare un lato alto terrebbe le bobine sotto corrente continua:
       si scalderebbero senza suonare. */
    gpio_set_level(PIN_BELL_IN1, 0);
    gpio_set_level(PIN_BELL_IN2, 0);
    ESP_LOGI(TAG, "campanello OFF");
}
