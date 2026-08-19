/*
 * tones.h — toni di sistema del telefono italiano.
 *
 * Riferimento: ITU-T E.180 + standard Telecom italiano.
 *   - tono di libero (dial):  425 Hz continuo
 *   - tono di occupato (busy): 425 Hz, 500 ms on / 500 ms off
 *   - conferma di cifra:       800 Hz, 50 ms
 *
 * Genera PCM mono a 16 kHz, lo stesso rate della banda larga HFP: cosi' i toni
 * si mescolano al flusso della chiamata senza riconversioni.
 */

#ifndef CORE_TONES_H
#define CORE_TONES_H

#include <stdbool.h>
#include <stdint.h>

#include "hw_iface.h"   /* tone_t */

#define TONE_SAMPLE_RATE 16000
#define TONE_AMPLITUDE   9830    /* ~0.3 fondo scala: il volume lo fa il codec */

#define TONE_DIAL_HZ      425
#define TONE_BUSY_HZ      425
#define TONE_KEYPRESS_HZ  800

#define TONE_BUSY_ON_MS    500
#define TONE_BUSY_OFF_MS   500
#define TONE_KEYPRESS_MS    50

typedef struct {
    tone_t   current;
    uint32_t phase;          /* accumulatore di fase, Q16 sulla tabella seno */
    uint32_t elapsed;        /* campioni emessi da quando il tono e' iniziato */
} tone_gen_t;

void   tone_init(tone_gen_t *t);
void   tone_set(tone_gen_t *t, tone_t tone);
tone_t tone_current(const tone_gen_t *t);

/* Riempie `n` campioni. Silenzio dove il tono e' in pausa o assente.
   La fase non si azzera tra una chiamata e l'altra: e' cio' che evita
   il clic udibile al confine tra due buffer. */
void tone_fill(tone_gen_t *t, int16_t *buf, uint32_t n);

#endif /* CORE_TONES_H */
