/*
 * hal_input.c — gancio, pulsante e disco combinatore.
 *
 * Tutto e' interrupt-driven: nessun ciclo che rilegge i pin di continuo. Gli
 * ISR non chiamano mai la logica di core/ e non interpretano nulla: si
 * limitano a segnalare "su questo pin e' successo qualcosa, a quest'ora".
 *
 * ── Perche' l'ISR non legge piu' il livello ──────────────────────────────
 * La prima stesura leggeva gpio_get_level() dentro l'interruzione e passava
 * quel livello al task. Sull'apparecchio vero non ha funzionato, e la misura
 * del 26/08/2026 ha mostrato il perche': durante il rimbalzo arrivavano
 * fronti consecutivi che riportavano lo STESSO livello — otto "NSI 1" di
 * fila. Impossibile per fronti veri, e sintomo inequivocabile che nei
 * microsecondi fra l'interruzione e la lettura la linea era gia' cambiata di
 * nuovo. Il driver deduceva "disco in rotazione" da quel dato e sbagliava:
 * perdeva i rilasci dell'NSI e accumulava gli impulsi di piu' cifre.
 *
 * Ora il livello si legge nel task, DOPO che la linea si e' assestata, ed e'
 * l'unico livello di cui ci si fida.
 *
 * ── Perche' non c'e' piu' il PCNT ────────────────────────────────────────
 * Il peripheral contava anche i fronti di rimbalzo, e il suo filtro
 * anti-glitch non poteva salvarlo: satura a ~12,8 us mentre i fronti spuri
 * misurati distano fino a 797 us. Servendo comunque la marca temporale di
 * ogni fronte per l'antirimbalzo, il contatore hardware non aggiungeva nulla.
 */

#include "phone_hal.h"
#include "hal_priv.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/task.h"

#include "dial_decode.h"

static const char *TAG = "hal_input";

/* Il disco: 10 impulsi valgono 0, convenzione italiana. */
#define DIAL_ZERO_PULSES     10
#define DIAL_TIMEOUT_MS      1200

/*
 * Finestra cieca in core/dial_decode.c. Misurato: rimbalzo ~1,3 ms, impulsi
 * veri distanti ~100 ms. Otto millisecondi stanno sei volte sopra il primo e
 * dodici volte sotto i secondi.
 */
#define DIAL_MIN_GAP_MS      8

/*
 * Assestamento dopo un fronte, prima di fidarsi del livello. NON puo' essere
 * una costante unica: i contatti di questo telefono rimbalzano in modo
 * profondamente diverso, e il 27/08/2026 usarne una sola ha reso il gancio
 * inutilizzabile mentre il disco funzionava perfettamente.
 *
 *   disco impulsi   rimbalzo ~1,3 ms   lamella leggera, a strisciamento
 *   gancio          due fenomeni distinti, vedi sotto
 *
 * Il vincolo che limita ciascun valore verso l'alto e' diverso e va rispettato
 * singolarmente:
 *
 *   - il DISCO non puo' superare qualche ms: gli impulsi durano 61 ms e vanno
 *     contati tutti, quindi l'assestamento deve stare molto sotto
 *   - il GANCIO non ha alcun vincolo di velocita': sollevare o riappoggiare
 *     una cornetta e' un gesto umano lento
 *
 * IL GANCIO, MISURATO (04/09/2026, cinque cicli solleva/riappoggia).
 *
 * Il commutatore produce due cose diverse che e' facile confondere:
 *
 *   a) rimbalzo vero e proprio: raffiche di 3-10 fronti in meno di 1 ms
 *   b) contatto che "chiacchiera" durante la corsa meccanica: fino a 6 fronti
 *      distribuiti su 247 ms, con intervalli di 20, 19, 140, 21 e 47 ms
 *
 * Il caso (b) e' quello che conta, e il criterio NON e' la durata totale della
 * raffica. L'attesa qui e' ritriggerabile: ogni fronte fa ripartire il conto,
 * quindi per tenere insieme una raffica basta superarne l'INTERVALLO MASSIMO
 * fra due fronti, non la sua durata complessiva.
 *
 * Ecco perche' i 150 ms stimati a occhio fallivano in modo intermittente: il
 * peggior intervallo misurato e' 140 ms, appena sotto la soglia. Bastava una
 * cornetta sollevata un filo piu' adagio perche' la raffica si spezzasse in
 * due e il telefono vedesse due transizioni al posto di una.
 *
 * 400 ms stanno comodamente in mezzo ai due limiti reali: molto sopra i 140 ms
 * di chiacchiera, e molto sotto il tempo minimo fra due gesti umani distinti
 * (nessuno solleva e riappoggia in mezzo secondo), quindi non possono fondere
 * due gesti veri in uno.
 */
#define SETTLE_PULSE_US      3000
#define SETTLE_NSI_US        5000
#define SETTLE_HOOK_US     400000

/* Non misurato: il pulsante non e' ancora cablato. Stima prudente da rivedere
   con diag_input.c quando ci sara'. */
#define SETTLE_BUTTON_US    50000

/* Un fronte grezzo, senza interpretazione: solo dove e quando. */
typedef struct {
    uint8_t pin;
    int64_t us;
} raw_ev_t;

/* Stato dell'antirimbalzo per un pin. */
typedef struct {
    uint8_t pin;
    int64_t settle_us; /* quanto dev'essere ferma la linea per fidarsi */
    int     stable;    /* ultimo livello considerato buono */
    int64_t edge_us;   /* istante dell'ultimo fronte grezzo */
    bool    pending;   /* c'e' un fronte in attesa di assestarsi */
} debounce_t;

enum { DB_PULSE, DB_NSI, DB_HOOK, DB_BUTTON, DB_COUNT };

static QueueHandle_t  s_evt_q;
static QueueHandle_t  s_raw_q;
static debounce_t     s_db[DB_COUNT];
static dial_decoder_t s_dial;

static void IRAM_ATTR isr_gpio(void *arg)
{
    const raw_ev_t ev = {
        .pin = (uint8_t)(uintptr_t)arg,
        .us  = esp_timer_get_time(),
    };
    BaseType_t woken = pdFALSE;
    xQueueSendFromISR(s_raw_q, &ev, &woken);
    if (woken) {
        portYIELD_FROM_ISR();
    }
}

static void send_ev(phone_ev_type_t type, uint32_t now_ms, uint8_t digit)
{
    phone_ev_t out = { .type = type, .now_ms = now_ms, .digit = digit };
    xQueueSend(s_evt_q, &out, 0);
}

/* Un cambiamento di livello vero, gia' ripulito dal rimbalzo. */
static void on_stable_change(int idx, int level, uint32_t now_ms)
{
    switch (idx) {
    case DB_PULSE:
        /* A riposo il contatto e' chiuso (livello basso). Un impulso e' la
           sua apertura, quindi si conta la transizione verso l'alto: una per
           impulso, mentre contarle entrambe ne darebbe due. */
        if (level == 1) {
            dial_on_pulse(&s_dial, now_ms);
        }
        break;

    case DB_NSI: {
        /* Chiuso, cioe' basso, significa disco in rotazione. */
        const bool rotating = (level == 0);
        uint8_t digit = 0;
        if (dial_on_nsi(&s_dial, rotating, &digit) && !rotating) {
            ESP_LOGI(TAG, "cifra %u", digit);
            send_ev(EV_DIGIT, now_ms, digit);
        }
        break;
    }

    case DB_HOOK:
        /* Misurato sull'S62, coppia di lamelle piu' a sinistra del
           commutatore: il contatto e' CHIUSO con la cornetta sollevata e
           aperto con la cornetta appoggiata. Con il pull-up interno chiuso
           significa livello basso, quindi basso = sganciato.

           Confermato il 04/09/2026 leggendo il pin: a riposo con la cornetta
           appoggiata vale 1, e cinque cicli solleva/riappoggia alternano
           0,1,0,1... chiudendo su 1 con la cornetta giu'.

           Il verso non e' deducibile a priori e va misurato: invertirlo
           produce un telefono che risponde quando riagganci e riaggancia
           quando rispondi, cioe' un guasto perfettamente simmetrico che
           sembra un problema del Bluetooth e non lo e'. */
        send_ev(level ? EV_HOOK_DOWN : EV_HOOK_UP, now_ms, 0);
        break;

    case DB_BUTTON:
        if (level == 0) {
            send_ev(EV_BUTTON, now_ms, 0);
        }
        break;
    }
}

static void input_task(void *arg)
{
    (void)arg;
    raw_ev_t raw;

    for (;;) {
        bool qualcuno_in_attesa = false;
        for (int i = 0; i < DB_COUNT; i++) {
            qualcuno_in_attesa |= s_db[i].pending;
        }

        /* Attesa corta solo mentre un pin si sta assestando; altrimenti lunga,
           giusto per far girare il fallback a tempo del decodificatore. */
        const TickType_t attesa = qualcuno_in_attesa ? pdMS_TO_TICKS(1)
                                                     : pdMS_TO_TICKS(50);
        if (xQueueReceive(s_raw_q, &raw, attesa) == pdTRUE) {
            for (int i = 0; i < DB_COUNT; i++) {
                if (s_db[i].pin == raw.pin) {
                    /* Ogni nuovo fronte fa ripartire l'attesa: e' cio' che
                       rende la raffica di rimbalzi un evento solo. */
                    s_db[i].edge_us = raw.us;
                    s_db[i].pending = true;
                    break;
                }
            }
        }

        const int64_t ora_us = esp_timer_get_time();
        const uint32_t ora_ms = (uint32_t)(ora_us / 1000);

        for (int i = 0; i < DB_COUNT; i++) {
            if (!s_db[i].pending || (ora_us - s_db[i].edge_us) < s_db[i].settle_us) {
                continue;
            }
            s_db[i].pending = false;

            const int level = gpio_get_level(s_db[i].pin);
            if (level != s_db[i].stable) {
                s_db[i].stable = level;
                on_stable_change(i, level, ora_ms);
            }
            /* Se il livello e' tornato quello di prima, la raffica era solo
               rumore attorno a un fronte che non ha cambiato nulla: si tace. */
        }

        uint8_t digit = 0;
        if (dial_tick(&s_dial, ora_ms, &digit)) {
            ESP_LOGW(TAG, "cifra %u chiusa dal fallback a tempo", digit);
            send_ev(EV_DIGIT, ora_ms, digit);
        }
    }
}

void hal_input_init(QueueHandle_t evt_q)
{
    s_evt_q = evt_q;
    s_raw_q = xQueueCreate(64, sizeof(raw_ev_t));
    configASSERT(s_raw_q);

    const dial_config_t dial_cfg = {
        .zero_pulses      = DIAL_ZERO_PULSES,
        .digit_timeout_ms = DIAL_TIMEOUT_MS,
        .min_pulse_gap_ms = DIAL_MIN_GAP_MS,
    };
    dial_init(&s_dial, &dial_cfg);

    /* Contatti puliti verso massa: il pull-up interno sostituisce le
       resistenze esterne e gli optoaccoppiatori della v1 su Raspberry. */
    gpio_config_t in = {
        .pin_bit_mask = (1ULL << PIN_DIAL_PULSE) | (1ULL << PIN_DIAL_NSI) |
                        (1ULL << PIN_HOOK)       | (1ULL << PIN_BUTTON),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .intr_type    = GPIO_INTR_ANYEDGE,
    };
    ESP_ERROR_CHECK(gpio_config(&in));

    static const uint8_t pins[DB_COUNT] = {
        [DB_PULSE]  = PIN_DIAL_PULSE,
        [DB_NSI]    = PIN_DIAL_NSI,
        [DB_HOOK]   = PIN_HOOK,
        [DB_BUTTON] = PIN_BUTTON,
    };
    static const int64_t settle[DB_COUNT] = {
        [DB_PULSE]  = SETTLE_PULSE_US,
        [DB_NSI]    = SETTLE_NSI_US,
        [DB_HOOK]   = SETTLE_HOOK_US,
        [DB_BUTTON] = SETTLE_BUTTON_US,
    };
    for (int i = 0; i < DB_COUNT; i++) {
        s_db[i].pin       = pins[i];
        s_db[i].settle_us = settle[i];
        s_db[i].stable    = gpio_get_level(pins[i]);  /* stato di partenza reale */
        s_db[i].pending   = false;
    }

    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    for (int i = 0; i < DB_COUNT; i++) {
        ESP_ERROR_CHECK(gpio_isr_handler_add(pins[i], isr_gpio,
                                             (void *)(uintptr_t)pins[i]));
    }

    xTaskCreate(input_task, "input", 3072, NULL, 6, NULL);

    ESP_LOGI(TAG, "ingressi pronti: gancio=%d pulsante=%d disco=%d/NSI=%d",
             PIN_HOOK, PIN_BUTTON, PIN_DIAL_PULSE, PIN_DIAL_NSI);
    ESP_LOGI(TAG, "livelli a riposo: IMP=%d NSI=%d GANCIO=%d",
             s_db[DB_PULSE].stable, s_db[DB_NSI].stable, s_db[DB_HOOK].stable);

    /*
     * Se all'accensione la cornetta e' gia' sollevata, dirlo subito.
     *
     * Leggere il livello reale non bastava: la macchina a stati parte da IDLE
     * comunque, e senza un evento il telefono resterebbe convinto di essere a
     * riposo con la cornetta in mano — muto, senza tono di libero, e sordo al
     * disco — finche' qualcuno non la riappoggia. E' lo scenario di ogni
     * black-out con la cornetta appoggiata male.
     *
     * L'evento finisce in coda e verra' consumato dal task del telefono, che
     * parte dopo phone_init(): al momento in cui viene gestito la macchina a
     * stati esiste gia'. Il caso opposto non serve: cornetta giu' e' proprio
     * lo stato da cui la FSM parte.
     */
    if (s_db[DB_HOOK].stable == 0) {
        ESP_LOGI(TAG, "cornetta gia' sollevata all'avvio");
        send_ev(EV_HOOK_UP, (uint32_t)(esp_timer_get_time() / 1000), 0);
    }
}
