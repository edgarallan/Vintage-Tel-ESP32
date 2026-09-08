/*
 * hal_display.c — l'OLED SSD1306, 128x64, su I2C.
 *
 * Il pannello lo pilota esp_lcd, che in ESP-IDF ha gia' il driver SSD1306:
 * niente sequenza di inizializzazione scritta a mano, che e' il pezzo dove si
 * sbaglia di piu' e si perde piu' tempo a capire perche' lo schermo resta nero.
 * Qui restano le due cose che nessun driver puo' dare: un font e la scelta di
 * cosa mostrare.
 *
 * QUESTO SCHERMO SI GUARDA DA UN METRO E MEZZO, di sfuggita, mentre si passa
 * in corridoio. Non e' un terminale: se ci si mettono quattro righe piccole non
 * le legge nessuno. Ogni schermata ha percio' UN dato grande — il numero, il
 * nome di chi chiama — e al massimo una riga piccola di contorno.
 *
 * Il bus I2C e' condiviso con il codec WM8960 (0x1A): il display sta a 0x3C, e
 * il bus viene creato qui perche' per ora e' l'unico che lo usa. Quando
 * arrivera' il codec la creazione andra' spostata in un posto comune.
 */

#include <ctype.h>
#include <string.h>

#include "hal_priv.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "driver/i2c_master.h"
#include "esp_lcd_io_i2c.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_ssd1306.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "hal_disp";

#define OLED_ADDR      0x3C
#define OLED_W         128
#define OLED_H         64
#define OLED_PAGINE    (OLED_H / 8)
#define I2C_HZ         400000

/* Font 5x7: cinque colonne per carattere, un bit per pixel, il bit 0 in alto.
   Copre da spazio (32) a 'Z' (90); le minuscole si convertono in maiuscole,
   e tutto il resto diventa uno spazio. E' l'insieme che serve a un telefono:
   cifre, nomi della rubrica, "+" del prefisso internazionale. */
#define FONT_PRIMO     32
#define FONT_ULTIMO    90
#define FONT_W         5
#define FONT_PASSO     6   /* 5 colonne + 1 di respiro */

static const uint8_t s_font[FONT_ULTIMO - FONT_PRIMO + 1][FONT_W] = {
    {0x00,0x00,0x00,0x00,0x00}, /*   */  {0x00,0x00,0x5F,0x00,0x00}, /* ! */
    {0x00,0x07,0x00,0x07,0x00}, /* " */  {0x14,0x7F,0x14,0x7F,0x14}, /* # */
    {0x24,0x2A,0x7F,0x2A,0x12}, /* $ */  {0x23,0x13,0x08,0x64,0x62}, /* % */
    {0x36,0x49,0x55,0x22,0x50}, /* & */  {0x00,0x05,0x03,0x00,0x00}, /* ' */
    {0x00,0x1C,0x22,0x41,0x00}, /* ( */  {0x00,0x41,0x22,0x1C,0x00}, /* ) */
    {0x14,0x08,0x3E,0x08,0x14}, /* * */  {0x08,0x08,0x3E,0x08,0x08}, /* + */
    {0x00,0x50,0x30,0x00,0x00}, /* , */  {0x08,0x08,0x08,0x08,0x08}, /* - */
    {0x00,0x60,0x60,0x00,0x00}, /* . */  {0x20,0x10,0x08,0x04,0x02}, /* / */
    {0x3E,0x51,0x49,0x45,0x3E}, /* 0 */  {0x00,0x42,0x7F,0x40,0x00}, /* 1 */
    {0x42,0x61,0x51,0x49,0x46}, /* 2 */  {0x21,0x41,0x45,0x4B,0x31}, /* 3 */
    {0x18,0x14,0x12,0x7F,0x10}, /* 4 */  {0x27,0x45,0x45,0x45,0x39}, /* 5 */
    {0x3C,0x4A,0x49,0x49,0x30}, /* 6 */  {0x01,0x71,0x09,0x05,0x03}, /* 7 */
    {0x36,0x49,0x49,0x49,0x36}, /* 8 */  {0x06,0x49,0x49,0x29,0x1E}, /* 9 */
    {0x00,0x36,0x36,0x00,0x00}, /* : */  {0x00,0x56,0x36,0x00,0x00}, /* ; */
    {0x08,0x14,0x22,0x41,0x00}, /* < */  {0x14,0x14,0x14,0x14,0x14}, /* = */
    {0x00,0x41,0x22,0x14,0x08}, /* > */  {0x02,0x01,0x51,0x09,0x06}, /* ? */
    {0x32,0x49,0x79,0x41,0x3E}, /* @ */  {0x7E,0x11,0x11,0x11,0x7E}, /* A */
    {0x7F,0x49,0x49,0x49,0x36}, /* B */  {0x3E,0x41,0x41,0x41,0x22}, /* C */
    {0x7F,0x41,0x41,0x22,0x1C}, /* D */  {0x7F,0x49,0x49,0x49,0x41}, /* E */
    {0x7F,0x09,0x09,0x09,0x01}, /* F */  {0x3E,0x41,0x49,0x49,0x7A}, /* G */
    {0x7F,0x08,0x08,0x08,0x7F}, /* H */  {0x00,0x41,0x7F,0x41,0x00}, /* I */
    {0x20,0x40,0x41,0x3F,0x01}, /* J */  {0x7F,0x08,0x14,0x22,0x41}, /* K */
    {0x7F,0x40,0x40,0x40,0x40}, /* L */  {0x7F,0x02,0x0C,0x02,0x7F}, /* M */
    {0x7F,0x04,0x08,0x10,0x7F}, /* N */  {0x3E,0x41,0x41,0x41,0x3E}, /* O */
    {0x7F,0x09,0x09,0x09,0x06}, /* P */  {0x3E,0x41,0x51,0x21,0x5E}, /* Q */
    {0x7F,0x09,0x19,0x29,0x46}, /* R */  {0x46,0x49,0x49,0x49,0x31}, /* S */
    {0x01,0x01,0x7F,0x01,0x01}, /* T */  {0x3F,0x40,0x40,0x40,0x3F}, /* U */
    {0x1F,0x20,0x40,0x20,0x1F}, /* V */  {0x3F,0x40,0x38,0x40,0x3F}, /* W */
    {0x63,0x14,0x08,0x14,0x63}, /* X */  {0x07,0x08,0x70,0x08,0x07}, /* Y */
    {0x61,0x51,0x49,0x45,0x43}, /* Z */
};

static esp_lcd_panel_handle_t s_pannello;
static uint8_t s_fb[OLED_W * OLED_PAGINE];   /* 1 bit per pixel, per pagine */
static bool s_pronto;

/*
 * Il framebuffer e il bus I2C hanno due scrittori: il task del telefono, che
 * ridisegna a ogni cambio di stato, e un timer che tiene aggiornato
 * l'indicatore del collegamento. Da qui il mutex.
 *
 * Non contraddice l'invariante "niente mutex" del progetto: quello riguarda
 * lo STATO DEL TELEFONO, che ha un proprietario solo ed e' per questo che non
 * va protetto. Qui si tratta di due task che condividono un bus fisico e un
 * buffer di pixel, che e' esattamente il caso per cui i mutex esistono.
 */
static SemaphoreHandle_t  s_lock;
static esp_timer_handle_t s_orologio;

/* Cosa c'e' scritto adesso, per poterlo ridisegnare da solo. */
static char s_stato[16];
static char s_extra[24];
static bool s_schermata_stato;   /* falsa mentre mostra una chiamata in arrivo */
static bool s_bt_mostrato;

static void controlla_collegamento(void *arg);

static void pulisci(void)
{
    memset(s_fb, 0, sizeof(s_fb));
}

static void pixel(int x, int y)
{
    if (x < 0 || x >= OLED_W || y < 0 || y >= OLED_H) {
        return;
    }
    s_fb[(y / 8) * OLED_W + x] |= (uint8_t)(1u << (y % 8));
}

/* Disegna un carattere ingrandito di 'scala' volte. Il 5x7 ingrandito due
   volte diventa 10x14, che da un metro e mezzo si legge; a scala 1 e' una
   riga di contorno, non un'informazione principale. */
static void glifo(int x, int y, char c, int scala)
{
    c = (char)toupper((unsigned char)c);
    if (c < FONT_PRIMO || c > FONT_ULTIMO) {
        c = ' ';
    }
    const uint8_t *col = s_font[c - FONT_PRIMO];

    for (int i = 0; i < FONT_W; i++) {
        for (int r = 0; r < 7; r++) {
            if (!(col[i] & (1u << r))) {
                continue;
            }
            for (int dx = 0; dx < scala; dx++) {
                for (int dy = 0; dy < scala; dy++) {
                    pixel(x + i * scala + dx, y + r * scala + dy);
                }
            }
        }
    }
}

static int larghezza(const char *s, int scala)
{
    return (int)strlen(s) * FONT_PASSO * scala;
}

static void testo(int x, int y, const char *s, int scala)
{
    for (; *s; s++) {
        glifo(x, y, *s, scala);
        x += FONT_PASSO * scala;
    }
}

static void testo_centrato(int y, const char *s, int scala)
{
    testo((OLED_W - larghezza(s, scala)) / 2, y, s, scala);
}

/* Il dato principale, grande quanto puo' stare. Un numero di dieci cifre a
   scala 2 occupa 120 pixel dei 128 disponibili: sopra le dieci cifre si
   ripiega su scala 1 invece di troncare, perche' meta' numero di telefono e'
   peggio di un numero piccolo. */
static void protagonista(int y, const char *s)
{
    testo_centrato(y, s, larghezza(s, 2) <= OLED_W ? 2 : 1);
}

static void mostra(void)
{
    if (!s_pronto) {
        return;
    }
    esp_lcd_panel_draw_bitmap(s_pannello, 0, 0, OLED_W, OLED_H, s_fb);
}

void hal_display_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    configASSERT(s_lock);

    i2c_master_bus_config_t bus = {
        .i2c_port          = I2C_NUM_0,
        .sda_io_num        = PIN_I2C_SDA,
        .scl_io_num        = PIN_I2C_SCL,
        .clk_source        = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus_h = NULL;
    esp_err_t err = i2c_new_master_bus(&bus, &bus_h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "bus I2C non creato: %s", esp_err_to_name(err));
        return;
    }

    esp_lcd_panel_io_i2c_config_t io = {
        .dev_addr            = OLED_ADDR,
        .scl_speed_hz        = I2C_HZ,
        .control_phase_bytes = 1,
        .dc_bit_offset       = 6,
        .lcd_cmd_bits        = 8,
        .lcd_param_bits      = 8,
    };
    esp_lcd_panel_io_handle_t io_h = NULL;
    err = esp_lcd_new_panel_io_i2c(bus_h, &io, &io_h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "display non raggiunto a 0x%02X: %s",
                 OLED_ADDR, esp_err_to_name(err));
        return;
    }

    esp_lcd_panel_ssd1306_config_t vendor = { .height = OLED_H };
    esp_lcd_panel_dev_config_t dev = {
        .bits_per_pixel = 1,
        .reset_gpio_num = -1,
        .vendor_config  = &vendor,
    };
    err = esp_lcd_new_panel_ssd1306(io_h, &dev, &s_pannello);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "pannello SSD1306 non creato: %s", esp_err_to_name(err));
        return;
    }

    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_pannello));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_pannello));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_pannello, true));
    s_pronto = true;

    const esp_timer_create_args_t targs = {
        .callback = controlla_collegamento,
        .name     = "disp_bt",
    };
    ESP_ERROR_CHECK(esp_timer_create(&targs, &s_orologio));
    ESP_ERROR_CHECK(esp_timer_start_periodic(s_orologio, 1000 * 1000));

    ESP_LOGI(TAG, "SSD1306 %dx%d a 0x%02X su SDA%d/SCL%d",
             OLED_W, OLED_H, OLED_ADDR, PIN_I2C_SDA, PIN_I2C_SCL);
}

/* Traduce il nome tecnico dello stato in una parola che significhi qualcosa
   per chi guarda. Il core non deve saperne niente: per lui restano IDLE e
   DIALING, che e' giusto perche' sono nomi di stati e non messaggi. */
static const char *parola(const char *stato)
{
    if (!stato)                          return "";
    if (!strcmp(stato, "IDLE"))          return "PRONTO";
    if (!strcmp(stato, "DIALING"))       return "COMPONI";
    if (!strcmp(stato, "CALLING"))       return "CHIAMO";
    if (!strcmp(stato, "IN_CALL"))       return "IN CHIAMATA";
    if (!strcmp(stato, "ERROR"))         return "GUASTO";
    return stato;
}

/* Disegna la schermata di stato da cio' che e' memorizzato. Chi la chiama
   deve gia' avere il lock. */
static void disegna_stato(void)
{
    const bool ha_numero = (s_extra[0] != '\0');
    const char *state = s_stato;
    const char *extra = s_extra;

    s_bt_mostrato = hal_bt_is_connected();

    pulisci();

    if (ha_numero) {
        /* Il numero e' il dato che conta: sta grande al centro, e lo stato
           diventa l'etichetta piccola in alto. */
        testo_centrato(0, parola(state), 1);
        protagonista(26, extra);
    } else {
        testo_centrato(24, parola(state), 2);
        /* A riposo la domanda vera e' un'altra: il telefono funziona? Senza
           cellulare collegato questo apparecchio non puo' fare niente, e
           saperlo prima di sollevare la cornetta evita di scoprirlo dopo aver
           composto un numero intero. */
        if (!strcmp(state, "IDLE")) {
            testo_centrato(56, s_bt_mostrato ? "COLLEGATO"
                                             : "NESSUN CELLULARE", 1);
        }
    }
    mostra();
}

void hal_display_state(const char *state, const char *extra)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    snprintf(s_stato, sizeof(s_stato), "%s", state ? state : "");
    snprintf(s_extra, sizeof(s_extra), "%s", extra ? extra : "");
    s_schermata_stato = true;
    disegna_stato();
    xSemaphoreGive(s_lock);
}

/*
 * A riposo il display dichiara se il cellulare e' collegato, ma quel
 * collegamento va e viene senza che la macchina a stati cambi stato: si
 * connette una decina di secondi DOPO l'avvio, e cade quando il telefono esce
 * di casa. Senza questo controllo lo schermo resterebbe fermo a com'erano le
 * cose all'accensione — e un'informazione falsa e' peggio di nessuna
 * informazione, perche' chi legge "nessun cellulare" rinuncia a telefonare.
 *
 * Si ridisegna solo quando il collegamento cambia davvero: riscrivere mille
 * volte lo stesso schermo terrebbe occupato il bus per niente.
 */
static void controlla_collegamento(void *arg)
{
    (void)arg;
    if (!s_schermata_stato || strcmp(s_stato, "IDLE") != 0) {
        return;
    }
    if (hal_bt_is_connected() == s_bt_mostrato) {
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    disegna_stato();
    xSemaphoreGive(s_lock);
}

void hal_display_incoming(const char *name, const char *number)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_schermata_stato = false;
    pulisci();
    testo_centrato(0, "CHIAMATA DA", 1);

    const char *chi = (name && *name) ? name : NULL;
    if (chi) {
        /* Se la rubrica conosce il nome, il nome e' il protagonista e il
           numero resta sotto come conferma. */
        protagonista(22, chi);
        if (number && *number) {
            testo_centrato(56, number, 1);
        }
    } else {
        protagonista(26, (number && *number) ? number : "SCONOSCIUTO");
    }
    mostra();
    xSemaphoreGive(s_lock);
}
