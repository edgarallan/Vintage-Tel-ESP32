/* Test della cadenza di squillo italiana (1 s on / 4 s off). */

#include "unity.h"
#include "ring_pattern.h"

static ring_pattern_t r;

static void init_with(uint8_t max_rings)
{
    ring_config_t cfg = { .on_ms = 1000, .off_ms = 4000, .max_rings = max_rings };
    ring_init(&r, &cfg);
}

void setUp(void)    { init_with(0); }
void tearDown(void) { }

void test_a_riposo_la_campana_e_ferma(void)
{
    TEST_ASSERT_FALSE(ring_is_active(&r));
    TEST_ASSERT_FALSE(ring_bell_is_on(&r));
}

void test_lo_squillo_parte_subito(void)
{
    /* Nessun ritardo all'inizio: chi chiama non deve aspettare 4 secondi
       prima di sentire il primo DRIN. */
    ring_start(&r, 0);
    TEST_ASSERT_TRUE(ring_is_active(&r));
    TEST_ASSERT_TRUE(ring_bell_is_on(&r));
}

void test_cadenza_uno_acceso_quattro_spento(void)
{
    bool bell = false;
    ring_start(&r, 0);

    TEST_ASSERT_FALSE(ring_tick(&r, 999, &bell));    /* ancora dentro l'ON */

    TEST_ASSERT_TRUE(ring_tick(&r, 1000, &bell));    /* fine ON */
    TEST_ASSERT_FALSE(bell);

    TEST_ASSERT_FALSE(ring_tick(&r, 4999, &bell));   /* ancora in pausa */

    TEST_ASSERT_TRUE(ring_tick(&r, 5000, &bell));    /* nuovo squillo */
    TEST_ASSERT_TRUE(bell);
}

void test_si_ferma_dopo_max_rings(void)
{
    bool bell = false;
    init_with(2);
    ring_start(&r, 0);

    ring_tick(&r, 1000, &bell);      /* fine 1o squillo */
    ring_tick(&r, 5000, &bell);      /* 2o squillo */
    TEST_ASSERT_TRUE(bell);
    ring_tick(&r, 6000, &bell);      /* fine 2o squillo */

    /* Raggiunto il limite: niente terzo squillo, e la cadenza si spegne. */
    TEST_ASSERT_FALSE(ring_tick(&r, 10000, &bell));
    TEST_ASSERT_FALSE(ring_is_active(&r));
    TEST_ASSERT_FALSE(ring_bell_is_on(&r));
}

void test_max_rings_zero_significa_senza_limite(void)
{
    bool bell = false;
    ring_start(&r, 0);
    for (uint32_t t = 1000; t <= 100000; t += 1000) {
        ring_tick(&r, t, &bell);
    }
    TEST_ASSERT_TRUE(ring_is_active(&r));
}

void test_stop_spegne_subito_anche_a_campana_accesa(void)
{
    /* Si risponde a meta' squillo: la campana deve tacere all'istante,
       non finire il secondo in corso. */
    ring_start(&r, 0);
    TEST_ASSERT_TRUE(ring_bell_is_on(&r));

    ring_stop(&r);
    TEST_ASSERT_FALSE(ring_is_active(&r));
    TEST_ASSERT_FALSE(ring_bell_is_on(&r));
}

void test_tick_a_riposo_non_fa_nulla(void)
{
    bool bell = true;
    TEST_ASSERT_FALSE(ring_tick(&r, 999999, &bell));
}

void test_stop_e_idempotente(void)
{
    ring_start(&r, 0);
    ring_stop(&r);
    ring_stop(&r);
    TEST_ASSERT_FALSE(ring_is_active(&r));
}

void test_ripartenza_azzera_il_conteggio(void)
{
    /* Una seconda chiamata deve avere di nuovo tutti i suoi squilli. */
    bool bell = false;
    init_with(2);

    ring_start(&r, 0);
    ring_tick(&r, 1000, &bell);
    ring_tick(&r, 5000, &bell);
    ring_tick(&r, 6000, &bell);
    ring_tick(&r, 10000, &bell);
    TEST_ASSERT_FALSE(ring_is_active(&r));

    ring_start(&r, 20000);
    TEST_ASSERT_TRUE(ring_is_active(&r));
    TEST_ASSERT_TRUE(ring_bell_is_on(&r));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_riposo_la_campana_e_ferma);
    RUN_TEST(test_lo_squillo_parte_subito);
    RUN_TEST(test_cadenza_uno_acceso_quattro_spento);
    RUN_TEST(test_si_ferma_dopo_max_rings);
    RUN_TEST(test_max_rings_zero_significa_senza_limite);
    RUN_TEST(test_stop_spegne_subito_anche_a_campana_accesa);
    RUN_TEST(test_tick_a_riposo_non_fa_nulla);
    RUN_TEST(test_stop_e_idempotente);
    RUN_TEST(test_ripartenza_azzera_il_conteggio);
    return UNITY_END();
}
