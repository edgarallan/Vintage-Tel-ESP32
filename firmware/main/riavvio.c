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

#include "esp_attr.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "nvs.h"

#include "alimentazione.h"
#include "riavvio.h"

static const char *TAG = "riavvio";

#define NVS_SPAZIO        "vtel"
#define NVS_ANOMALI       "rst_n"
#define NVS_ULTIMO        "rst_ultimo"

/* Quanto dormire quando la batteria non regge l'avvio. Abbastanza da non
   insistere ogni quindici secondi, poco abbastanza da ripartire presto se nel
   frattempo arriva il caricatore. */
#define SONNO_MIN         15

/* Dopo quanto un avvio conta come riuscito: un calo di tensione che arriva
   dopo un minuto di funzionamento non fa parte di un circolo. */
#define STABILE_S         60

/* La serie sta nella memoria RTC, che sopravvive ai riavvii per calo di
   tensione senza costare una scrittura in flash a ogni giro. La memoria non
   inizializzata contiene quel che capita all'accensione: la parola magica
   distingue un contatore vero dalla spazzatura. */
#define MAGIA             0x56544C31u   /* "VTL1" */
static RTC_NOINIT_ATTR uint32_t s_magia;
static RTC_NOINIT_ATTR uint32_t s_serie;

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

static void azzera_serie(void *arg)
{
    (void)arg;
    s_serie = 0;
}

/* Interrompe il circolo: dorme prima di accendere il Bluetooth, cioe' prima
   del picco di corrente che lo alimenta. Il consumo scende sotto la soglia a
   cui il modulo batteria taglia l'uscita, quindi spesso questo sonno diventa
   uno spegnimento vero fino all'arrivo del caricatore — che e' proprio quello
   che serve a una cella quasi vuota. */
static void controlla_serie(esp_reset_reason_t r)
{
    if (s_magia != MAGIA) {
        s_magia = MAGIA;
        s_serie = 0;
    }
    s_serie = alim_serie((uint8_t)s_serie, r == ESP_RST_BROWNOUT);

    if (alim_deve_dormire((uint8_t)s_serie)) {
        ESP_LOGW(TAG, "%lu cali di tensione di fila: la batteria non regge. "
                 "Dormo %d minuti, collega il caricatore",
                 (unsigned long)s_serie, SONNO_MIN);
        esp_sleep_enable_timer_wakeup((uint64_t)SONNO_MIN * 60 * 1000 * 1000);
        esp_deep_sleep_start();
    }

    const esp_timer_create_args_t a = { .callback = azzera_serie, .name = "stabile" };
    esp_timer_handle_t t;
    if (esp_timer_create(&a, &t) == ESP_OK) {
        esp_timer_start_once(t, (uint64_t)STABILE_S * 1000 * 1000);
    }
}

void riavvio_registra(void)
{
    const esp_reset_reason_t r = esp_reset_reason();

    nvs_handle_t h;
    if (nvs_open(NVS_SPAZIO, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGW(TAG, "avvio per %s (storico non disponibile)", nome(r));
        controlla_serie(r);
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

    controlla_serie(r);
}
