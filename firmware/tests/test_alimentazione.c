/* Test della regola che ferma il circolo dei riavvii per calo di tensione. */

#include "unity.h"
#include "alimentazione.h"

void setUp(void)    { }
void tearDown(void) { }

void test_un_calo_isolato_non_fa_dormire(void)
{
    const uint8_t s = alim_serie(0, true);
    TEST_ASSERT_EQUAL(1, s);
    TEST_ASSERT_FALSE(alim_deve_dormire(s));
}

void test_tre_cali_di_fila_fanno_dormire(void)
{
    uint8_t s = 0;
    for (int i = 0; i < ALIM_SERIE_MAX; i++) {
        s = alim_serie(s, true);
    }
    TEST_ASSERT_TRUE(alim_deve_dormire(s));
}

void test_un_avvio_normale_azzera_la_serie(void)
{
    /* Dopo il sonno si riparte per risveglio, non per calo: la batteria ha
       di nuovo tre tentativi. */
    uint8_t s = alim_serie(alim_serie(0, true), true);
    s = alim_serie(s, false);
    TEST_ASSERT_EQUAL(0, s);
    TEST_ASSERT_FALSE(alim_deve_dormire(s));
}

void test_la_serie_satura_senza_traboccare(void)
{
    TEST_ASSERT_EQUAL(UINT8_MAX, alim_serie(UINT8_MAX, true));
    TEST_ASSERT_TRUE(alim_deve_dormire(UINT8_MAX));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_un_calo_isolato_non_fa_dormire);
    RUN_TEST(test_tre_cali_di_fila_fanno_dormire);
    RUN_TEST(test_un_avvio_normale_azzera_la_serie);
    RUN_TEST(test_la_serie_satura_senza_traboccare);
    return UNITY_END();
}
