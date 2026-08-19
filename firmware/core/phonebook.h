/*
 * phonebook.h — rubrica in memoria.
 *
 * Serve a due cose: dare un nome al numero che chiama, e associare una singola
 * cifra del disco a un numero (quick-dial). Su un telefono a disco il quick-dial
 * non e' una comodita' accessoria: e' l'unica interfaccia rapida che esiste.
 *
 * La persistenza sta in hal/nvs_store.c: qui c'e' solo la logica, testabile su PC.
 */

#ifndef CORE_PHONEBOOK_H
#define CORE_PHONEBOOK_H

#include <stdbool.h>
#include <stdint.h>

#define PB_MAX_CONTACTS 32
#define PB_NAME_LEN     24
#define PB_NUMBER_LEN   24

/* Quante cifre finali si confrontano per riconoscere un numero.
   Il caller-id arriva in formati imprevedibili (+39..., 0039..., senza prefisso):
   confrontare la coda e' l'unico modo robusto. Nove cifre = il numero nazionale
   italiano completo, quindi niente falsi positivi. */
#define PB_MATCH_DIGITS 9

#define PB_NO_QUICK_DIAL (-1)

typedef struct {
    char   name[PB_NAME_LEN];
    char   number[PB_NUMBER_LEN];
    int8_t quick_dial;    /* 0-9, oppure PB_NO_QUICK_DIAL */
} contact_t;

typedef struct {
    contact_t items[PB_MAX_CONTACTS];
    uint8_t   count;
} phonebook_t;

void pb_init(phonebook_t *pb);

/* Aggiunge un contatto. Rifiuta i duplicati di numero e la rubrica piena. */
bool pb_add(phonebook_t *pb, const char *name, const char *number, int8_t quick_dial);

/* Nome associato a un numero, confrontando le ultime PB_MATCH_DIGITS cifre.
   NULL se sconosciuto. */
const char *pb_lookup_name(const phonebook_t *pb, const char *number);

/* Numero associato a una cifra di quick-dial. NULL se non assegnata. */
const char *pb_quick_dial(const phonebook_t *pb, uint8_t digit);

#endif /* CORE_PHONEBOOK_H */
