#include "dial_decode.h"

#include <string.h>

/* Traduce il conteggio impulsi in cifra.
   Oltre zero_pulses si resta su 0: un rimbalzo meccanico non deve
   produrre una cifra inventata. */
static uint8_t pulses_to_digit(const dial_decoder_t *d)
{
    return (d->pulses >= d->cfg.zero_pulses) ? 0 : d->pulses;
}

/* Chiude la rotazione corrente emettendo la cifra, se ce n'e' una.
   Zero impulsi significa disco mosso ma non composto: non e' una cifra. */
static bool finalize(dial_decoder_t *d, uint8_t *digit_out)
{
    if (d->emitted || d->pulses == 0) {
        return false;
    }
    d->emitted = true;
    if (digit_out) {
        *digit_out = pulses_to_digit(d);
    }
    return true;
}

void dial_init(dial_decoder_t *d, const dial_config_t *cfg)
{
    memset(d, 0, sizeof(*d));
    d->cfg = *cfg;
    /* Un zero_pulses a 0 renderebbe ogni cifra uno 0: e' quasi certamente
       una configurazione dimenticata, meglio il default italiano. */
    if (d->cfg.zero_pulses == 0) {
        d->cfg.zero_pulses = 10;
    }
}

bool dial_on_nsi(dial_decoder_t *d, bool active, uint8_t *digit_out)
{
    if (active) {
        /* Il disco comincia a tornare: nuova rotazione, conteggio azzerato. */
        d->rotating = true;
        d->emitted  = false;
        d->pulses   = 0;
        return false;
    }

    if (!d->rotating) {
        return false;   /* rilascio senza rotazione: rumore, si ignora */
    }

    bool got = finalize(d, digit_out);
    d->rotating = false;
    return got;
}

void dial_on_pulse(dial_decoder_t *d, uint32_t now_ms)
{
    /* Fuori rotazione gli impulsi sono vibrazioni: e' esattamente il motivo
       per cui il disco ha un contatto NSI oltre a quello degli impulsi. */
    if (!d->rotating || d->emitted) {
        return;
    }
    if (d->pulses < UINT8_MAX) {
        d->pulses++;
    }
    d->last_pulse_ms = now_ms;
}

bool dial_tick(dial_decoder_t *d, uint32_t now_ms, uint8_t *digit_out)
{
    /* Rete di sicurezza: se il contatto NSI non riapre (ossido, molla stanca)
       la cifra esce lo stesso dopo una pausa negli impulsi, invece di
       bloccare il telefono. */
    if (d->cfg.digit_timeout_ms == 0 || !d->rotating || d->emitted || d->pulses == 0) {
        return false;
    }
    if ((now_ms - d->last_pulse_ms) < d->cfg.digit_timeout_ms) {
        return false;
    }
    return finalize(d, digit_out);
}
