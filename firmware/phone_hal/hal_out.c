/*
 * hal_out.c — i toni di sistema.
 *
 * STATO: segnaposto che scrive a log. La forma d'onda esiste gia' ed e'
 * testata in core/tones.c: qui manchera' solo il travaso del buffer nel driver
 * I2S, quando ci sara' il codec.
 *
 * LED e display, che stavano qui, hanno ora un driver ciascuno in hal_led.c e
 * hal_display.c.
 */

#include "hal_priv.h"

#include "esp_log.h"

static const char *TAG = "hal_out";

void hal_out_init(void)
{
    ESP_LOGW(TAG, "i toni sono un segnaposto: manca il codec");
}

void hal_out_play_tone(tone_t tone)
{
    /* TODO: generazione via I2S verso il WM8960. La forma d'onda esiste gia'
       ed e' testata: core/tones.c, funzione tone_fill(). Qui manchera' solo
       il travaso del buffer nel driver I2S. */
    static const char *names[] = { "silenzio", "libero", "occupato", "tasto" };
    ESP_LOGI(TAG, "tono -> %s", tone <= TONE_KEYPRESS ? names[tone] : "?");
}

