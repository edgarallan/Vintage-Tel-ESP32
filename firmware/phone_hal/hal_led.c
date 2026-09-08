/*
 * hal_led.c — il LED di stato, un WS2812 su GPIO 27.
 *
 * Un pixel indirizzabile al posto di un LED RGB: un GPIO invece di tre, e
 * spariscono le tre resistenze di limitazione da saldare. E' la scelta che ha
 * fatto tornare i conti della mappa GPIO, dove i pin utilizzabili erano
 * esattamente quanti i segnali.
 *
 * Il colore da solo non basta a distinguere sei stati a colpo d'occhio, e
 * qualcuno li confonderebbe comunque: ogni stato ha percio' anche un MODO —
 * fisso, respiro, pulsazione, lampeggio — che si riconosce con la coda
 * dell'occhio da un'altra stanza, che e' l'unico posto da cui questo telefono
 * verra' davvero guardato.
 *
 * L'animazione gira su un timer, non sul task del telefono: la macchina a
 * stati dice solo "adesso sei cosi'" e non deve sapere che esiste un respiro.
 */

#include "hal_priv.h"

#include "driver/rmt_tx.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "hal_led";

/* Il WS2812 vuole impulsi a 0,3 e 0,9 us: con 10 MHz un tick e' 0,1 us, che
   li descrive entrambi con numeri interi e senza arrotondamenti. */
#define RMT_HZ            10000000
#define T0H_TICK          3   /* 0,3 us */
#define T0L_TICK          9   /* 0,9 us */
#define T1H_TICK          9
#define T1L_TICK          3

/* Quanto spesso si ridisegna. 25 ms sono 40 fotogrammi al secondo: il respiro
   e' liscio e il costo e' trascurabile. */
#define FRAME_MS          25

/* Luminosita' massima, su 255. Un WS2812 a piena potenza in una stanza al buio
   e' fastidioso, e per giunta si porta via 60 mA che sul modulo di
   alimentazione non avanzano. */
#define LUCE_MAX          64

typedef enum {
    FISSO,        /* luminosita' costante */
    RESPIRO,      /* sale e scende dolcemente: "acceso e tranquillo" */
    PULSAZIONE,   /* respiro veloce: "sta succedendo qualcosa" */
    LAMPEGGIO,    /* acceso/spento netto: "richiede attenzione adesso" */
} modo_t;

typedef struct {
    uint8_t     r, g, b;
    modo_t      modo;
    uint32_t    periodo_ms;
    const char *nome;
} aspetto_t;

/* I colori sono quelli gia' dichiarati in core/hw_iface.h: qui si aggiunge
   solo il modo, che l'occhio distingue meglio della tinta. */
static const aspetto_t s_aspetto[] = {
    [LED_IDLE]    = { 0,   0,   255, RESPIRO,    4000, "IDLE (blu, respiro)"       },
    [LED_DIALING] = { 255, 255, 255, FISSO,         0, "DIALING (bianco)"          },
    [LED_CALLING] = { 255, 170, 0,   PULSAZIONE,  900, "CALLING (giallo)"          },
    [LED_RINGING] = { 255, 0,   0,   LAMPEGGIO,   600, "RINGING (rosso lampeggio)" },
    [LED_IN_CALL] = { 0,   255, 0,   RESPIRO,    4000, "IN_CALL (verde, respiro)"  },
    [LED_ERROR]   = { 255, 0,   0,   FISSO,         0, "ERROR (rosso fisso)"       },
};

static rmt_channel_handle_t s_canale;
static rmt_encoder_handle_t s_encoder;
static esp_timer_handle_t   s_timer;

/* Scritto dal task del telefono, letto dal task del timer. Un enum a 32 bit su
   questa architettura si scrive in una istruzione sola, quindi il lettore vede
   il valore vecchio o quello nuovo e mai una via di mezzo: non serve un lock
   per un dato che nessuno modifica leggendolo. */
static volatile led_pattern_t s_pattern = LED_IDLE;
static volatile uint32_t      s_inizio_ms;

/* Onda triangolare 0..255 sul periodo dato. */
static uint8_t triangolo(uint32_t t_ms, uint32_t periodo_ms)
{
    if (periodo_ms == 0) {
        return 255;
    }
    const uint32_t fase = (t_ms % periodo_ms) * 512 / periodo_ms;  /* 0..511 */
    return (uint8_t)(fase < 256 ? fase : 511 - fase);
}

/* La percezione della luce non e' lineare: senza correzione un respiro
   triangolare sembra restare acceso e poi spegnersi di colpo. Elevare al
   quadrato avvicina la rampa a come la vede l'occhio. */
static uint8_t gamma_(uint8_t v)
{
    return (uint8_t)((uint16_t)v * v / 255);
}

static uint8_t luminosita(const aspetto_t *a, uint32_t t_ms)
{
    switch (a->modo) {
    case FISSO:
        return 255;
    case RESPIRO:
    case PULSAZIONE:
        /* Il respiro non arriva mai a zero: un LED che si spegne del tutto
           sembra un telefono spento, e questo telefono da spento non deve mai
           sembrarlo (vincolo #6). */
        return 40 + (uint8_t)((uint16_t)gamma_(triangolo(t_ms, a->periodo_ms)) * 215 / 255);
    case LAMPEGGIO:
        return (t_ms % a->periodo_ms) < (a->periodo_ms / 2) ? 255 : 0;
    }
    return 255;
}

static void disegna(void *arg)
{
    (void)arg;

    const led_pattern_t p = s_pattern;
    const aspetto_t    *a = &s_aspetto[p];
    const uint32_t   t_ms = (uint32_t)(esp_timer_get_time() / 1000) - s_inizio_ms;

    const uint16_t scala = (uint16_t)luminosita(a, t_ms) * LUCE_MAX / 255;

    /* Il WS2812 vuole i byte in ordine verde, rosso, blu. */
    const uint8_t px[3] = {
        (uint8_t)(a->g * scala / 255),
        (uint8_t)(a->r * scala / 255),
        (uint8_t)(a->b * scala / 255),
    };

    const rmt_transmit_config_t cfg = { .loop_count = 0 };
    rmt_transmit(s_canale, s_encoder, px, sizeof(px), &cfg);
}

void hal_led_init(void)
{
    rmt_tx_channel_config_t ch = {
        .gpio_num          = PIN_LED_WS2812,
        .clk_src           = RMT_CLK_SRC_DEFAULT,
        .resolution_hz     = RMT_HZ,
        .mem_block_symbols = 64,
        .trans_queue_depth = 4,
    };
    ESP_ERROR_CHECK(rmt_new_tx_channel(&ch, &s_canale));

    /* Un WS2812 non ha un clock: lo zero e l'uno si distinguono solo dalla
       durata dell'impulso alto. L'encoder a byte fa esattamente questo, ed e'
       il motivo per cui non serve scrivere un encoder su misura. */
    rmt_bytes_encoder_config_t enc = {
        .bit0 = { .level0 = 1, .duration0 = T0H_TICK,
                  .level1 = 0, .duration1 = T0L_TICK },
        .bit1 = { .level0 = 1, .duration0 = T1H_TICK,
                  .level1 = 0, .duration1 = T1L_TICK },
        .flags.msb_first = 1,
    };
    ESP_ERROR_CHECK(rmt_new_bytes_encoder(&enc, &s_encoder));
    ESP_ERROR_CHECK(rmt_enable(s_canale));

    s_inizio_ms = (uint32_t)(esp_timer_get_time() / 1000);

    const esp_timer_create_args_t targs = {
        .callback = disegna,
        .name     = "led",
    };
    ESP_ERROR_CHECK(esp_timer_create(&targs, &s_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(s_timer, FRAME_MS * 1000));

    /* Lo stato iniziale va detto: hal_led_set() scarta i cambi che non cambiano
       nulla, e all'accensione il valore e' gia' IDLE, quindi senza questa riga
       il log non confermerebbe mai che il LED e' stato impostato. */
    ESP_LOGI(TAG, "WS2812 su GPIO%d, LED -> %s", PIN_LED_WS2812,
             s_aspetto[s_pattern].nome);
}

void hal_led_set(led_pattern_t pattern)
{
    if (pattern == s_pattern) {
        return;
    }
    /* L'animazione riparte da capo a ogni cambio: un respiro che comincia a
       meta' corsa sembra un difetto. */
    s_inizio_ms = (uint32_t)(esp_timer_get_time() / 1000);
    s_pattern   = pattern;
    ESP_LOGI(TAG, "LED -> %s", s_aspetto[pattern].nome);
}
