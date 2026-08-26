/*
 * Test della decodifica impulsi -> cifra.
 * Portati dalla suite pytest della v1 (test_dial_reader.py): stessi casi,
 * stessa semantica, ma qui il tempo e' un parametro invece che l'orologio.
 */

#include "unity.h"
#include "dial_decode.h"

static dial_decoder_t d;

static void init_with(uint8_t zero_pulses, uint32_t timeout_ms)
{
    dial_config_t cfg = { .zero_pulses = zero_pulses, .digit_timeout_ms = timeout_ms };
    dial_init(&d, &cfg);
}

/* Come init_with, ma con la finestra cieca attiva. Tenuta separata perche'
   gli altri test spaziano gli impulsi di 10 ms e vogliono che passino tutti. */
static void init_con_finestra(uint32_t gap_ms)
{
    dial_config_t cfg = {
        .zero_pulses      = 10,
        .digit_timeout_ms = 0,
        .min_pulse_gap_ms = gap_ms,
    };
    dial_init(&d, &cfg);
}

void setUp(void)    { init_with(10, 0); }
void tearDown(void) { }

/* Compone una cifra completa: NSI giu', N impulsi, NSI su. */
static bool dial_digit(uint8_t pulses, uint8_t *out)
{
    uint8_t ignored;
    dial_on_nsi(&d, true, &ignored);
    for (uint8_t i = 0; i < pulses; i++) {
        dial_on_pulse(&d, 100 + i * 10);
    }
    return dial_on_nsi(&d, false, out);
}

/* --- finestra cieca contro il rimbalzo del contatto ----------------------- */

void test_il_rimbalzo_non_gonfia_la_cifra(void)
{
    /* Riproduce il rimbalzo misurato il 26/08/2026 sull'apparecchio reale:
       dopo ogni impulso vero il contatto produce una raffica di fronti spuri
       entro ~1,3 ms. Qui sono modellati come impulsi a +1 e +2 ms.
       Senza finestra cieca un "3" diventerebbe un "9". */
    init_con_finestra(8);

    uint8_t digit = 0;
    uint8_t ignored;
    dial_on_nsi(&d, true, &ignored);

    for (int i = 0; i < 3; i++) {
        const uint32_t t = 100 + i * 100;   /* impulsi veri: 100 ms di distanza */
        dial_on_pulse(&d, t);
        dial_on_pulse(&d, t + 1);           /* rimbalzo */
        dial_on_pulse(&d, t + 2);           /* rimbalzo */
    }

    TEST_ASSERT_TRUE(dial_on_nsi(&d, false, &digit));
    TEST_ASSERT_EQUAL_UINT8(3, digit);
}

void test_senza_finestra_il_rimbalzo_falsa_la_cifra(void)
{
    /* Controprova: e' il comportamento che il firmware aveva sull'hardware
       vero, ed e' il motivo per cui un 3 composto usciva come cifra sbagliata.
       Serve a dimostrare che il test sopra misura davvero qualcosa. */
    init_con_finestra(0);   /* 0 = disattivata */

    uint8_t digit = 0;
    uint8_t ignored;
    dial_on_nsi(&d, true, &ignored);

    for (int i = 0; i < 3; i++) {
        const uint32_t t = 100 + i * 100;
        dial_on_pulse(&d, t);
        dial_on_pulse(&d, t + 1);
        dial_on_pulse(&d, t + 2);
    }

    TEST_ASSERT_TRUE(dial_on_nsi(&d, false, &digit));
    TEST_ASSERT_NOT_EQUAL(3, digit);   /* nove impulsi contati, non tre */
}

void test_la_finestra_non_mangia_impulsi_veri(void)
{
    /* Il rischio opposto: una finestra troppo larga perderebbe impulsi buoni.
       A 10 impulsi al secondo la distanza vera e' ~100 ms, quindi anche una
       finestra da 8 ms ha un margine di dodici volte. Uno "0" deve restare 0,
       cioe' dieci impulsi tutti contati. */
    init_con_finestra(8);

    uint8_t digit = 99;
    uint8_t ignored;
    dial_on_nsi(&d, true, &ignored);
    for (int i = 0; i < 10; i++) {
        dial_on_pulse(&d, 100 + i * 100);
    }
    TEST_ASSERT_TRUE(dial_on_nsi(&d, false, &digit));
    TEST_ASSERT_EQUAL_UINT8(0, digit);
}

void test_il_primo_impulso_passa_sempre(void)
{
    /* last_pulse_ms sopravvive alla rotazione precedente: se il primo impulso
       fosse confrontato con quel residuo, la prima cifra composta dopo una
       pausa breve perderebbe un impulso. */
    init_con_finestra(8);

    uint8_t digit = 0;
    uint8_t ignored;

    dial_on_nsi(&d, true, &ignored);
    dial_on_pulse(&d, 1000);
    TEST_ASSERT_TRUE(dial_on_nsi(&d, false, &digit));
    TEST_ASSERT_EQUAL_UINT8(1, digit);

    /* Seconda cifra a soli 2 ms dall'impulso precedente. */
    dial_on_nsi(&d, true, &ignored);
    dial_on_pulse(&d, 1002);
    TEST_ASSERT_TRUE(dial_on_nsi(&d, false, &digit));
    TEST_ASSERT_EQUAL_UINT8(1, digit);
}

/* --- conteggio impulsi -> cifra ------------------------------------------ */

void test_un_impulso_da_uno(void)
{
    uint8_t digit = 0xFF;
    TEST_ASSERT_TRUE(dial_digit(1, &digit));
    TEST_ASSERT_EQUAL_UINT8(1, digit);
}

void test_cinque_impulsi_danno_cinque(void)
{
    uint8_t digit = 0xFF;
    TEST_ASSERT_TRUE(dial_digit(5, &digit));
    TEST_ASSERT_EQUAL_UINT8(5, digit);
}

void test_nove_impulsi_danno_nove(void)
{
    uint8_t digit = 0xFF;
    TEST_ASSERT_TRUE(dial_digit(9, &digit));
    TEST_ASSERT_EQUAL_UINT8(9, digit);
}

void test_dieci_impulsi_danno_zero(void)
{
    /* Convenzione italiana: il buco dello 0 e' l'ultimo, 10 scatti. */
    uint8_t digit = 0xFF;
    TEST_ASSERT_TRUE(dial_digit(10, &digit));
    TEST_ASSERT_EQUAL_UINT8(0, digit);
}

void test_oltre_dieci_impulsi_danno_comunque_zero(void)
{
    /* Un rimbalzo meccanico non deve produrre una cifra assurda. */
    uint8_t digit = 0xFF;
    TEST_ASSERT_TRUE(dial_digit(11, &digit));
    TEST_ASSERT_EQUAL_UINT8(0, digit);
}

void test_disco_mosso_senza_impulsi_non_emette_nulla(void)
{
    uint8_t digit = 0xFF;
    TEST_ASSERT_FALSE(dial_digit(0, &digit));
    TEST_ASSERT_EQUAL_UINT8(0xFF, digit);
}

void test_cifre_consecutive(void)
{
    uint8_t first = 0xFF, second = 0xFF;
    TEST_ASSERT_TRUE(dial_digit(3, &first));
    TEST_ASSERT_TRUE(dial_digit(10, &second));
    TEST_ASSERT_EQUAL_UINT8(3, first);
    TEST_ASSERT_EQUAL_UINT8(0, second);
}

/* --- fallback a tempo ----------------------------------------------------- */

void test_fallback_completa_la_cifra_senza_rilascio_nsi(void)
{
    /* Se il contatto NSI non riapre, la cifra deve uscire lo stesso:
       altrimenti un contatto sporco blocca il telefono. */
    uint8_t digit = 0xFF;
    init_with(10, 50);

    dial_on_nsi(&d, true, &digit);
    for (int i = 0; i < 4; i++) {
        dial_on_pulse(&d, 100 + i * 10);
    }

    TEST_ASSERT_FALSE(dial_tick(&d, 150, &digit));      /* 20 ms dopo: presto */
    TEST_ASSERT_TRUE(dial_tick(&d, 200, &digit));       /* 70 ms dopo: scatta */
    TEST_ASSERT_EQUAL_UINT8(4, digit);
}

void test_nessuna_doppia_emissione_tra_nsi_e_fallback(void)
{
    /* Percorso normale e fallback non devono produrre due cifre
       per la stessa rotazione del disco. */
    uint8_t digit = 0xFF;
    init_with(10, 50);

    dial_on_nsi(&d, true, &digit);
    dial_on_pulse(&d, 100);
    dial_on_pulse(&d, 110);
    TEST_ASSERT_TRUE(dial_on_nsi(&d, false, &digit));
    TEST_ASSERT_EQUAL_UINT8(2, digit);

    TEST_ASSERT_FALSE(dial_tick(&d, 1000, &digit));     /* ben oltre il timeout */
}

void test_fallback_disattivo_con_timeout_zero(void)
{
    uint8_t digit = 0xFF;
    init_with(10, 0);

    dial_on_nsi(&d, true, &digit);
    for (int i = 0; i < 3; i++) {
        dial_on_pulse(&d, 100 + i * 10);
    }

    TEST_ASSERT_FALSE(dial_tick(&d, 10000, &digit));    /* nessun fallback */
    TEST_ASSERT_TRUE(dial_on_nsi(&d, false, &digit));   /* solo l'NSI finalizza */
    TEST_ASSERT_EQUAL_UINT8(3, digit);
}

void test_zero_pulses_a_zero_usa_il_default_italiano(void)
{
    /* Una configurazione dimenticata (o azzerata dalla pagina web) renderebbe
       ogni cifra uno 0, perche' qualunque conteggio supererebbe la soglia.
       Il default deve subentrare. */
    uint8_t digit = 0xFF;
    init_with(0, 0);

    TEST_ASSERT_TRUE(dial_digit(5, &digit));
    TEST_ASSERT_EQUAL_UINT8(5, digit);

    TEST_ASSERT_TRUE(dial_digit(10, &digit));
    TEST_ASSERT_EQUAL_UINT8(0, digit);
}

void test_impulsi_fuori_rotazione_sono_ignorati(void)
{
    /* Vibrazioni a disco fermo non devono contare. */
    uint8_t digit = 0xFF;
    dial_on_pulse(&d, 100);
    dial_on_pulse(&d, 110);
    TEST_ASSERT_FALSE(dial_on_nsi(&d, false, &digit));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_un_impulso_da_uno);
    RUN_TEST(test_cinque_impulsi_danno_cinque);
    RUN_TEST(test_nove_impulsi_danno_nove);
    RUN_TEST(test_dieci_impulsi_danno_zero);
    RUN_TEST(test_oltre_dieci_impulsi_danno_comunque_zero);
    RUN_TEST(test_disco_mosso_senza_impulsi_non_emette_nulla);
    RUN_TEST(test_cifre_consecutive);
    RUN_TEST(test_fallback_completa_la_cifra_senza_rilascio_nsi);
    RUN_TEST(test_nessuna_doppia_emissione_tra_nsi_e_fallback);
    RUN_TEST(test_fallback_disattivo_con_timeout_zero);
    RUN_TEST(test_zero_pulses_a_zero_usa_il_default_italiano);
    RUN_TEST(test_impulsi_fuori_rotazione_sono_ignorati);
    RUN_TEST(test_il_rimbalzo_non_gonfia_la_cifra);
    RUN_TEST(test_senza_finestra_il_rimbalzo_falsa_la_cifra);
    RUN_TEST(test_la_finestra_non_mangia_impulsi_veri);
    RUN_TEST(test_il_primo_impulso_passa_sempre);
    return UNITY_END();
}
