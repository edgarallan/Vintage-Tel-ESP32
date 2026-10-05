#include "vcard.h"

#include <ctype.h>
#include <string.h>

/* U+00C0..U+00FF, cioe' i due byte UTF-8 C3 80..C3 BF, senza accento.
   × e ÷ diventano x e /, gli unici due simboli del blocco. */
static const char LATIN1_ASCII[] =
    "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTs"
    "aaaaaaaceeeeiiiidnooooo/ouuuuyty";

static void aggiungi(char *dst, size_t size, size_t *n, char c)
{
    if (*n + 1 >= size) {
        return;
    }
    /* Niente spazi in testa ne' doppi: un'emoji tolta fra due parole non deve
       lasciare un buco largo il doppio. */
    if (c == ' ' && (*n == 0 || dst[*n - 1] == ' ')) {
        return;
    }
    dst[(*n)++] = c;
}

/* Lunghezza di una sequenza UTF-8 dal suo primo byte. Un byte di
   continuazione spaiato conta come uno, cosi' non si resta mai fermi. */
static size_t lunghezza_utf8(uint8_t b)
{
    if (b >= 0xF0) return 4;
    if (b >= 0xE0) return 3;
    if (b >= 0xC0) return 2;
    return 1;
}

void vcard_ascii(char *dst, size_t size, const char *src)
{
    const uint8_t *s = (const uint8_t *)src;
    size_t n = 0;

    while (*s) {
        if (*s < 0x80) {
            if (*s >= 0x20 && *s < 0x7F) {
                aggiungi(dst, size, &n, (char)*s);
            }
            s++;
        } else if (s[0] == 0xC3 && s[1] >= 0x80 && s[1] <= 0xBF) {
            aggiungi(dst, size, &n, LATIN1_ASCII[s[1] - 0x80]);
            s += 2;
        } else if (s[0] == 0xE2 && s[1] == 0x80 && (s[2] == 0x98 || s[2] == 0x99)) {
            aggiungi(dst, size, &n, '\'');   /* ‘ ’ */
            s += 3;
        } else {
            /* Fuori dal font: si salta l'intera sequenza, senza mai
               scavalcare il terminatore. */
            for (size_t k = lunghezza_utf8(*s); k > 0 && *s; k--) {
                s++;
            }
        }
    }
    while (n > 0 && dst[n - 1] == ' ') {
        n--;
    }
    dst[n] = '\0';
}

/* Toglie gli escape del vCard 3.0: \, \; \\ e \n (un a-capo in un nome
   diventa uno spazio). */
static void senza_escape(char *dst, size_t size, const char *src, size_t len)
{
    size_t n = 0;
    for (size_t i = 0; i < len && n + 1 < size; i++) {
        char c = src[i];
        if (c == '\\' && i + 1 < len) {
            c = src[++i];
            if (c == 'n' || c == 'N') {
                c = ' ';
            }
        }
        dst[n++] = c;
    }
    dst[n] = '\0';
}

/* Il campo `indice` di un valore separato da ';', rispettando i "\;". */
static void campo(char *dst, size_t size, const char *val, int indice)
{
    const char *inizio = val;
    int i = 0;
    for (const char *c = val;; c++) {
        if (*c == '\\' && c[1]) {
            c++;
            continue;
        }
        if (*c == ';' || *c == '\0') {
            if (i == indice) {
                senza_escape(dst, size, inizio, (size_t)(c - inizio));
                return;
            }
            if (*c == '\0') {
                break;
            }
            i++;
            inizio = c + 1;
        }
    }
    dst[0] = '\0';
}

static int esadecimale(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    c = (char)toupper((unsigned char)c);
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Decodifica il quoted-printable sul posto: "=C3=B2" torna "ò". */
static void da_quoted_printable(char *s)
{
    char *w = s;
    for (const char *r = s; *r; r++) {
        int hi, lo;
        if (*r == '=' && (hi = esadecimale(r[1])) >= 0 && (lo = esadecimale(r[2])) >= 0) {
            *w++ = (char)(hi * 16 + lo);
            r += 2;
        } else {
            *w++ = *r;
        }
    }
    *w = '\0';
}

static bool uguale(const char *a, const char *b, size_t n)
{
    if (strlen(b) != n) {
        return false;
    }
    for (size_t i = 0; i < n; i++) {
        if (toupper((unsigned char)a[i]) != toupper((unsigned char)b[i])) {
            return false;
        }
    }
    return true;
}

/* Cerca `ago` nei primi `n` caratteri, senza badare alle maiuscole. */
static bool contiene(const char *s, size_t n, const char *ago)
{
    const size_t m = strlen(ago);
    for (size_t i = 0; i + m <= n; i++) {
        if (uguale(s + i, ago, m)) {
            return true;
        }
    }
    return false;
}

static size_t fine_proprieta(const char *linea)
{
    const char *due_punti = strchr(linea, ':');
    return due_punti ? (size_t)(due_punti - linea) : strlen(linea);
}

static bool e_quoted_printable(const char *linea)
{
    return contiene(linea, fine_proprieta(linea), "QUOTED-PRINTABLE");
}

static void azzera_contatto(vcard_t *v)
{
    v->nome_n[0]  = '\0';
    v->nome_fn[0] = '\0';
    v->n_numeri   = 0;
}

static void emetti(vcard_t *v)
{
    for (uint8_t i = 0; i < v->n_numeri; i++) {
        const char *nome = v->nome_n[0]  ? v->nome_n
                         : v->nome_fn[0] ? v->nome_fn
                         : v->numeri[i];
        v->cb(nome, v->numeri[i], v->ctx);
    }
}

static void elabora_riga(vcard_t *v)
{
    v->linea[v->len] = '\0';
    char *linea = v->linea;

    char *due_punti = strchr(linea, ':');
    if (!due_punti) {
        return;
    }
    *due_punti = '\0';
    char *valore = due_punti + 1;

    /* Il nome della proprieta' sta prima dei parametri, e dopo l'eventuale
       gruppo: "item1.TEL;TYPE=CELL" e' un TEL. */
    size_t len_nome = strcspn(linea, ";");
    const char *nome = linea;
    for (size_t i = 0; i < len_nome; i++) {
        if (linea[i] == '.') {
            nome = linea + i + 1;
        }
    }
    len_nome -= (size_t)(nome - linea);

    if (contiene(linea, strlen(linea), "QUOTED-PRINTABLE")) {
        da_quoted_printable(valore);
    }

    if (uguale(nome, "BEGIN", len_nome)) {
        azzera_contatto(v);
        v->dentro = true;
        return;
    }
    if (uguale(nome, "END", len_nome)) {
        if (v->dentro) {
            emetti(v);
        }
        v->dentro = false;
        return;
    }
    if (!v->dentro) {
        return;
    }

    char grezzo[VCARD_LINE_MAX];
    if (uguale(nome, "N", len_nome)) {
        campo(grezzo, sizeof(grezzo), valore, 1);
        vcard_ascii(v->nome_n, sizeof(v->nome_n), grezzo);
    } else if (uguale(nome, "FN", len_nome)) {
        senza_escape(grezzo, sizeof(grezzo), valore, strlen(valore));
        vcard_ascii(v->nome_fn, sizeof(v->nome_fn), grezzo);
    } else if (uguale(nome, "TEL", len_nome)) {
        if (v->n_numeri < VCARD_MAX_TEL && valore[0]) {
            char *dst = v->numeri[v->n_numeri++];
            senza_escape(dst, PB_NUMBER_LEN, valore, strlen(valore));
        }
    }
}

static void chiudi_riga(vcard_t *v)
{
    if (!v->scarta && v->len > 0) {
        elabora_riga(v);
    }
    v->len      = 0;
    v->scarta   = false;
    v->pendente = false;
}

void vcard_init(vcard_t *v, vcard_cb_t cb, void *ctx)
{
    memset(v, 0, sizeof(*v));
    v->cb  = cb;
    v->ctx = ctx;
}

void vcard_feed(vcard_t *v, const uint8_t *dati, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        const char c = (char)dati[i];

        /* Una riga finita si elabora solo quando si vede l'inizio della
           successiva: se comincia con uno spazio e' la stessa riga ripiegata. */
        if (v->pendente) {
            if (c == ' ' || c == '\t') {
                v->pendente = false;
                continue;
            }
            chiudi_riga(v);
        }

        if (c == '\r') {
            continue;
        }
        if (c == '\n') {
            /* A-capo morbido del quoted-printable: un '=' in fondo alla riga
               dice che il valore continua sulla prossima, senza spazio. */
            if (!v->scarta && v->len > 0 && v->linea[v->len - 1] == '=') {
                v->linea[v->len] = '\0';
                if (e_quoted_printable(v->linea)) {
                    v->len--;
                    continue;
                }
            }
            v->pendente = true;
            continue;
        }
        if (v->scarta) {
            continue;
        }
        if (v->len >= VCARD_LINE_MAX - 1) {
            v->scarta = true;
            continue;
        }
        v->linea[v->len++] = c;
    }
}

void vcard_end(vcard_t *v)
{
    chiudi_riga(v);
}
