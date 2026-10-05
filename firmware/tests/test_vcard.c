/*
 * Test del parser vCard. I campioni ricalcano cio' che manda il PBAP di
 * Android: vCard 3.0, CRLF, campi raggruppati e qualche 2.1 col
 * quoted-printable da apparecchi piu' vecchi.
 */

#include "unity.h"
#include "vcard.h"

#include <stdio.h>
#include <string.h>

#define MAX_VOCI 16

static struct {
    char nome[PB_NAME_LEN];
    char numero[PB_NUMBER_LEN];
} voci[MAX_VOCI];
static int n_voci;

static vcard_t v;

static void raccogli(const char *nome, const char *numero, void *ctx)
{
    (void)ctx;
    TEST_ASSERT_TRUE_MESSAGE(n_voci < MAX_VOCI, "troppe voci");
    snprintf(voci[n_voci].nome, sizeof(voci[n_voci].nome), "%s", nome);
    snprintf(voci[n_voci].numero, sizeof(voci[n_voci].numero), "%s", numero);
    n_voci++;
}

void setUp(void)
{
    memset(voci, 0, sizeof(voci));
    n_voci = 0;
    vcard_init(&v, raccogli, NULL);
}

void tearDown(void) { }

static void tutto(const char *s)
{
    vcard_feed(&v, (const uint8_t *)s, strlen(s));
    vcard_end(&v);
}

/* Lo stesso testo, un byte alla volta: il caso peggiore di frammentazione. */
static void a_briciole(const char *s)
{
    for (size_t i = 0; s[i]; i++) {
        vcard_feed(&v, (const uint8_t *)&s[i], 1);
    }
    vcard_end(&v);
}

#define MARIO \
    "BEGIN:VCARD\r\n" \
    "VERSION:3.0\r\n" \
    "FN:Mario Rossi\r\n" \
    "N:Rossi;Mario;;;\r\n" \
    "TEL;TYPE=CELL:+39 333 1234567\r\n" \
    "END:VCARD\r\n"

void test_tiene_solo_il_nome_di_battesimo(void)
{
    tutto(MARIO);
    TEST_ASSERT_EQUAL(1, n_voci);
    TEST_ASSERT_EQUAL_STRING("Mario", voci[0].nome);
    TEST_ASSERT_EQUAL_STRING("+39 333 1234567", voci[0].numero);
}

void test_un_byte_alla_volta_da_lo_stesso_risultato(void)
{
    a_briciole(MARIO);
    TEST_ASSERT_EQUAL(1, n_voci);
    TEST_ASSERT_EQUAL_STRING("Mario", voci[0].nome);
    TEST_ASSERT_EQUAL_STRING("+39 333 1234567", voci[0].numero);
}

void test_una_voce_per_ogni_numero(void)
{
    tutto("BEGIN:VCARD\r\nVERSION:3.0\r\nN:Bianchi;Anna;;;\r\n"
          "TEL;TYPE=CELL:3331111111\r\nTEL;TYPE=HOME:011222222\r\n"
          "END:VCARD\r\n");
    TEST_ASSERT_EQUAL(2, n_voci);
    TEST_ASSERT_EQUAL_STRING("Anna", voci[0].nome);
    TEST_ASSERT_EQUAL_STRING("3331111111", voci[0].numero);
    TEST_ASSERT_EQUAL_STRING("Anna", voci[1].nome);
    TEST_ASSERT_EQUAL_STRING("011222222", voci[1].numero);
}

void test_piu_contatti_di_fila(void)
{
    tutto(MARIO
          "BEGIN:VCARD\r\nVERSION:3.0\r\nN:Verdi;Luca;;;\r\n"
          "TEL:3332222222\r\nEND:VCARD\r\n");
    TEST_ASSERT_EQUAL(2, n_voci);
    TEST_ASSERT_EQUAL_STRING("Mario", voci[0].nome);
    TEST_ASSERT_EQUAL_STRING("Luca", voci[1].nome);
}

void test_contatto_senza_numero_non_produce_voci(void)
{
    tutto("BEGIN:VCARD\r\nVERSION:3.0\r\nN:Neri;Paolo;;;\r\nEND:VCARD\r\n");
    TEST_ASSERT_EQUAL(0, n_voci);
}

void test_senza_nome_di_battesimo_usa_il_nome_completo(void)
{
    /* Le aziende hanno solo FN, o il nome tutto nel campo cognome. */
    tutto("BEGIN:VCARD\r\nVERSION:3.0\r\nFN:Idraulico Ferri\r\n"
          "N:Idraulico Ferri;;;;\r\nTEL:0113333333\r\nEND:VCARD\r\n");
    TEST_ASSERT_EQUAL(1, n_voci);
    TEST_ASSERT_EQUAL_STRING("Idraulico Ferri", voci[0].nome);
}

void test_senza_alcun_nome_usa_il_numero(void)
{
    tutto("BEGIN:VCARD\r\nVERSION:3.0\r\nTEL:3334444444\r\nEND:VCARD\r\n");
    TEST_ASSERT_EQUAL(1, n_voci);
    TEST_ASSERT_EQUAL_STRING("3334444444", voci[0].nome);
}

void test_accenti_ridotti_ad_ascii(void)
{
    tutto("BEGIN:VCARD\r\nVERSION:3.0\r\nN:Rossi;Niccol\xC3\xB2;;;\r\n"
          "TEL:3335555555\r\nEND:VCARD\r\n");
    TEST_ASSERT_EQUAL_STRING("Niccolo", voci[0].nome);
}

void test_ascii_toglie_emoji_e_raddrizza_apostrofi(void)
{
    char out[PB_NAME_LEN];
    vcard_ascii(out, sizeof(out), "D\xE2\x80\x99" "Amico \xF0\x9F\x98\x80");
    TEST_ASSERT_EQUAL_STRING("D'Amico", out);
    vcard_ascii(out, sizeof(out), "\xC3\x89lise \xC3\xA0");
    TEST_ASSERT_EQUAL_STRING("Elise a", out);
}

void test_escape_nel_nome(void)
{
    /* Una virgola nel nome arriva come "\,". */
    tutto("BEGIN:VCARD\r\nVERSION:3.0\r\nN:Rossi;Gian\\, Luca;;;\r\n"
          "TEL:3336666666\r\nEND:VCARD\r\n");
    TEST_ASSERT_EQUAL_STRING("Gian, Luca", voci[0].nome);
}

void test_punto_e_virgola_protetto_non_spezza_il_campo(void)
{
    tutto("BEGIN:VCARD\r\nVERSION:3.0\r\nN:Ross\\;i;Ugo;;;\r\n"
          "TEL:3336666666\r\nEND:VCARD\r\n");
    TEST_ASSERT_EQUAL_STRING("Ugo", voci[0].nome);
}

void test_riga_ripiegata_viene_ricucita(void)
{
    /* vCard 3.0: una riga che continua comincia con uno spazio. */
    tutto("BEGIN:VCARD\r\nVERSION:3.0\r\nN:Rossi;Massimi\r\n liano;;;\r\n"
          "TEL:3337777777\r\nEND:VCARD\r\n");
    TEST_ASSERT_EQUAL_STRING("Massimiliano", voci[0].nome);
}

void test_campi_raggruppati_e_minuscoli(void)
{
    /* Alcuni server prefissano il gruppo ("item1.") e non tutti scrivono le
       proprieta' in maiuscolo. */
    tutto("begin:vcard\r\nversion:3.0\r\nn:Rossi;Ada;;;\r\n"
          "item1.TEL;type=pref:3338888888\r\nend:vcard\r\n");
    TEST_ASSERT_EQUAL(1, n_voci);
    TEST_ASSERT_EQUAL_STRING("Ada", voci[0].nome);
    TEST_ASSERT_EQUAL_STRING("3338888888", voci[0].numero);
}

void test_capoversi_solo_lf(void)
{
    tutto("BEGIN:VCARD\nVERSION:3.0\nN:Rossi;Ezio;;;\nTEL:3339999999\nEND:VCARD\n");
    TEST_ASSERT_EQUAL(1, n_voci);
    TEST_ASSERT_EQUAL_STRING("Ezio", voci[0].nome);
}

void test_quoted_printable_della_versione_2_1(void)
{
    /* "Niccolò" in quoted-printable, spezzato da un a-capo morbido. */
    tutto("BEGIN:VCARD\r\nVERSION:2.1\r\n"
          "N;CHARSET=UTF-8;ENCODING=QUOTED-PRINTABLE:Rossi;Nicc=\r\nol=C3=B2;;;\r\n"
          "TEL;CELL:3330000000\r\nEND:VCARD\r\n");
    TEST_ASSERT_EQUAL(1, n_voci);
    TEST_ASSERT_EQUAL_STRING("Niccolo", voci[0].nome);
}

void test_foto_lunghissima_viene_ignorata(void)
{
    char buf[2048];
    int n = snprintf(buf, sizeof(buf),
                     "BEGIN:VCARD\r\nVERSION:3.0\r\nN:Rossi;Bruno;;;\r\n"
                     "PHOTO;ENCODING=b;TYPE=JPEG:");
    for (int i = 0; i < 600; i++) {
        buf[n++] = 'A';
    }
    n += snprintf(buf + n, sizeof(buf) - (size_t)n,
                  "\r\n AAAAAAAAAAAAAAAA\r\nTEL:3331212121\r\nEND:VCARD\r\n");
    tutto(buf);
    TEST_ASSERT_EQUAL(1, n_voci);
    TEST_ASSERT_EQUAL_STRING("Bruno", voci[0].nome);
    TEST_ASSERT_EQUAL_STRING("3331212121", voci[0].numero);
}

void test_numeri_oltre_il_massimo_ignorati(void)
{
    tutto("BEGIN:VCARD\r\nVERSION:3.0\r\nN:Rossi;Tea;;;\r\n"
          "TEL:1\r\nTEL:2\r\nTEL:3\r\nTEL:4\r\nTEL:5\r\nEND:VCARD\r\n");
    TEST_ASSERT_EQUAL(VCARD_MAX_TEL, n_voci);
}

void test_nome_lunghissimo_troncato(void)
{
    tutto("BEGIN:VCARD\r\nVERSION:3.0\r\n"
          "N:Rossi;Maria Assunta Giuseppina Antonietta;;;\r\n"
          "TEL:3331313131\r\nEND:VCARD\r\n");
    TEST_ASSERT_EQUAL(1, n_voci);
    TEST_ASSERT_TRUE(strlen(voci[0].nome) < PB_NAME_LEN);
}

void test_ultima_riga_senza_capoverso(void)
{
    const char *s = "BEGIN:VCARD\r\nN:Rossi;Lia;;;\r\nTEL:3331414141\r\nEND:VCARD";
    vcard_feed(&v, (const uint8_t *)s, strlen(s));
    TEST_ASSERT_EQUAL(0, n_voci);
    vcard_end(&v);
    TEST_ASSERT_EQUAL(1, n_voci);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_tiene_solo_il_nome_di_battesimo);
    RUN_TEST(test_un_byte_alla_volta_da_lo_stesso_risultato);
    RUN_TEST(test_una_voce_per_ogni_numero);
    RUN_TEST(test_piu_contatti_di_fila);
    RUN_TEST(test_contatto_senza_numero_non_produce_voci);
    RUN_TEST(test_senza_nome_di_battesimo_usa_il_nome_completo);
    RUN_TEST(test_senza_alcun_nome_usa_il_numero);
    RUN_TEST(test_accenti_ridotti_ad_ascii);
    RUN_TEST(test_ascii_toglie_emoji_e_raddrizza_apostrofi);
    RUN_TEST(test_escape_nel_nome);
    RUN_TEST(test_punto_e_virgola_protetto_non_spezza_il_campo);
    RUN_TEST(test_riga_ripiegata_viene_ricucita);
    RUN_TEST(test_campi_raggruppati_e_minuscoli);
    RUN_TEST(test_capoversi_solo_lf);
    RUN_TEST(test_quoted_printable_della_versione_2_1);
    RUN_TEST(test_foto_lunghissima_viene_ignorata);
    RUN_TEST(test_numeri_oltre_il_massimo_ignorati);
    RUN_TEST(test_nome_lunghissimo_troncato);
    RUN_TEST(test_ultima_riga_senza_capoverso);
    return UNITY_END();
}
