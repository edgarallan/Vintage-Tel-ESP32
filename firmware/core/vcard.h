/*
 * vcard.h — estrae nome e numeri dai vCard che arrivano via PBAP.
 *
 * Il server PBAP non consegna un file: consegna una SEQUENZA DI PEZZI, tagliati
 * dove capita. Un taglio cade regolarmente a meta' di una riga, e puo' cadere
 * anche in mezzo a una sequenza di escape. Il parser e' percio' a flusso: si
 * alimenta con i pezzi nell'ordine in cui arrivano e tiene lui lo stato fra una
 * chiamata e l'altra.
 *
 * Del contatto tiene SOLO IL NOME DI BATTESIMO, gia' ridotto all'ASCII che il
 * font del display sa disegnare: "Niccolò Rossi" diventa "Niccolo". Il font
 * 5x7 non ha lettere accentate, e un byte UTF-8 che non conosce diventa uno
 * spazio — meglio la lettera senza accento che un buco nel nome.
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

/* Le righe piu' lunghe di cosi' vengono scartate fino alla fine della riga
   logica. Un vCard con nome e numero ci sta largamente dentro; le righe
   chilometriche sono le fotografie in base64, che non ci interessano. */
#define VCARD_LINE_MAX 160

/* Numeri tenuti per contatto: cellulare, casa, ufficio e uno di scorta. Chi
   chiama da un quinto numero comparira' col numero invece che col nome. */
#define VCARD_MAX_TEL 4

/* Chiamata una volta PER OGNI NUMERO del contatto, alla sua chiusura, perche'
   la rubrica riconosce un chiamante dal numero: un contatto con cellulare e
   fisso diventa due voci con lo stesso nome. `nome` non e' mai vuoto: se il
   vCard non porta un nome usabile viene passato il numero stesso. */
typedef void (*vcard_cb_t)(const char *nome, const char *numero, void *ctx);

typedef struct {
    char   linea[VCARD_LINE_MAX];
    size_t len;
    bool   scarta;      /* riga troppo lunga: si ignora fino alla sua fine */
    bool   pendente;    /* riga finita, ma la prossima potrebbe continuarla */
    bool   dentro;      /* fra BEGIN:VCARD e END:VCARD */

    char    nome_n[PB_NAME_LEN];    /* dal campo N: il nome di battesimo */
    char    nome_fn[PB_NAME_LEN];   /* dal campo FN: ripiego per le aziende */
    char    numeri[VCARD_MAX_TEL][PB_NUMBER_LEN];
    uint8_t n_numeri;

    vcard_cb_t cb;
    void      *ctx;
} vcard_t;

void vcard_init(vcard_t *v, vcard_cb_t cb, void *ctx);

/* Alimenta il parser con un pezzo qualunque del flusso. */
void vcard_feed(vcard_t *v, const uint8_t *dati, size_t n);

/* Chiude il flusso: elabora l'ultima riga se non terminava con un capoverso. */
void vcard_end(vcard_t *v);

/* Riduce un testo UTF-8 all'ASCII stampabile: lettere accentate senza accento,
   apostrofo tipografico dritto, tutto il resto fuori dal font eliminato.
   `dst` e' sempre terminato. Esposta per i test. */
void vcard_ascii(char *dst, size_t size, const char *src);

#endif /* CORE_VCARD_H */
