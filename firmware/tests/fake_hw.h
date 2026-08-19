/*
 * fake_hw.h — hardware finto per i test della macchina a stati.
 *
 * Equivalente C di fakes.py della v1: registra tutto quello che la logica
 * chiede all'hardware, cosi' i test possono asserire sulle *azioni* invece
 * che sugli effetti fisici. Il tempo e' un contatore pilotato a mano, quindi
 * i test sono istantanei e deterministici invece che dipendenti dall'orologio.
 */

#ifndef TESTS_FAKE_HW_H
#define TESTS_FAKE_HW_H

#include <stdbool.h>
#include <stdint.h>

#include "hw_iface.h"

#define FAKE_MAX_ACTIONS 64
#define FAKE_ACTION_LEN  48

typedef struct {
    char     actions[FAKE_MAX_ACTIONS][FAKE_ACTION_LEN];
    uint8_t  action_count;

    led_pattern_t last_led;
    tone_t        last_tone;
    bool          bell_on;
    char          display_state[32];
    char          display_extra[32];
    char          display_name[32];

    /* Comportamento pilotabile dal test. */
    bool bt_connected;
    bool place_call_ok;

    uint32_t now_ms;
} fake_hw_t;

void fake_hw_init(fake_hw_t *f);

/* Restituisce l'interfaccia da passare alla logica. */
const hw_iface_t *fake_hw_iface(void);

/* Vero se l'azione esatta e' stata registrata, es. "place_call:123". */
bool fake_did(const fake_hw_t *f, const char *action);

/* Quante volte compare l'azione. */
uint8_t fake_count(const fake_hw_t *f, const char *action);

extern fake_hw_t g_fake;

#endif /* TESTS_FAKE_HW_H */
