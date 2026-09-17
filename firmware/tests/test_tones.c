/* Test del generatore di toni italiani. */

#include "unity.h"
#include "tones.h"

#include <stdlib.h>

static tone_gen_t g;
/* Quattro secondi: la cadenza del tono di centrale dura due secondi, e per
   verificare che si ripeta ne servono due periodi. */
static int16_t    buf[4 * TONE_SAMPLE_RATE];

void setUp(void)    { tone_init(&g); }
void tearDown(void) { }

/* Conta gli attraversamenti dello zero: e' il modo piu' diretto di misurare
   la frequenza generata senza tirare in ballo una FFT. */
static uint32_t zero_crossings(const int16_t *b, uint32_t n)
{
    uint32_t count = 0;
    for (uint32_t i = 1; i < n; i++) {
        if ((b[i - 1] < 0 && b[i] >= 0) || (b[i - 1] >= 0 && b[i] < 0)) {
            count++;
        }
    }
    return count;
}

static uint32_t non_zero_samples(const int16_t *b, uint32_t n)
{
    uint32_t count = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (b[i] != 0) {
            count++;
        }
    }
    return count;
}

static int16_t peak(const int16_t *b, uint32_t n)
{
    int16_t max = 0;
    for (uint32_t i = 0; i < n; i++) {
        int16_t a = (int16_t)(b[i] < 0 ? -b[i] : b[i]);
        if (a > max) {
            max = a;
        }
    }
    return max;
}

void test_senza_tono_esce_silenzio(void)
{
    tone_fill(&g, buf, 1000);
    TEST_ASSERT_EQUAL_UINT32(0, non_zero_samples(buf, 1000));
}

void test_tono_di_centrale_e_425_hz(void)
{
    /* La frequenza si misura DENTRO un tratto acceso: su un secondo intero
       ricadrebbero anche le pause della cadenza, e il conto degli
       attraversamenti risulterebbe piu' basso senza che la frequenza sia
       sbagliata. Si usa il tratto lungo, 0,6 s, che da' il campione piu' ampio. */
    const uint32_t a_on  = TONE_SAMPLE_RATE * TONE_DIAL_A_ON_MS  / 1000;
    const uint32_t a_off = TONE_SAMPLE_RATE * TONE_DIAL_A_OFF_MS / 1000;
    const uint32_t b_on  = TONE_SAMPLE_RATE * TONE_DIAL_B_ON_MS  / 1000;

    tone_set(&g, TONE_DIAL);
    tone_fill(&g, buf, TONE_SAMPLE_RATE);

    const uint32_t attesi = 2 * TONE_DIAL_HZ * TONE_DIAL_B_ON_MS / 1000;
    uint32_t crossings = zero_crossings(buf + a_on + a_off, b_on);
    TEST_ASSERT_UINT32_WITHIN(2, attesi, crossings);
}

void test_tono_di_centrale_segue_la_cadenza_italiana(void)
{
    /* Tabella ITU-T E.180, riga Italia: 0,2 on / 0,2 off / 0,6 on / 1,0 off.
       Il periodo dura due secondi esatti. */
    const uint32_t a_on   = TONE_SAMPLE_RATE * TONE_DIAL_A_ON_MS   / 1000;
    const uint32_t a_off  = TONE_SAMPLE_RATE * TONE_DIAL_A_OFF_MS  / 1000;
    const uint32_t b_on   = TONE_SAMPLE_RATE * TONE_DIAL_B_ON_MS   / 1000;
    const uint32_t b_off  = TONE_SAMPLE_RATE * TONE_DIAL_B_OFF_MS  / 1000;

    tone_set(&g, TONE_DIAL);
    tone_fill(&g, buf, 2 * TONE_SAMPLE_RATE);

    uint32_t p = 0;
    TEST_ASSERT_TRUE(non_zero_samples(buf + p, a_on) > a_on * 9 / 10);
    p += a_on;
    TEST_ASSERT_EQUAL_UINT32(0, non_zero_samples(buf + p, a_off));
    p += a_off;
    TEST_ASSERT_TRUE(non_zero_samples(buf + p, b_on) > b_on * 9 / 10);
    p += b_on;
    TEST_ASSERT_EQUAL_UINT32(0, non_zero_samples(buf + p, b_off));
}

void test_tono_di_centrale_si_ripete(void)
{
    /* Il secondo periodo deve essere identico al primo: e' un invito a
       comporre, non un annuncio che finisce. */
    const uint32_t periodo = 2 * TONE_SAMPLE_RATE;
    const uint32_t a_on = TONE_SAMPLE_RATE * TONE_DIAL_A_ON_MS / 1000;

    tone_set(&g, TONE_DIAL);
    tone_fill(&g, buf, 2 * periodo);

    TEST_ASSERT_TRUE(non_zero_samples(buf + periodo, a_on) > a_on * 9 / 10);
}

void test_ampiezza_entro_il_fondo_scala(void)
{
    /* Un tono che clippa suona sporco e satura il percorso audio HFP. */
    tone_set(&g, TONE_DIAL);
    tone_fill(&g, buf, TONE_SAMPLE_RATE);
    TEST_ASSERT_TRUE(peak(buf, TONE_SAMPLE_RATE) <= TONE_AMPLITUDE);
    TEST_ASSERT_TRUE(peak(buf, TONE_SAMPLE_RATE) > TONE_AMPLITUDE / 2);
}

void test_fase_continua_tra_due_buffer(void)
{
    /* Il bug classico: azzerare la fase a ogni buffer produce un salto
       di ampiezza, cioe' un clic udibile a ogni riempimento. */
    const uint32_t half = 256;
    tone_set(&g, TONE_DIAL);

    tone_fill(&g, buf, half);
    tone_fill(&g, buf + half, half);

    /* Il salto al confine non deve superare quello tipico interno al buffer. */
    int32_t max_step_inside = 0;
    for (uint32_t i = 1; i < half; i++) {
        int32_t step = abs((int32_t)buf[i] - (int32_t)buf[i - 1]);
        if (step > max_step_inside) {
            max_step_inside = step;
        }
    }
    int32_t boundary_step = abs((int32_t)buf[half] - (int32_t)buf[half - 1]);
    TEST_ASSERT_TRUE(boundary_step <= max_step_inside);
}

void test_occupato_alterna_mezzo_secondo(void)
{
    tone_set(&g, TONE_BUSY);
    tone_fill(&g, buf, TONE_SAMPLE_RATE);

    const uint32_t half_sec = TONE_SAMPLE_RATE / 2;
    /* Primo mezzo secondo: suona. Secondo mezzo secondo: silenzio. */
    TEST_ASSERT_TRUE(non_zero_samples(buf, half_sec) > half_sec * 9 / 10);
    TEST_ASSERT_EQUAL_UINT32(0, non_zero_samples(buf + half_sec, half_sec));
}

void test_conferma_cifra_dura_50ms_poi_si_spegne_da_sola(void)
{
    /* Il beep di conferma e' un colpo solo: non deve restare acceso
       aspettando che qualcuno lo fermi. */
    const uint32_t fifty_ms = TONE_SAMPLE_RATE * TONE_KEYPRESS_MS / 1000;

    tone_set(&g, TONE_KEYPRESS);
    tone_fill(&g, buf, TONE_SAMPLE_RATE);

    TEST_ASSERT_TRUE(non_zero_samples(buf, fifty_ms) > fifty_ms * 9 / 10);
    TEST_ASSERT_EQUAL_UINT32(0, non_zero_samples(buf + fifty_ms, TONE_SAMPLE_RATE - fifty_ms));
    TEST_ASSERT_EQUAL(TONE_NONE, tone_current(&g));
}

void test_conferma_cifra_e_800_hz(void)
{
    const uint32_t fifty_ms = TONE_SAMPLE_RATE * TONE_KEYPRESS_MS / 1000;
    tone_set(&g, TONE_KEYPRESS);
    tone_fill(&g, buf, fifty_ms);

    /* 800 Hz per 50 ms = 40 cicli = 80 attraversamenti. */
    TEST_ASSERT_UINT32_WITHIN(2, 80, zero_crossings(buf, fifty_ms));
}

void test_cambio_tono_riparte_da_capo(void)
{
    /* Passando da occupato a libero, il nuovo tono non deve ereditare
       la fase di pausa del precedente. */
    tone_set(&g, TONE_BUSY);
    tone_fill(&g, buf, TONE_SAMPLE_RATE * 3 / 4);   /* dentro la pausa */

    tone_set(&g, TONE_DIAL);
    tone_fill(&g, buf, 1000);
    TEST_ASSERT_TRUE(non_zero_samples(buf, 1000) > 900);
}

void test_spegnere_il_tono_da_silenzio_immediato(void)
{
    tone_set(&g, TONE_DIAL);
    tone_fill(&g, buf, 100);
    tone_set(&g, TONE_NONE);
    tone_fill(&g, buf, 1000);
    TEST_ASSERT_EQUAL_UINT32(0, non_zero_samples(buf, 1000));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_senza_tono_esce_silenzio);
    RUN_TEST(test_tono_di_centrale_e_425_hz);
    RUN_TEST(test_tono_di_centrale_segue_la_cadenza_italiana);
    RUN_TEST(test_tono_di_centrale_si_ripete);
    RUN_TEST(test_ampiezza_entro_il_fondo_scala);
    RUN_TEST(test_fase_continua_tra_due_buffer);
    RUN_TEST(test_occupato_alterna_mezzo_secondo);
    RUN_TEST(test_conferma_cifra_dura_50ms_poi_si_spegne_da_sola);
    RUN_TEST(test_conferma_cifra_e_800_hz);
    RUN_TEST(test_cambio_tono_riparte_da_capo);
    RUN_TEST(test_spegnere_il_tono_da_silenzio_immediato);
    return UNITY_END();
}
