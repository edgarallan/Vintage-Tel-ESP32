/*
 * dial_decode.h — da impulsi del disco a cifra composta.
 *
 * Il disco ha due contatti: gli **impulsi** (uno per scatto di ritorno) e
 * l'**NSI**, il contatto di fuori-normale che resta chiuso per tutta la
 * rotazione. Gli impulsi si contano solo mentre l'NSI dice che il disco gira:
 * senza quella condizione le vibrazioni meccaniche produrrebbero cifre fantasma.
 *
 * Convenzione italiana/europea: 10 impulsi valgono 0.
 *
 * Nessuna dipendenza dall'hardware: il tempo arriva dal chiamante, e questo
 * rende i test deterministici invece che dipendenti dall'orologio.
 */

#ifndef CORE_DIAL_DECODE_H
#define CORE_DIAL_DECODE_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint8_t  zero_pulses;      /* quanti impulsi valgono 0 (IT: 10) */
    uint32_t digit_timeout_ms; /* fallback se il rilascio NSI si perde; 0 = disattivo */
} dial_config_t;

typedef struct {
    dial_config_t cfg;
    bool     rotating;       /* NSI attivo: il disco sta tornando indietro */
    bool     emitted;        /* cifra gia' emessa per questa rotazione */
    uint8_t  pulses;
    uint32_t last_pulse_ms;
} dial_decoder_t;

void dial_init(dial_decoder_t *d, const dial_config_t *cfg);

/* Fronte dell'NSI. Sul rilascio puo' completare una cifra:
   ritorna true e scrive *digit_out. */
bool dial_on_nsi(dial_decoder_t *d, bool active, uint8_t *digit_out);

/* Un impulso dal disco. Ignorato se il disco non sta ruotando. */
void dial_on_pulse(dial_decoder_t *d, uint32_t now_ms);

/* Da chiamare periodicamente: gestisce il fallback a tempo.
   Ritorna true e scrive *digit_out se la cifra e' stata completata cosi'. */
bool dial_tick(dial_decoder_t *d, uint32_t now_ms, uint8_t *digit_out);

#endif /* CORE_DIAL_DECODE_H */
