#include "phonebook.h"

#include <string.h>

/* Estrae le sole cifre di un numero, tenendo le ultime `max` e scartando
   prefissi, spazi, trattini e il '+'. Ritorna quante ne ha estratte. */
static uint8_t digits_tail(const char *src, char *dst, uint8_t max)
{
    char all[PB_NUMBER_LEN * 2];
    uint8_t n = 0;

    for (const char *c = src; *c && n < sizeof(all) - 1; c++) {
        if (*c >= '0' && *c <= '9') {
            all[n++] = *c;
        }
    }
    all[n] = '\0';

    uint8_t keep   = (n > max) ? max : n;
    uint8_t offset = n - keep;
    memcpy(dst, all + offset, keep);
    dst[keep] = '\0';
    return keep;
}

/* Copia troncando: le stringhe arrivano dalla pagina web di configurazione,
   cioe' da fuori, e non devono poter traboccare il buffer. */
static void copy_bounded(char *dst, const char *src, size_t size)
{
    size_t n = strlen(src);
    if (n >= size) {
        n = size - 1;
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
}

void pb_init(phonebook_t *pb)
{
    memset(pb, 0, sizeof(*pb));
}

static bool same_number(const char *a, const char *b)
{
    char ta[PB_MATCH_DIGITS + 1];
    char tb[PB_MATCH_DIGITS + 1];

    uint8_t na = digits_tail(a, ta, PB_MATCH_DIGITS);
    uint8_t nb = digits_tail(b, tb, PB_MATCH_DIGITS);

    /* Nessuna cifra: e' un chiamante anonimo, non un numero. Non deve
       agganciare nulla, altrimenti "sconosciuto" diventerebbe un contatto. */
    if (na == 0 || nb == 0) {
        return false;
    }
    /* Confronto solo a parita' di lunghezza: cosi' "567" non aggancia un
       numero che finisce per 567, e i numeri brevi (112, 118) restano
       confrontati per intero. */
    if (na != nb) {
        return false;
    }
    return strcmp(ta, tb) == 0;
}

bool pb_add(phonebook_t *pb, const char *name, const char *number, int8_t quick_dial)
{
    if (pb->count >= PB_MAX_CONTACTS) {
        return false;
    }
    for (uint8_t i = 0; i < pb->count; i++) {
        if (same_number(pb->items[i].number, number)) {
            return false;   /* numero gia' presente */
        }
        if (quick_dial != PB_NO_QUICK_DIAL && pb->items[i].quick_dial == quick_dial) {
            return false;   /* cifra di quick-dial gia' assegnata */
        }
    }

    contact_t *c = &pb->items[pb->count];
    copy_bounded(c->name, name, sizeof(c->name));
    copy_bounded(c->number, number, sizeof(c->number));
    c->quick_dial = quick_dial;
    pb->count++;
    return true;
}

const char *pb_lookup_name(const phonebook_t *pb, const char *number)
{
    for (uint8_t i = 0; i < pb->count; i++) {
        if (same_number(pb->items[i].number, number)) {
            return pb->items[i].name;
        }
    }
    return NULL;
}

const char *pb_quick_dial(const phonebook_t *pb, uint8_t digit)
{
    for (uint8_t i = 0; i < pb->count; i++) {
        if (pb->items[i].quick_dial == (int8_t)digit) {
            return pb->items[i].number;
        }
    }
    return NULL;
}
