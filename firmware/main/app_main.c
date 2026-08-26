/*
 * app_main.c — avvio e task unico della macchina a stati.
 *
 * L'intera architettura sta in questa immagine:
 *
 *   ISR gancio  ─┐
 *   PCNT disco  ─┼─→ xQueueSend(evt_q) ─→ phone_task ─→ phone_handle()
 *   callback HFP ┤                          (unico proprietario dello stato)
 *   esp_timer   ─┘
 *
 * Un solo task tocca il phone_t. Non essendoci concorrenza non c'e' nulla da
 * proteggere, ed e' per questo che qui non troverai un mutex: nella v1 in
 * Python serviva un lock asyncio, qui la coda lo rende superfluo. Se un
 * giorno venisse voglia di aggiungerne uno, vuol dire che qualcuno ha
 * cominciato a toccare lo stato da fuori — ed e' quello il bug da correggere.
 */

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"

#include "phone_hal.h"
#include "phone_fsm.h"
#include "phonebook.h"

static const char *TAG = "vintage-tel";

/* Il battito fa scadere i timeout: interdigit, quick-dial, cadenza di squillo.
   100 ms sono abbondantemente sotto il piu' corto di quei tempi (1,5 s). */
#define TICK_PERIOD_US  (100 * 1000)

#define EVT_QUEUE_LEN   16

static QueueHandle_t s_evt_q;
static phone_t       s_phone;
static phonebook_t   s_pb;

static void tick_cb(void *arg)
{
    (void)arg;
    phone_ev_t ev = { .type = EV_TICK, .now_ms = hal_now_ms() };
    /* Se la coda e' piena il battito si perde, e va benissimo: il prossimo
       arriva fra 100 ms. Bloccare qui fermerebbe il timer di sistema. */
    xQueueSend(s_evt_q, &ev, 0);
}

static void phone_task(void *arg)
{
    (void)arg;
    phone_ev_t ev;

    for (;;) {
        if (xQueueReceive(s_evt_q, &ev, portMAX_DELAY) == pdTRUE) {
            phone_handle(&s_phone, &ev);
        }
    }
}

void app_main(void)
{
    /* NVS serve allo stack Bluetooth per le chiavi di accoppiamento, oltre
       che alla configurazione del telefono. */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    s_evt_q = xQueueCreate(EVT_QUEUE_LEN, sizeof(phone_ev_t));
    configASSERT(s_evt_q);

    const hw_iface_t *hw = hal_init(s_evt_q);

    pb_init(&s_pb);

    const phone_config_t cfg = {
        .interdigit_ms = 8000,
        .quickdial_ms  = 1500,
        .busy_ms       = 3000,
    };
    const ring_config_t ring_cfg = {
        .on_ms     = 1000,   /* cadenza italiana: 1 s di squillo... */
        .off_ms    = 4000,   /* ...e 4 di pausa */
        .max_rings = 0,      /* senza limite: smette quando smette il chiamante */
    };
    phone_init(&s_phone, hw, &s_pb, &cfg, &ring_cfg);

    const esp_timer_create_args_t tick_args = {
        .callback = tick_cb,
        .name     = "tick",
    };
    esp_timer_handle_t tick;
    ESP_ERROR_CHECK(esp_timer_create(&tick_args, &tick));
    ESP_ERROR_CHECK(esp_timer_start_periodic(tick, TICK_PERIOD_US));

    xTaskCreate(phone_task, "phone", 4096, NULL, 5, NULL);

    ESP_LOGI(TAG, "avviato");
}
