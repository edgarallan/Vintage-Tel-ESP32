/*
 * vcard.h — estrae nome e numero dai vCard che arrivano via PBAP.
 *
 * Il server PBAP non consegna un file: consegna una SEQUENZA DI PEZZI, tagliati
 * dove capita. Un taglio cade regolarmente a meta' di una riga, e puo' cadere
 * anche in mezzo a una sequenza di escape. Il parser e' percio' a flusso: si
 * alimenta con i pezzi nell'ordine in cui arrivano e tiene lui lo stato fra una
 * chiamata e l'altra.
 *
 * Sta in core/ perche' e' la parte che sbaglia piu' facilmente ed e' l'unica
 * provabile sul PC: il Bluetooth qui non c'entra niente.
 */

#ifndef CORE_VCARD_H
#define CORE_VCARD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "phonebook.h"

/* Le righe piu' lunghe di cosi' vengono scartate fino al ritorno a capo.
   Un vCard con nome e numero ci sta largamente dentro; le righe chilometriche
   sono le fotografie in base64, che non ci interessano. */
#define VCARD_LINE_MAX 160

/* Chiamata a ogni contatto completo. `nome` non e' mai NULL ne' vuoto: se il
   vCard non porta un nome usabile viene passato il numero stesso. */
typedef void (*vcard_cb_t)(const char *nome, const char *numero, void *ctx);

typedef struct {
    char   linea[VCARD_LINE_MAX];
    size_t len;
    bool   scarta;      /* riga troppo lunga: si ignora fino al capoverso */
    bool   dentro;      /* fra BEGIN:VCARD e END:VCARD */

    char nome[PB_NAME_LEN];
    char numero[PB_NUMBER_LEN];

    vcard_cb_t cb;
    void      *ctx;
} vcard_t;

void vcard_init(vcard_t *v, vcard_cb_t cb, void *ctx);

/* Alimenta il parser con un pezzo qualunque del flusso. */
void vcard_feed(vcard_t *v, const uint8_t *dati, size_t n);

/* Chiude il flusso: emette l'ultima riga se non terminava con un capoverso. */
void vcard_end(vcard_t *v);

#endif /* CORE_VCARD_H */
