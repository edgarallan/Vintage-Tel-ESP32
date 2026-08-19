#include "ring_pattern.h"

#include <string.h>

void ring_init(ring_pattern_t *r, const ring_config_t *cfg)
{
    memset(r, 0, sizeof(*r));
    r->cfg = *cfg;
}

void ring_start(ring_pattern_t *r, uint32_t now_ms)
{
    /* Il conteggio riparte da zero: una nuova chiamata ha diritto a tutti
       i suoi squilli, indipendentemente da come e' finita la precedente. */
    r->active         = true;
    r->bell_on        = true;
    r->rings_done     = 0;
    r->phase_start_ms = now_ms;
}

void ring_stop(ring_pattern_t *r)
{
    /* Si risponde a meta' squillo: la campana tace all'istante, non finisce
       il secondo in corso. */
    r->active  = false;
    r->bell_on = false;
}

bool ring_is_active(const ring_pattern_t *r)  { return r->active; }
bool ring_bell_is_on(const ring_pattern_t *r) { return r->bell_on; }

bool ring_tick(ring_pattern_t *r, uint32_t now_ms, bool *bell_on)
{
    if (!r->active) {
        return false;
    }

    uint32_t phase_ms = r->bell_on ? r->cfg.on_ms : r->cfg.off_ms;
    if ((now_ms - r->phase_start_ms) < phase_ms) {
        return false;   /* fase ancora in corso */
    }

    if (r->bell_on) {
        /* Fine di uno squillo: si conta e si passa alla pausa. */
        r->rings_done++;
        if (r->cfg.max_rings != 0 && r->rings_done >= r->cfg.max_rings) {
            ring_stop(r);
            if (bell_on) {
                *bell_on = false;
            }
            return true;
        }
        r->bell_on = false;
    } else {
        r->bell_on = true;
    }

    r->phase_start_ms = now_ms;
    if (bell_on) {
        *bell_on = r->bell_on;
    }
    return true;
}
