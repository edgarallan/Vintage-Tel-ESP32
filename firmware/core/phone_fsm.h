/*
 * phone_fsm.h — la macchina a stati del telefono.
 *
 *   IDLE ──cornetta su──> DIALING ──numero──> CALLING ──ok──> IN_CALL
 *   IDLE ──chiamata in arrivo──> RINGING ──cornetta su──> IN_CALL
 *
 * Invariante portato dalla v1 e reso piu' forte: **tutte** le transizioni
 * passano da una sola funzione interna. Nella versione Python serviva un lock
 * asyncio per proteggere lo stato; qui la macchina vive in un unico task e
 * riceve solo eventi da una coda, quindi non c'e' concorrenza da proteggere
 * e il lock sparisce del tutto.
 *
 * Nessuna dipendenza da ESP-IDF: l'hardware arriva come hw_iface_t, il tempo
 * come campo dell'evento.
 */

#ifndef CORE_PHONE_FSM_H
#define CORE_PHONE_FSM_H

#include <stdbool.h>
#include <stdint.h>

#include "hw_iface.h"
#include "phonebook.h"
#include "ring_pattern.h"

#define PHONE_MAX_DIGITS 20

typedef enum {
    ST_IDLE,
    ST_DIALING,
    ST_CALLING,
    ST_RINGING,
    ST_IN_CALL,
    ST_ERROR,
} phone_state_t;

typedef enum {
    EV_HOOK_UP,        /* cornetta sollevata */
    EV_HOOK_DOWN,      /* cornetta riagganciata */
    EV_DIGIT,          /* cifra composta col disco */
    EV_BUTTON,         /* pulsante rubrica premuto */
    EV_INCOMING_CALL,  /* il cellulare segnala una chiamata in arrivo */
    EV_CALL_ENDED,     /* la chiamata e' finita dal lato remoto */
    EV_TICK,           /* battito periodico: fa scadere i timeout */
} phone_ev_type_t;

typedef struct {
    phone_ev_type_t type;
    uint32_t        now_ms;
    uint8_t         digit;                    /* EV_DIGIT */
    char            caller[PB_NUMBER_LEN];    /* EV_INCOMING_CALL */
} phone_ev_t;

typedef struct {
    uint32_t interdigit_ms;  /* attesa massima tra due cifre (IT: 8000) */
    uint32_t quickdial_ms;   /* pausa dopo una cifra sola prima del quick-dial (1500) */
    uint32_t busy_ms;        /* durata del tono di occupato dopo un fallimento (3000) */
} phone_config_t;

typedef struct {
    const hw_iface_t *hw;
    phonebook_t      *pb;
    phone_config_t    cfg;

    phone_state_t state;
    char          dialed[PHONE_MAX_DIGITS + 1];
    uint8_t       dialed_len;
    char          last_number[PHONE_MAX_DIGITS + 1];

    uint32_t last_digit_ms;
    uint32_t busy_started_ms;
    bool     busy_playing;

    ring_pattern_t ring;
} phone_t;

void phone_init(phone_t *p, const hw_iface_t *hw, phonebook_t *pb,
                const phone_config_t *cfg, const ring_config_t *ring_cfg);

void phone_handle(phone_t *p, const phone_ev_t *ev);

phone_state_t phone_state(const phone_t *p);
const char   *phone_dialed(const phone_t *p);

#endif /* CORE_PHONE_FSM_H */
