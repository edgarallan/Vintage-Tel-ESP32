/*
 * hal_out.c — LED di stato, display e toni.
 *
 * STATO: tutti e tre gli attuatori sono ancora segnaposto che scrivono a log.
 * Non e' pigrizia, e' che l'hardware non c'e': WS2812 e OLED sono nell'ordine
 * AliExpress, il codec WM8960 in quello Amazon. Scrivere driver che non si
 * possono provare significa scrivere bug che si scoprono fra tre settimane.
 *
 * Il log intanto e' utile davvero: rende visibili le transizioni di stato sul
 * monitor seriale, che e' esattamente cio' che serve per collaudare disco e
 * gancio prima che arrivi il resto.
 */

#include "hal_priv.h"

#include "esp_log.h"

static const char *TAG = "hal_out";

static const char *led_name(led_pattern_t p)
{
    switch (p) {
    case LED_IDLE:    return "IDLE (blu, respiro)";
    case LED_DIALING: return "DIALING (bianco)";
    case LED_CALLING: return "CALLING (giallo)";
    case LED_RINGING: return "RINGING (rosso lampeggio)";
    case LED_IN_CALL: return "IN_CALL (verde)";
    case LED_ERROR:   return "ERROR (rosso fisso)";
    }
    return "?";
}

void hal_out_init(void)
{
    ESP_LOGW(TAG, "LED, display e toni sono segnaposto: hardware non ancora presente");
}

void hal_out_set_led(led_pattern_t pattern)
{
    /* TODO: WS2812 su GPIO 27 via RMT, quando il modulo arriva. */
    ESP_LOGI(TAG, "LED -> %s", led_name(pattern));
}

void hal_out_play_tone(tone_t tone)
{
    /* TODO: generazione via I2S verso il WM8960. La forma d'onda esiste gia'
       ed e' testata: core/tones.c, funzione tone_fill(). Qui manchera' solo
       il travaso del buffer nel driver I2S. */
    static const char *names[] = { "silenzio", "libero", "occupato", "tasto" };
    ESP_LOGI(TAG, "tono -> %s", tone <= TONE_KEYPRESS ? names[tone] : "?");
}

void hal_out_display_state(const char *state, const char *extra)
{
    /* TODO: SSD1306 su I2C 21/19, indirizzo 0x3C. */
    ESP_LOGI(TAG, "display: %s%s%s", state ? state : "",
             (extra && *extra) ? " | " : "", extra ? extra : "");
}

void hal_out_display_incoming(const char *name, const char *number)
{
    ESP_LOGI(TAG, "display: chiamata da %s <%s>",
             (name && *name) ? name : "sconosciuto", number ? number : "");
}
