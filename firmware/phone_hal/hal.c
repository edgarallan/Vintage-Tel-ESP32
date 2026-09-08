/* Assemblaggio dell'interfaccia hardware e sorgente del tempo. */

#include "phone_hal.h"
#include "hal_priv.h"

#include "esp_timer.h"

uint32_t hal_now_ms(void)
{
    /* esp_timer conta in microsecondi da 64 bit; il core ragiona in ms a 32 bit,
       che bastano per 49 giorni di accensione continua. */
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static const hw_iface_t s_hw = {
    .set_led           = hal_led_set,
    .play_tone         = hal_out_play_tone,
    .bell_start        = hal_bell_start,
    .bell_stop         = hal_bell_stop,
    .display_state     = hal_out_display_state,
    .display_incoming  = hal_out_display_incoming,
    .bt_place_call     = hal_bt_place_call,
    .bt_answer         = hal_bt_answer,
    .bt_reject         = hal_bt_reject,
    .bt_hangup         = hal_bt_hangup,
    .bt_send_dtmf      = hal_bt_send_dtmf,
    .bt_is_connected   = hal_bt_is_connected,
    .now_ms            = hal_now_ms,
};

const hw_iface_t *phone_hal_init(QueueHandle_t evt_q)
{
    hal_out_init();
    hal_led_init();
    hal_bell_init();
    hal_bt_init(evt_q);
    hal_input_init(evt_q);
    return &s_hw;
}
