/*
 * hal_out.c — display e toni.
 *
 * STATO: entrambi sono ancora segnaposto che scrivono a log. Il LED, che stava
 * qui, ora ha il suo driver in hal_led.c.
 *
 * Il log intanto e' utile davvero: rende visibili le transizioni di stato sul
 * monitor seriale, ed e' cio' con cui sono stati collaudati disco, gancio e
 * l'intero controllo chiamata prima che ci fosse qualcosa da guardare.
 */

#include "hal_priv.h"

#include "esp_log.h"

static const char *TAG = "hal_out";

void hal_out_init(void)
{
    ESP_LOGW(TAG, "display e toni sono segnaposto: hardware non ancora presente");
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
