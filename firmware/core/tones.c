#include "tones.h"

#include <math.h>
#include <string.h>

/* Tabella seno a 256 punti, costruita una volta sola. Una tabella invece di
   sinf() a ogni campione: deterministica nei test e piu' economica sul chip,
   dove il generatore gira dentro il task audio a 16 kHz. */
#define SINE_TABLE_SIZE  256
#define PHASE_FRAC_BITS  16

static int16_t sine_table[SINE_TABLE_SIZE];
static bool    table_ready = false;

static void build_table(void)
{
    if (table_ready) {
        return;
    }
    for (int i = 0; i < SINE_TABLE_SIZE; i++) {
        double angle = 2.0 * M_PI * i / SINE_TABLE_SIZE;
        sine_table[i] = (int16_t)lround(sin(angle) * TONE_AMPLITUDE);
    }
    table_ready = true;
}

/* Incremento di fase per campione, in Q16 sulla tabella. */
static uint32_t phase_step(uint32_t freq_hz)
{
    return (uint32_t)(((uint64_t)freq_hz * SINE_TABLE_SIZE << PHASE_FRAC_BITS)
                      / TONE_SAMPLE_RATE);
}

static uint32_t ms_to_samples(uint32_t ms)
{
    return (uint32_t)((uint64_t)TONE_SAMPLE_RATE * ms / 1000);
}

void tone_init(tone_gen_t *t)
{
    build_table();
    memset(t, 0, sizeof(*t));
    t->current = TONE_NONE;
}

void tone_set(tone_gen_t *t, tone_t tone)
{
    /* Fase ed elapsed azzerati: il nuovo tono non deve ereditare la pausa
       di quello precedente, altrimenti passando da occupato a libero il
       telefono resterebbe muto per mezzo secondo. */
    t->current = tone;
    t->phase   = 0;
    t->elapsed = 0;
}

tone_t tone_current(const tone_gen_t *t)
{
    return t->current;
}

/* Dice se, al campione `elapsed`, il tono corrente deve suonare o tacere. */
static bool tone_is_audible(tone_gen_t *t)
{
    switch (t->current) {
    case TONE_DIAL:
        return true;   /* continuo */

    case TONE_BUSY: {
        uint32_t on     = ms_to_samples(TONE_BUSY_ON_MS);
        uint32_t period = on + ms_to_samples(TONE_BUSY_OFF_MS);
        return (t->elapsed % period) < on;
    }

    case TONE_KEYPRESS:
        if (t->elapsed >= ms_to_samples(TONE_KEYPRESS_MS)) {
            /* Colpo singolo: si spegne da solo, senza che nessuno lo fermi. */
            t->current = TONE_NONE;
            return false;
        }
        return true;

    case TONE_NONE:
    default:
        return false;
    }
}

static uint32_t tone_freq(tone_t tone)
{
    switch (tone) {
    case TONE_DIAL:     return TONE_DIAL_HZ;
    case TONE_BUSY:     return TONE_BUSY_HZ;
    case TONE_KEYPRESS: return TONE_KEYPRESS_HZ;
    default:            return 0;
    }
}

void tone_fill(tone_gen_t *t, int16_t *buf, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) {
        if (!tone_is_audible(t)) {
            buf[i] = 0;
            t->elapsed++;
            continue;
        }

        uint32_t idx = (t->phase >> PHASE_FRAC_BITS) % SINE_TABLE_SIZE;
        buf[i] = sine_table[idx];

        /* La fase avanza senza mai azzerarsi tra un buffer e l'altro:
           e' cio' che evita il clic al confine. */
        t->phase += phase_step(tone_freq(t->current));
        t->elapsed++;
    }
}
