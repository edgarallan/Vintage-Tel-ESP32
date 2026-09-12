/* Dichiarazioni interne a hal/. Non incluso da nessun file fuori da questa cartella. */

#ifndef HAL_PRIV_H
#define HAL_PRIV_H

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
#define PIN_I2C_SDA      21
#define PIN_I2C_SCL      19

void hal_input_init(QueueHandle_t evt_q);
void hal_bell_init(void);
void hal_bell_start(void);
void hal_bell_stop(void);
void hal_bell_set_hz(int hz);   /* solo per la taratura */

void hal_led_init(void);
void hal_led_set(led_pattern_t pattern);

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
