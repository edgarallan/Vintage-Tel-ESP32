/*
 * Test della macchina a stati. Portati da test_state_machine.py della v1,
 * piu' i casi nuovi (richiamo, pulsante) e quelli che in Python erano
 * scomodi da scrivere per via del tempo reale.
 */

#include "unity.h"
#include "fake_hw.h"
#include "phone_fsm.h"

#include <stdio.h>
#include <string.h>

static phone_t     ph;
static phonebook_t pb;

#define INTERDIGIT_MS 8000
#define QUICKDIAL_MS  1500
#define BUSY_MS       3000

void setUp(void)
{
    fake_hw_init(&g_fake);
    pb_init(&pb);
    pb_add(&pb, "Emergenza", "112", 9);
    pb_add(&pb, "Mario", "+393331234567", PB_NO_QUICK_DIAL);

    phone_config_t cfg = {
        .interdigit_ms = INTERDIGIT_MS,
        .quickdial_ms  = QUICKDIAL_MS,
        .busy_ms       = BUSY_MS,
    };
    ring_config_t ring = { .on_ms = 1000, .off_ms = 4000, .max_rings = 30 };
    phone_init(&ph, fake_hw_iface(), &pb, &cfg, &ring);
}

void tearDown(void) { }

/* --- piccoli aiuti per scrivere i test come una sequenza di gesti --------- */

static void at(uint32_t ms) { g_fake.now_ms = ms; }

static void send(phone_ev_type_t type)
{
    phone_ev_t ev = { .type = type, .now_ms = g_fake.now_ms };
    phone_handle(&ph, &ev);
}

static void send_digit(uint8_t digit)
{
    phone_ev_t ev = { .type = EV_DIGIT, .now_ms = g_fake.now_ms, .digit = digit };
    phone_handle(&ph, &ev);
}

static void send_incoming(const char *caller)
{
    phone_ev_t ev = { .type = EV_INCOMING_CALL, .now_ms = g_fake.now_ms };
    snprintf(ev.caller, sizeof(ev.caller), "%s", caller);
    phone_handle(&ph, &ev);
}

static void tick_to(uint32_t ms)
{
    at(ms);
    send(EV_TICK);
}

/* --- chiamata in arrivo --------------------------------------------------- */

void test_chiamata_in_arrivo_fa_squillare(void)
{
    send_incoming("+393331234567");
    TEST_ASSERT_EQUAL(ST_RINGING, phone_state(&ph));
    TEST_ASSERT_TRUE(g_fake.bell_on);
}

void test_chiamata_in_arrivo_mostra_il_nome_dalla_rubrica(void)
{
    /* Sul display deve comparire "Mario", non il numero: e' il motivo
       per cui la rubrica esiste. */
    send_incoming("0039-333-1234567");
    TEST_ASSERT_EQUAL_STRING("Mario", g_fake.display_name);
}

void test_chiamante_sconosciuto_mostra_il_numero(void)
{
    send_incoming("+393339999999");
    TEST_ASSERT_EQUAL_STRING("+393339999999", g_fake.display_name);
}

void test_chiamata_in_arrivo_ignorata_se_non_a_riposo(void)
{
    /* Cornetta gia' sollevata: non deve mettersi a squillare in mano. */
    send(EV_HOOK_UP);
    TEST_ASSERT_EQUAL(ST_DIALING, phone_state(&ph));

    send_incoming("x");
    TEST_ASSERT_EQUAL(ST_DIALING, phone_state(&ph));
    TEST_ASSERT_FALSE(g_fake.bell_on);
}

void test_cornetta_su_risponde(void)
{
    send_incoming("x");
    send(EV_HOOK_UP);
    TEST_ASSERT_EQUAL(ST_IN_CALL, phone_state(&ph));
    TEST_ASSERT_TRUE(fake_did(&g_fake, "answer"));
    TEST_ASSERT_FALSE(g_fake.bell_on);
}

void test_cornetta_giu_durante_squillo_rifiuta(void)
{
    send_incoming("x");
    send(EV_HOOK_DOWN);
    TEST_ASSERT_EQUAL(ST_IDLE, phone_state(&ph));
    TEST_ASSERT_TRUE(fake_did(&g_fake, "reject"));
    TEST_ASSERT_FALSE(g_fake.bell_on);
}

void test_la_campana_segue_la_cadenza_italiana(void)
{
    send_incoming("x");
    TEST_ASSERT_TRUE(g_fake.bell_on);
    tick_to(1000);
    TEST_ASSERT_FALSE(g_fake.bell_on);   /* pausa */
    tick_to(5000);
    TEST_ASSERT_TRUE(g_fake.bell_on);    /* secondo squillo */
}

/* --- composizione --------------------------------------------------------- */

void test_cornetta_su_da_tono_di_libero(void)
{
    send(EV_HOOK_UP);
    TEST_ASSERT_EQUAL(ST_DIALING, phone_state(&ph));
    TEST_ASSERT_EQUAL(TONE_DIAL, g_fake.last_tone);
}

void test_la_prima_cifra_zittisce_il_tono_di_libero(void)
{
    send(EV_HOOK_UP);
    send_digit(1);
    TEST_ASSERT_NOT_EQUAL(TONE_DIAL, g_fake.last_tone);
}

void test_compone_dopo_la_pausa_tra_cifre(void)
{
    send(EV_HOOK_UP);
    at(100); send_digit(1);
    at(200); send_digit(2);
    at(300); send_digit(3);

    tick_to(300 + INTERDIGIT_MS - 1);
    TEST_ASSERT_FALSE(fake_did(&g_fake, "place_call:123"));

    tick_to(300 + INTERDIGIT_MS);
    TEST_ASSERT_TRUE(fake_did(&g_fake, "place_call:123"));
    TEST_ASSERT_EQUAL(ST_IN_CALL, phone_state(&ph));
}

void test_quick_dial_con_una_cifra_sola(void)
{
    send(EV_HOOK_UP);
    at(100); send_digit(9);        /* 9 -> Emergenza 112 */

    tick_to(100 + QUICKDIAL_MS);
    TEST_ASSERT_TRUE(fake_did(&g_fake, "place_call:112"));
}

void test_una_seconda_cifra_annulla_il_quick_dial(void)
{
    /* 9 e' un quick-dial, ma 91 e' l'inizio di un numero vero:
       chi continua a comporre non deve ritrovarsi a chiamare il 112. */
    send(EV_HOOK_UP);
    at(100); send_digit(9);
    at(500); send_digit(1);        /* prima che scada il quick-dial */

    tick_to(500 + QUICKDIAL_MS);
    TEST_ASSERT_FALSE(fake_did(&g_fake, "place_call:112"));

    tick_to(500 + INTERDIGIT_MS);
    TEST_ASSERT_TRUE(fake_did(&g_fake, "place_call:91"));
}

void test_cifra_senza_quick_dial_aspetta_la_pausa_lunga(void)
{
    send(EV_HOOK_UP);
    at(100); send_digit(7);        /* 7 non e' assegnato */

    tick_to(100 + QUICKDIAL_MS);
    TEST_ASSERT_EQUAL(ST_DIALING, phone_state(&ph));
    TEST_ASSERT_FALSE(fake_did(&g_fake, "place_call:7"));

    tick_to(100 + INTERDIGIT_MS);
    TEST_ASSERT_TRUE(fake_did(&g_fake, "place_call:7"));
}

void test_cornetta_giu_durante_composizione_annulla(void)
{
    send(EV_HOOK_UP);
    at(100); send_digit(1);
    send(EV_HOOK_DOWN);

    TEST_ASSERT_EQUAL(ST_IDLE, phone_state(&ph));
    TEST_ASSERT_EQUAL_STRING("", phone_dialed(&ph));

    /* E il numero abbandonato non deve partire dopo, allo scadere del timeout. */
    tick_to(100 + INTERDIGIT_MS + 1000);
    TEST_ASSERT_FALSE(fake_did(&g_fake, "place_call:1"));
}

void test_numero_troppo_lungo_non_trabocca(void)
{
    send(EV_HOOK_UP);
    for (int i = 0; i < PHONE_MAX_DIGITS + 10; i++) {
        at(100 + i * 10);
        send_digit(1);
    }
    TEST_ASSERT_TRUE(strlen(phone_dialed(&ph)) <= PHONE_MAX_DIGITS);
}

/* --- chiamata attiva ------------------------------------------------------ */

void test_cornetta_giu_chiude_la_chiamata(void)
{
    send_incoming("x");
    send(EV_HOOK_UP);
    send(EV_HOOK_DOWN);
    TEST_ASSERT_EQUAL(ST_IDLE, phone_state(&ph));
    TEST_ASSERT_TRUE(fake_did(&g_fake, "hangup"));
}

void test_il_disco_in_chiamata_manda_dtmf(void)
{
    /* Serve per i menu vocali: il disco continua a funzionare dentro
       la conversazione. */
    send_incoming("x");
    send(EV_HOOK_UP);
    send_digit(5);
    TEST_ASSERT_TRUE(fake_did(&g_fake, "dtmf:5"));
    TEST_ASSERT_EQUAL(ST_IN_CALL, phone_state(&ph));
}

void test_chiusura_dal_remoto_torna_a_riposo(void)
{
    send_incoming("x");
    send(EV_HOOK_UP);
    TEST_ASSERT_EQUAL(ST_IN_CALL, phone_state(&ph));

    send(EV_CALL_ENDED);
    TEST_ASSERT_EQUAL(ST_IDLE, phone_state(&ph));
}

/* --- fallimenti ----------------------------------------------------------- */

void test_chiamata_fallita_da_occupato_poi_torna_a_riposo(void)
{
    g_fake.place_call_ok = false;

    send(EV_HOOK_UP);
    at(100); send_digit(1);
    tick_to(100 + INTERDIGIT_MS);

    TEST_ASSERT_TRUE(fake_did(&g_fake, "place_call:1"));
    TEST_ASSERT_EQUAL(TONE_BUSY, g_fake.last_tone);

    tick_to(100 + INTERDIGIT_MS + BUSY_MS);
    TEST_ASSERT_EQUAL(ST_IDLE, phone_state(&ph));
}

void test_senza_bluetooth_non_chiama_e_da_occupato(void)
{
    /* Senza cellulare accoppiato non si tenta nemmeno: si dice subito
       all'utente che non si puo' fare, col tono che gia' conosce. */
    g_fake.bt_connected = false;

    send(EV_HOOK_UP);
    at(100); send_digit(1);
    tick_to(100 + INTERDIGIT_MS);

    TEST_ASSERT_FALSE(fake_did(&g_fake, "place_call:1"));
    TEST_ASSERT_EQUAL(TONE_BUSY, g_fake.last_tone);

    tick_to(100 + INTERDIGIT_MS + BUSY_MS);
    TEST_ASSERT_EQUAL(ST_IDLE, phone_state(&ph));
}

/* --- richiamo dell'ultimo numero (nuovo rispetto alla v1) ----------------- */

void test_il_pulsante_richiama_l_ultimo_numero(void)
{
    send(EV_HOOK_UP);
    at(100); send_digit(1);
    at(200); send_digit(2);
    tick_to(200 + INTERDIGIT_MS);
    TEST_ASSERT_TRUE(fake_did(&g_fake, "place_call:12"));

    send(EV_HOOK_DOWN);
    TEST_ASSERT_EQUAL(ST_IDLE, phone_state(&ph));

    send(EV_HOOK_UP);
    send(EV_BUTTON);
    TEST_ASSERT_EQUAL(2, fake_count(&g_fake, "place_call:12"));
}

void test_il_pulsante_non_fa_nulla_senza_un_numero_precedente(void)
{
    send(EV_HOOK_UP);
    send(EV_BUTTON);
    TEST_ASSERT_EQUAL(ST_DIALING, phone_state(&ph));
}

void test_il_quick_dial_alimenta_il_richiamo(void)
{
    send(EV_HOOK_UP);
    at(100); send_digit(9);
    tick_to(100 + QUICKDIAL_MS);
    send(EV_HOOK_DOWN);

    send(EV_HOOK_UP);
    send(EV_BUTTON);
    TEST_ASSERT_EQUAL(2, fake_count(&g_fake, "place_call:112"));
}

/* --- coerenza generale ---------------------------------------------------- */

void test_ogni_stato_aggiorna_il_led(void)
{
    /* Il LED e' l'unico segnale visibile a telefono chiuso: nessuna
       transizione deve dimenticarselo. */
    send(EV_HOOK_UP);
    TEST_ASSERT_EQUAL(LED_DIALING, g_fake.last_led);

    send(EV_HOOK_DOWN);
    TEST_ASSERT_EQUAL(LED_IDLE, g_fake.last_led);

    send_incoming("x");
    TEST_ASSERT_EQUAL(LED_RINGING, g_fake.last_led);

    send(EV_HOOK_UP);
    TEST_ASSERT_EQUAL(LED_IN_CALL, g_fake.last_led);
}

void test_eventi_fuori_contesto_non_rompono_nulla(void)
{
    /* Rimbalzi del gancio e cifre spurie non devono portare la macchina
       in uno stato incoerente. */
    send(EV_HOOK_DOWN);
    TEST_ASSERT_EQUAL(ST_IDLE, phone_state(&ph));

    send_digit(5);
    TEST_ASSERT_EQUAL(ST_IDLE, phone_state(&ph));

    send(EV_CALL_ENDED);
    TEST_ASSERT_EQUAL(ST_IDLE, phone_state(&ph));

    /* Rimbalzo del gancio durante la composizione: il contatto della forcella
       e' meccanico e puo' ripetere. Non deve azzerare il numero in corso. */
    send(EV_HOOK_UP);
    at(100); send_digit(4);
    send(EV_HOOK_UP);
    TEST_ASSERT_EQUAL(ST_DIALING, phone_state(&ph));
    TEST_ASSERT_EQUAL_STRING("4", phone_dialed(&ph));
    send(EV_HOOK_DOWN);

    send(EV_BUTTON);
    TEST_ASSERT_EQUAL(ST_IDLE, phone_state(&ph));

    tick_to(999999);
    TEST_ASSERT_EQUAL(ST_IDLE, phone_state(&ph));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_chiamata_in_arrivo_fa_squillare);
    RUN_TEST(test_chiamata_in_arrivo_mostra_il_nome_dalla_rubrica);
    RUN_TEST(test_chiamante_sconosciuto_mostra_il_numero);
    RUN_TEST(test_chiamata_in_arrivo_ignorata_se_non_a_riposo);
    RUN_TEST(test_cornetta_su_risponde);
    RUN_TEST(test_cornetta_giu_durante_squillo_rifiuta);
    RUN_TEST(test_la_campana_segue_la_cadenza_italiana);
    RUN_TEST(test_cornetta_su_da_tono_di_libero);
    RUN_TEST(test_la_prima_cifra_zittisce_il_tono_di_libero);
    RUN_TEST(test_compone_dopo_la_pausa_tra_cifre);
    RUN_TEST(test_quick_dial_con_una_cifra_sola);
    RUN_TEST(test_una_seconda_cifra_annulla_il_quick_dial);
    RUN_TEST(test_cifra_senza_quick_dial_aspetta_la_pausa_lunga);
    RUN_TEST(test_cornetta_giu_durante_composizione_annulla);
    RUN_TEST(test_numero_troppo_lungo_non_trabocca);
    RUN_TEST(test_cornetta_giu_chiude_la_chiamata);
    RUN_TEST(test_il_disco_in_chiamata_manda_dtmf);
    RUN_TEST(test_chiusura_dal_remoto_torna_a_riposo);
    RUN_TEST(test_chiamata_fallita_da_occupato_poi_torna_a_riposo);
    RUN_TEST(test_senza_bluetooth_non_chiama_e_da_occupato);
    RUN_TEST(test_il_pulsante_richiama_l_ultimo_numero);
    RUN_TEST(test_il_pulsante_non_fa_nulla_senza_un_numero_precedente);
    RUN_TEST(test_il_quick_dial_alimenta_il_richiamo);
    RUN_TEST(test_ogni_stato_aggiorna_il_led);
    RUN_TEST(test_eventi_fuori_contesto_non_rompono_nulla);
    return UNITY_END();
}
