/* Test della rubrica. Portati da test_phonebook.py della v1. */

#include "unity.h"
#include "phonebook.h"

#include <stdio.h>
#include <string.h>

static phonebook_t pb;

void setUp(void)    { pb_init(&pb); }
void tearDown(void) { }

void test_rubrica_nuova_e_vuota(void)
{
    TEST_ASSERT_NULL(pb_lookup_name(&pb, "+393331234567"));
    TEST_ASSERT_NULL(pb_quick_dial(&pb, 1));
}

void test_riconosce_il_numero_in_formati_diversi(void)
{
    /* Il caller-id arriva formattato in modi imprevedibili: il confronto
       sulle ultime cifre deve assorbire prefissi e separatori. */
    TEST_ASSERT_TRUE(pb_add(&pb, "Mario", "+393331234567", PB_NO_QUICK_DIAL));

    TEST_ASSERT_EQUAL_STRING("Mario", pb_lookup_name(&pb, "+393331234567"));
    TEST_ASSERT_EQUAL_STRING("Mario", pb_lookup_name(&pb, "0039-333-1234567"));
    TEST_ASSERT_EQUAL_STRING("Mario", pb_lookup_name(&pb, "333 1234567"));
    TEST_ASSERT_EQUAL_STRING("Mario", pb_lookup_name(&pb, "3331234567"));
}

void test_numero_sconosciuto_da_null(void)
{
    pb_add(&pb, "Mario", "+393331234567", PB_NO_QUICK_DIAL);
    TEST_ASSERT_NULL(pb_lookup_name(&pb, "+393339999999"));
}

void test_stringa_senza_cifre_da_null(void)
{
    /* Chiamante anonimo: la centrale manda testo, non un numero. */
    pb_add(&pb, "Mario", "+393331234567", PB_NO_QUICK_DIAL);
    TEST_ASSERT_NULL(pb_lookup_name(&pb, "sconosciuto"));
    TEST_ASSERT_NULL(pb_lookup_name(&pb, ""));
}

void test_numero_troppo_corto_non_fa_falsi_positivi(void)
{
    /* "567" non deve agganciare un numero che finisce per 567:
       servono tutte le cifre di confronto. */
    pb_add(&pb, "Mario", "+393331234567", PB_NO_QUICK_DIAL);
    TEST_ASSERT_NULL(pb_lookup_name(&pb, "567"));
}

void test_numero_breve_confrontato_per_intero(void)
{
    /* I numeri brevi (112, 118) non hanno 9 cifre: vanno confrontati
       per quello che sono, altrimenti le emergenze non si riconoscono. */
    pb_add(&pb, "Emergenza", "112", PB_NO_QUICK_DIAL);
    TEST_ASSERT_EQUAL_STRING("Emergenza", pb_lookup_name(&pb, "112"));
    TEST_ASSERT_NULL(pb_lookup_name(&pb, "113"));
}

void test_numero_duplicato_rifiutato(void)
{
    TEST_ASSERT_TRUE(pb_add(&pb, "Mario", "+393331234567", PB_NO_QUICK_DIAL));
    TEST_ASSERT_FALSE(pb_add(&pb, "Mario bis", "+393331234567", PB_NO_QUICK_DIAL));
    TEST_ASSERT_EQUAL_STRING("Mario", pb_lookup_name(&pb, "+393331234567"));
}

void test_quick_dial(void)
{
    pb_add(&pb, "Emergenza", "112", 9);
    pb_add(&pb, "Mario", "+393331234567", 1);

    TEST_ASSERT_EQUAL_STRING("112", pb_quick_dial(&pb, 9));
    TEST_ASSERT_EQUAL_STRING("+393331234567", pb_quick_dial(&pb, 1));
    TEST_ASSERT_NULL(pb_quick_dial(&pb, 7));
}

void test_quick_dial_duplicato_rifiutato(void)
{
    /* Due contatti sulla stessa cifra renderebbero il quick-dial ambiguo. */
    TEST_ASSERT_TRUE(pb_add(&pb, "Mario", "+393331111111", 1));
    TEST_ASSERT_FALSE(pb_add(&pb, "Anna", "+393332222222", 1));
    TEST_ASSERT_EQUAL_STRING("+393331111111", pb_quick_dial(&pb, 1));
}

void test_rubrica_piena_rifiuta_senza_traboccare(void)
{
    char number[PB_NUMBER_LEN];
    for (int i = 0; i < PB_MAX_CONTACTS; i++) {
        snprintf(number, sizeof(number), "33300000%02d", i);
        TEST_ASSERT_TRUE(pb_add(&pb, "Tizio", number, PB_NO_QUICK_DIAL));
    }
    TEST_ASSERT_FALSE(pb_add(&pb, "Uno di troppo", "3339999999", PB_NO_QUICK_DIAL));
}

void test_nome_troppo_lungo_viene_troncato_senza_traboccare(void)
{
    const char *lungo = "Un nome assurdamente lungo che non ci sta nel buffer";
    TEST_ASSERT_TRUE(pb_add(&pb, lungo, "+393331234567", PB_NO_QUICK_DIAL));
    const char *got = pb_lookup_name(&pb, "+393331234567");
    TEST_ASSERT_NOT_NULL(got);
    TEST_ASSERT_TRUE(strlen(got) < PB_NAME_LEN);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_rubrica_nuova_e_vuota);
    RUN_TEST(test_riconosce_il_numero_in_formati_diversi);
    RUN_TEST(test_numero_sconosciuto_da_null);
    RUN_TEST(test_stringa_senza_cifre_da_null);
    RUN_TEST(test_numero_troppo_corto_non_fa_falsi_positivi);
    RUN_TEST(test_numero_breve_confrontato_per_intero);
    RUN_TEST(test_numero_duplicato_rifiutato);
    RUN_TEST(test_quick_dial);
    RUN_TEST(test_quick_dial_duplicato_rifiutato);
    RUN_TEST(test_rubrica_piena_rifiuta_senza_traboccare);
    RUN_TEST(test_nome_troppo_lungo_viene_troncato_senza_traboccare);
    return UNITY_END();
}
