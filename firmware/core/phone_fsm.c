#include "phone_fsm.h"

#include <stdio.h>
#include <string.h>

/* --- effetti sull'hardware, tolleranti ai puntatori nulli ----------------- */
/* Display e LED sono opzionali, e nei test se ne riempie solo una parte:
   la logica non deve sapere quali pezzi ci sono davvero. */

static void hw_led(phone_t *p, led_pattern_t pattern)
{
    if (p->hw && p->hw->set_led) {
        p->hw->set_led(pattern);
    }
}

static void hw_tone(phone_t *p, tone_t tone)
{
    if (p->hw && p->hw->play_tone) {
        p->hw->play_tone(tone);
    }
}

static void hw_bell(phone_t *p, bool on)
{
    if (!p->hw) {
        return;
    }
    if (on && p->hw->bell_start) {
        p->hw->bell_start();
    } else if (!on && p->hw->bell_stop) {
        p->hw->bell_stop();
    }
}

static bool hw_bt_connected(phone_t *p)
{
    return p->hw && p->hw->bt_is_connected && p->hw->bt_is_connected();
}

/* --- transizione: l'unico punto in cui lo stato cambia -------------------- */

static led_pattern_t led_for(phone_state_t s)
{
    switch (s) {
    case ST_IDLE:    return LED_IDLE;
    case ST_DIALING: return LED_DIALING;
    case ST_CALLING: return LED_CALLING;
    case ST_RINGING: return LED_RINGING;
    case ST_IN_CALL: return LED_IN_CALL;
    default:         return LED_ERROR;
    }
}

static const char *name_for(phone_state_t s)
{
    switch (s) {
    case ST_IDLE:    return "IDLE";
    case ST_DIALING: return "DIALING";
    case ST_CALLING: return "CALLING";
    case ST_RINGING: return "RINGING";
    case ST_IN_CALL: return "IN_CALL";
    default:         return "ERROR";
    }
}

static void transition(phone_t *p, phone_state_t next)
{
    p->state = next;
    hw_led(p, led_for(next));
    if (p->hw && p->hw->display_state) {
        p->hw->display_state(name_for(next), p->dialed);
    }
}

/* --- composizione --------------------------------------------------------- */

static void clear_dialed(phone_t *p)
{
    p->dialed[0]  = '\0';
    p->dialed_len = 0;
}

static void stop_busy(phone_t *p)
{
    if (p->busy_playing) {
        p->busy_playing = false;
        hw_tone(p, TONE_NONE);
    }
}

/* Fallimento della chiamata: si avvisa col tono che l'utente gia' conosce,
   invece di restare muti, e dopo busy_ms si torna a riposo. */
static void start_busy(phone_t *p, uint32_t now_ms)
{
    p->busy_playing    = true;
    p->busy_started_ms = now_ms;
    hw_tone(p, TONE_BUSY);
    transition(p, ST_IDLE);
}

static void place_call(phone_t *p, const char *number, uint32_t now_ms)
{
    transition(p, ST_CALLING);

    if (!hw_bt_connected(p)) {
        /* Senza cellulare accoppiato non si tenta nemmeno la chiamata. */
        clear_dialed(p);
        start_busy(p, now_ms);
        return;
    }

    bool ok = p->hw && p->hw->bt_place_call && p->hw->bt_place_call(number);
    if (!ok) {
        clear_dialed(p);
        start_busy(p, now_ms);
        return;
    }

    /* Solo un numero effettivamente chiamato alimenta il richiamo:
       cosi' il pulsante non ripete un tentativo mai partito. */
    snprintf(p->last_number, sizeof(p->last_number), "%s", number);
    clear_dialed(p);
    transition(p, ST_IN_CALL);
}

static void hangup(phone_t *p)
{
    if (p->hw && p->hw->bt_hangup) {
        p->hw->bt_hangup();
    }
    clear_dialed(p);
    transition(p, ST_IDLE);
}

static void stop_ringing(phone_t *p)
{
    ring_stop(&p->ring);
    hw_bell(p, false);
}

/* --- gestione dei singoli eventi ------------------------------------------ */

static void on_hook_up(phone_t *p, uint32_t now_ms)
{
    (void)now_ms;
    switch (p->state) {
    case ST_RINGING:
        stop_ringing(p);
        if (p->hw && p->hw->bt_answer) {
            p->hw->bt_answer();
        }
        transition(p, ST_IN_CALL);
        break;

    case ST_IDLE:
        stop_busy(p);
        clear_dialed(p);
        transition(p, ST_DIALING);
        hw_tone(p, TONE_DIAL);
        break;

    default:
        break;   /* rimbalzo del gancio: si ignora */
    }
}

static void on_hook_down(phone_t *p)
{
    switch (p->state) {
    case ST_IN_CALL:
    case ST_CALLING:
        hangup(p);
        break;

    case ST_DIALING:
        /* Il numero abbandonato non deve partire dopo, allo scadere
           del timeout tra cifre. */
        hw_tone(p, TONE_NONE);
        clear_dialed(p);
        transition(p, ST_IDLE);
        break;

    case ST_RINGING:
        stop_ringing(p);
        if (p->hw && p->hw->bt_reject) {
            p->hw->bt_reject();
        }
        transition(p, ST_IDLE);
        break;

    default:
        break;
    }
}

static void on_digit(phone_t *p, uint8_t digit, uint32_t now_ms)
{
    if (p->state == ST_IN_CALL) {
        /* Il disco continua a funzionare dentro la conversazione,
           per i menu vocali. */
        if (p->hw && p->hw->bt_send_dtmf) {
            p->hw->bt_send_dtmf((char)('0' + digit));
        }
        return;
    }

    if (p->state != ST_DIALING || digit > 9) {
        return;
    }

    if (p->dialed_len < PHONE_MAX_DIGITS) {
        p->dialed[p->dialed_len++] = (char)('0' + digit);
        p->dialed[p->dialed_len]   = '\0';
    }

    p->last_digit_ms = now_ms;
    hw_tone(p, TONE_KEYPRESS);

    if (p->hw && p->hw->display_state) {
        p->hw->display_state(name_for(p->state), p->dialed);
    }
}

static void on_button(phone_t *p, uint32_t now_ms)
{
    /* Richiamo dell'ultimo numero: una pressione sola, nessun menu. */
    if (p->state != ST_DIALING || p->last_number[0] == '\0') {
        return;
    }
    hw_tone(p, TONE_NONE);
    place_call(p, p->last_number, now_ms);
}

static void on_incoming(phone_t *p, const char *caller, uint32_t now_ms)
{
    if (p->state != ST_IDLE) {
        return;   /* cornetta gia' in mano: non si mette a squillare */
    }

    const char *name = p->pb ? pb_lookup_name(p->pb, caller) : NULL;
    if (p->hw && p->hw->display_incoming) {
        p->hw->display_incoming(name ? name : caller, caller);
    }

    transition(p, ST_RINGING);
    ring_start(&p->ring, now_ms);
    hw_bell(p, true);
}

static void on_call_ended(phone_t *p)
{
    if (p->state == ST_IDLE) {
        return;
    }
    stop_ringing(p);
    clear_dialed(p);
    transition(p, ST_IDLE);
}

static void on_tick(phone_t *p, uint32_t now_ms)
{
    /* Cadenza del campanello. */
    if (p->state == ST_RINGING) {
        bool bell = false;
        if (ring_tick(&p->ring, now_ms, &bell)) {
            hw_bell(p, bell);
        }
    }

    /* Fine del tono di occupato. */
    if (p->busy_playing && (now_ms - p->busy_started_ms) >= p->cfg.busy_ms) {
        stop_busy(p);
    }

    if (p->state != ST_DIALING || p->dialed_len == 0) {
        return;
    }

    uint32_t waited = now_ms - p->last_digit_ms;

    /* Una cifra sola assegnata a quick-dial: dopo una breve pausa parte.
       Se nel frattempo ne arriva un'altra, dialed_len cresce e questo
       ramo non si applica piu'. */
    if (p->dialed_len == 1 && p->pb) {
        const char *qd = pb_quick_dial(p->pb, (uint8_t)(p->dialed[0] - '0'));
        if (qd && waited >= p->cfg.quickdial_ms) {
            hw_tone(p, TONE_NONE);
            place_call(p, qd, now_ms);
            return;
        }
    }

    if (waited >= p->cfg.interdigit_ms) {
        char number[PHONE_MAX_DIGITS + 1];
        snprintf(number, sizeof(number), "%s", p->dialed);
        hw_tone(p, TONE_NONE);
        place_call(p, number, now_ms);
    }
}

/* --- API ------------------------------------------------------------------ */

void phone_init(phone_t *p, const hw_iface_t *hw, phonebook_t *pb,
                const phone_config_t *cfg, const ring_config_t *ring_cfg)
{
    memset(p, 0, sizeof(*p));
    p->hw    = hw;
    p->pb    = pb;
    p->cfg   = *cfg;
    p->state = ST_IDLE;
    ring_init(&p->ring, ring_cfg);
}

void phone_handle(phone_t *p, const phone_ev_t *ev)
{
    switch (ev->type) {
    case EV_HOOK_UP:       on_hook_up(p, ev->now_ms);              break;
    case EV_HOOK_DOWN:     on_hook_down(p);                        break;
    case EV_DIGIT:         on_digit(p, ev->digit, ev->now_ms);     break;
    case EV_BUTTON:        on_button(p, ev->now_ms);               break;
    case EV_INCOMING_CALL: on_incoming(p, ev->caller, ev->now_ms); break;
    case EV_CALL_ENDED:    on_call_ended(p);                       break;
    case EV_TICK:          on_tick(p, ev->now_ms);                 break;
    default:                                                       break;
    }
}

phone_state_t phone_state(const phone_t *p)  { return p->state; }
const char   *phone_dialed(const phone_t *p) { return p->dialed; }
