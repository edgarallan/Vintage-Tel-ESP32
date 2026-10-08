/*
 * riavvio.c — perche' il telefono si e' riavviato, ricordato fra un avvio e
 * l'altro.
 *
 * Il telefono vive a batteria e dentro una scocca chiusa: quando si blocca o
 * si riavvia da solo nessuno sta guardando la seriale. Il motivo dell'ultimo
 * riavvio pero' l'ESP32 lo conosce al boot successivo, e qui finisce in NVS
 * insieme a un contatore. La prossima volta che si attacca l'USB il log dice
 * cosa e' successo nel frattempo, invece di lasciarlo indovinare.
 */

#include "esp_log.h"
#include "esp_system.h"
#include "nvs.h"

#include "riavvio.h"

static const char *TAG = "riavvio";

#define NVS_SPAZIO        "vtel"
#define NVS_ANOMALI       "rst_n"
#define NVS_ULTIMO        "rst_ultimo"

static const char *nome(esp_reset_reason_t r)
{
    switch (r) {
    case ESP_RST_POWERON:   return "accensione";
    case ESP_RST_SW:        return "riavvio da software";
    case ESP_RST_PANIC:     return "crash del programma";
    case ESP_RST_INT_WDT:   return "watchdog delle interruzioni";
    case ESP_RST_TASK_WDT:  return "watchdog dei task (telefono bloccato)";
    case ESP_RST_WDT:       return "watchdog di sistema";
    case ESP_RST_BROWNOUT:  return "calo di tensione (batteria scarica?)";
    case ESP_RST_DEEPSLEEP: return "risveglio";
    case ESP_RST_EXT:       return "pulsante di reset";
    default:                return "motivo sconosciuto";
    }
}

static bool anomalo(esp_reset_reason_t r)
{
    return r == ESP_RST_PANIC || r == ESP_RST_INT_WDT || r == ESP_RST_TASK_WDT ||
           r == ESP_RST_WDT || r == ESP_RST_BROWNOUT;
}

void riavvio_registra(void)
{
    const esp_reset_reason_t r = esp_reset_reason();

    nvs_handle_t h;
    if (nvs_open(NVS_SPAZIO, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGW(TAG, "avvio per %s (storico non disponibile)", nome(r));
        return;
    }

    uint32_t n = 0;
    uint8_t  ultimo = ESP_RST_UNKNOWN;
    nvs_get_u32(h, NVS_ANOMALI, &n);
    nvs_get_u8(h, NVS_ULTIMO, &ultimo);

    if (anomalo(r)) {
        n++;
        ultimo = (uint8_t)r;
        nvs_set_u32(h, NVS_ANOMALI, n);
        nvs_set_u8(h, NVS_ULTIMO, ultimo);
        nvs_commit(h);
        ESP_LOGW(TAG, "avvio dopo un riavvio anomalo: %s", nome(r));
    } else {
        ESP_LOGI(TAG, "avvio per %s", nome(r));
    }
    nvs_close(h);

    if (n > 0) {
        ESP_LOGW(TAG, "riavvii anomali finora: %lu, l'ultimo per %s",
                 (unsigned long)n, nome((esp_reset_reason_t)ultimo));
    }
}
