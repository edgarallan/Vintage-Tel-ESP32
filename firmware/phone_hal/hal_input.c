/*
 * hal_input.c — gancio, pulsante e disco combinatore.
 *
 * Tutto e' interrupt-driven: nessun ciclo che rilegge i pin. Gli ISR non
 * chiamano mai la logica di core/, si limitano a depositare un evento grezzo
 * su una coda interna. A svuotarla e' un piccolo task di ingresso, il cui
 * unico compito e' tradurre i segnali del disco nel vocabolario che la
 * macchina a stati capisce: EV_DIGIT. E' il confine di hal/ al lavoro.
 */

#include "phone_hal.h"
#include "hal_priv.h"

#include "driver/gpio.h"
#include "driver/pulse_cnt.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/task.h"

#include "dial_decode.h"

static const char *TAG = "hal_input";

/* Il disco: 10 impulsi valgono 0, convenzione italiana. Il fallback a tempo
   copre il caso in cui il contatto NSI non riapra. */
#define DIAL_ZERO_PULSES     10
#define DIAL_TIMEOUT_MS      1200

/* Fondo scala del contatore: una rotazione completa vale 10 impulsi, il
   margine serve solo a non saturare se il contatto rimbalza. */
#define PCNT_HIGH_LIMIT      100
#define PCNT_LOW_LIMIT       -1

/*
 * Il filtro anti-glitch del PCNT satura a 1023 cicli di APB_CLK
 * (PCNT_LL_MAX_GLITCH_WIDTH), cioe' ~12,8 us a 80 MHz. Basta e avanza contro
 * il rumore elettrico, ma NON e' un antirimbalzo meccanico: i contatti di un
 * disco d'epoca rimbalzano per millisecondi, tre ordini di grandezza in piu'.
 * Vedi la nota in hardware/pinout.md.
 */
#define PCNT_GLITCH_NS       12000

typedef enum {
    RAW_HOOK,
    RAW_BUTTON,
    RAW_NSI,
} raw_kind_t;

typedef struct {
    raw_kind_t kind;
    bool       level;    /* livello letto sul pin */
    uint32_t   now_ms;
} raw_ev_t;

static QueueHandle_t      s_evt_q;      /* verso la macchina a stati */
static QueueHandle_t      s_raw_q;      /* dagli ISR al task di ingresso */
static pcnt_unit_handle_t s_pcnt;
static dial_decoder_t     s_dial;

static void IRAM_ATTR isr_gpio(void *arg)
{
    const uint32_t pin = (uint32_t)(uintptr_t)arg;
    raw_ev_t ev = { .now_ms = (uint32_t)(esp_timer_get_time() / 1000) };

    switch (pin) {
    case PIN_HOOK:     ev.kind = RAW_HOOK;   break;
    case PIN_BUTTON:   ev.kind = RAW_BUTTON; break;
    case PIN_DIAL_NSI: ev.kind = RAW_NSI;    break;
    default:           return;
    }
    ev.level = gpio_get_level(pin);

    BaseType_t woken = pdFALSE;
    xQueueSendFromISR(s_raw_q, &ev, &woken);
    if (woken) {
        portYIELD_FROM_ISR();
    }
}

static void send_digit(uint8_t digit, uint32_t now_ms)
{
    phone_ev_t out = { .type = EV_DIGIT, .now_ms = now_ms, .digit = digit };
    xQueueSend(s_evt_q, &out, 0);
}

static void handle_nsi(const raw_ev_t *raw)
{
    /* Il contatto NSI e' chiuso mentre il disco ruota; con pull-up interno,
       chiuso significa livello basso. */
    const bool rotating = (raw->level == 0);

    if (rotating) {
        pcnt_unit_clear_count(s_pcnt);
        dial_on_nsi(&s_dial, true, NULL);
        return;
    }

    /* Rilascio: il conteggio accumulato dall'hardware diventa la cifra. */
    int count = 0;
    pcnt_unit_get_count(s_pcnt, &count);
    for (int i = 0; i < count; i++) {
        dial_on_pulse(&s_dial, raw->now_ms);
    }

    uint8_t digit = 0;
    if (dial_on_nsi(&s_dial, false, &digit)) {
        ESP_LOGI(TAG, "cifra %u (%d impulsi)", digit, count);
        send_digit(digit, raw->now_ms);
    }
}

static void input_task(void *arg)
{
    (void)arg;
    raw_ev_t raw;

    for (;;) {
        /* L'attesa ha un tetto perche' il decoder ha un fallback a tempo:
           se l'NSI non riapre, dial_tick() chiude comunque la cifra. */
        if (xQueueReceive(s_raw_q, &raw, pdMS_TO_TICKS(100)) == pdTRUE) {
            switch (raw.kind) {
            case RAW_HOOK: {
                /* Contatto verso massa: cornetta sollevata = pin alto. */
                phone_ev_t out = {
                    .type   = raw.level ? EV_HOOK_UP : EV_HOOK_DOWN,
                    .now_ms = raw.now_ms,
                };
                xQueueSend(s_evt_q, &out, 0);
                break;
            }
            case RAW_BUTTON:
                if (raw.level == 0) {
                    phone_ev_t out = { .type = EV_BUTTON, .now_ms = raw.now_ms };
                    xQueueSend(s_evt_q, &out, 0);
                }
                break;
            case RAW_NSI:
                handle_nsi(&raw);
                break;
            }
        }

        uint8_t digit = 0;
        if (dial_tick(&s_dial, hal_now_ms(), &digit)) {
            ESP_LOGW(TAG, "cifra %u chiusa dal fallback a tempo", digit);
            send_digit(digit, hal_now_ms());
        }
    }
}

static void pcnt_setup(void)
{
    pcnt_unit_config_t unit_cfg = {
        .high_limit = PCNT_HIGH_LIMIT,
        .low_limit  = PCNT_LOW_LIMIT,
    };
    ESP_ERROR_CHECK(pcnt_new_unit(&unit_cfg, &s_pcnt));

    pcnt_glitch_filter_config_t filter = { .max_glitch_ns = PCNT_GLITCH_NS };
    ESP_ERROR_CHECK(pcnt_unit_set_glitch_filter(s_pcnt, &filter));

    pcnt_chan_config_t chan_cfg = {
        .edge_gpio_num  = PIN_DIAL_PULSE,
        .level_gpio_num = -1,
    };
    pcnt_channel_handle_t chan = NULL;
    ESP_ERROR_CHECK(pcnt_new_channel(s_pcnt, &chan_cfg, &chan));

    /* Il contatto si apre e chiude una volta per impulso: contiamo un solo
       fronte, altrimenti ogni impulso varrebbe due. */
    ESP_ERROR_CHECK(pcnt_channel_set_edge_action(chan,
        PCNT_CHANNEL_EDGE_ACTION_INCREASE,
        PCNT_CHANNEL_EDGE_ACTION_HOLD));

    ESP_ERROR_CHECK(pcnt_unit_enable(s_pcnt));
    ESP_ERROR_CHECK(pcnt_unit_clear_count(s_pcnt));
    ESP_ERROR_CHECK(pcnt_unit_start(s_pcnt));
}

void hal_input_init(QueueHandle_t evt_q)
{
    s_evt_q = evt_q;
    s_raw_q = xQueueCreate(16, sizeof(raw_ev_t));
    configASSERT(s_raw_q);

    const dial_config_t dial_cfg = {
        .zero_pulses      = DIAL_ZERO_PULSES,
        .digit_timeout_ms = DIAL_TIMEOUT_MS,
    };
    dial_init(&s_dial, &dial_cfg);

    /* Contatti puliti verso massa: il pull-up interno sostituisce le
       resistenze esterne e gli optoaccoppiatori della v1 su Raspberry. */
    gpio_config_t in = {
        .pin_bit_mask = (1ULL << PIN_HOOK) | (1ULL << PIN_BUTTON) | (1ULL << PIN_DIAL_NSI),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .intr_type    = GPIO_INTR_ANYEDGE,
    };
    ESP_ERROR_CHECK(gpio_config(&in));

    pcnt_setup();

    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    ESP_ERROR_CHECK(gpio_isr_handler_add(PIN_HOOK,     isr_gpio, (void *)PIN_HOOK));
    ESP_ERROR_CHECK(gpio_isr_handler_add(PIN_BUTTON,   isr_gpio, (void *)PIN_BUTTON));
    ESP_ERROR_CHECK(gpio_isr_handler_add(PIN_DIAL_NSI, isr_gpio, (void *)PIN_DIAL_NSI));

    xTaskCreate(input_task, "input", 3072, NULL, 6, NULL);

    ESP_LOGI(TAG, "ingressi pronti: gancio=%d pulsante=%d disco=%d/NSI=%d",
             PIN_HOOK, PIN_BUTTON, PIN_DIAL_PULSE, PIN_DIAL_NSI);
}
