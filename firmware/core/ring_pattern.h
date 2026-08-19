/*
 * ring_pattern.h — cadenza dello squillo italiano.
 *
 * Il campanello elettromeccanico non suona in continuo: alterna 1 secondo di
 * squillo e 4 di pausa, secondo la cadenza Telecom italiana. Qui c'e' solo il
 * quando; il come (onda quadra a 22 Hz sull'H-bridge) sta in hal/bell_drv8871.c.
 */

#ifndef CORE_RING_PATTERN_H
#define CORE_RING_PATTERN_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t on_ms;      /* durata dello squillo (IT: 1000) */
    uint32_t off_ms;     /* pausa tra gli squilli (IT: 4000) */
    uint8_t  max_rings;  /* oltre i quali si smette; 0 = senza limite */
} ring_config_t;

typedef struct {
    ring_config_t cfg;
    bool     active;
    bool     bell_on;
    uint8_t  rings_done;
    uint32_t phase_start_ms;
} ring_pattern_t;

void ring_init(ring_pattern_t *r, const ring_config_t *cfg);

/* Comincia a squillare: la campana parte subito. */
void ring_start(ring_pattern_t *r, uint32_t now_ms);

/* Interrompe, in qualunque fase. Idempotente. */
void ring_stop(ring_pattern_t *r);

bool ring_is_active(const ring_pattern_t *r);
bool ring_bell_is_on(const ring_pattern_t *r);

/* Fa avanzare la cadenza. Ritorna true se la campana ha cambiato stato,
   scrivendo il nuovo stato in *bell_on: e' il momento in cui il chiamante
   deve accendere o spegnere l'H-bridge. */
bool ring_tick(ring_pattern_t *r, uint32_t now_ms, bool *bell_on);

#endif /* CORE_RING_PATTERN_H */
