/* Dichiarazioni interne a hal/. Non incluso da nessun file fuori da questa cartella. */

#ifndef HAL_PRIV_H
#define HAL_PRIV_H

#include "driver/i2c_master.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "hw_iface.h"

/* --- Mappa GPIO ------------------------------------------------------------
 * Fonte di verita': hardware/pinout.md. Se questi numeri divergono da quel
 * documento e' un bug, e va corretto uno dei due nello stesso commit.
 */
#define PIN_DIAL_PULSE   4
#define PIN_DIAL_NSI     32
#define PIN_HOOK         18
#define PIN_BELL_IN1     13
#define PIN_BELL_IN2     14
#define PIN_BUTTON       23
#define PIN_LED_WS2812   27
#define PIN_I2S_BCLK     26
#define PIN_I2S_WS       25
/* Sul connettore della Waveshare WM8960 Audio Board le sigle TX e RX sono dal
   punto di vista DELLA SCHEDA, non del nostro: e' lei che riceve su RXSDA.
   Leggerle al contrario costa una serata — il DAC non riceve niente e si sente
   solo un fruscio, perche' si sta pilotando un'uscita del codec. */
#define PIN_I2S_DIN      33   /* al pin TXSDA: il codec trasmette, il mic */
#define PIN_I2S_DOUT     22   /* al pin RXSDA: il codec riceve, la capsula */
/* L'MCLK su ESP32 puo' uscire SOLO da GPIO 0, 1 o 3, e 1/3 sono la console.
   GPIO 0 e' strapping ma viene campionato solo al reset: vedi pinout.md. */
#define PIN_I2S_MCLK     0

#define PIN_I2C_SDA      21
#define PIN_I2C_SCL      19

void hal_input_init(QueueHandle_t evt_q);
void hal_bell_init(void);
void hal_bell_start(void);
void hal_bell_stop(void);
void hal_bell_set_hz(int hz);   /* solo per la taratura */

void hal_led_init(void);
void hal_led_set(led_pattern_t pattern);

i2c_master_bus_handle_t hal_i2c_bus(void);   /* creato una volta, condiviso */

void      hal_codec_init(void);
bool      hal_codec_presente(void);
esp_err_t hal_codec_write(uint8_t reg, uint16_t val);

void hal_audio_init(void);
bool hal_audio_play(const int16_t *mono, size_t n);

void hal_display_init(void);
void hal_display_state(const char *state, const char *extra);
void hal_display_incoming(const char *name, const char *number);

void hal_out_init(void);
void hal_out_play_tone(tone_t tone);

void hal_bt_init(QueueHandle_t evt_q);
bool hal_bt_place_call(const char *number);
bool hal_bt_answer(void);
bool hal_bt_reject(void);
bool hal_bt_hangup(void);
bool hal_bt_send_dtmf(char digit);
bool hal_bt_is_connected(void);

#endif /* HAL_PRIV_H */
