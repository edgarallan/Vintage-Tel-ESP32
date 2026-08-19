#include "fake_hw.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

fake_hw_t g_fake;

static void record(const char *fmt, ...)
{
    if (g_fake.action_count >= FAKE_MAX_ACTIONS) {
        return;
    }
    va_list args;
    va_start(args, fmt);
    vsnprintf(g_fake.actions[g_fake.action_count], FAKE_ACTION_LEN, fmt, args);
    va_end(args);
    g_fake.action_count++;
}

static void f_set_led(led_pattern_t p)      { g_fake.last_led = p; record("led:%d", p); }
static void f_play_tone(tone_t t)           { g_fake.last_tone = t; record("tone:%d", t); }
static void f_bell_start(void)              { g_fake.bell_on = true;  record("bell_start"); }
static void f_bell_stop(void)               { g_fake.bell_on = false; record("bell_stop"); }

static void f_display_state(const char *state, const char *extra)
{
    snprintf(g_fake.display_state, sizeof(g_fake.display_state), "%s", state ? state : "");
    snprintf(g_fake.display_extra, sizeof(g_fake.display_extra), "%s", extra ? extra : "");
}

static void f_display_incoming(const char *name, const char *number)
{
    snprintf(g_fake.display_name, sizeof(g_fake.display_name), "%s", name ? name : "");
    record("incoming_shown:%s", number ? number : "");
}

static bool f_place_call(const char *number)
{
    record("place_call:%s", number);
    return g_fake.place_call_ok;
}

static bool f_answer(void)              { record("answer"); return true; }
static bool f_reject(void)              { record("reject"); return true; }
static bool f_hangup(void)              { record("hangup"); return true; }
static bool f_send_dtmf(char d)         { record("dtmf:%c", d); return true; }
static bool f_is_connected(void)        { return g_fake.bt_connected; }
static uint32_t f_now_ms(void)          { return g_fake.now_ms; }

static const hw_iface_t iface = {
    .set_led           = f_set_led,
    .play_tone         = f_play_tone,
    .bell_start        = f_bell_start,
    .bell_stop         = f_bell_stop,
    .display_state     = f_display_state,
    .display_incoming  = f_display_incoming,
    .bt_place_call     = f_place_call,
    .bt_answer         = f_answer,
    .bt_reject         = f_reject,
    .bt_hangup         = f_hangup,
    .bt_send_dtmf      = f_send_dtmf,
    .bt_is_connected   = f_is_connected,
    .now_ms            = f_now_ms,
};

void fake_hw_init(fake_hw_t *f)
{
    memset(f, 0, sizeof(*f));
    f->bt_connected  = true;
    f->place_call_ok = true;
    f->last_tone     = TONE_NONE;
}

const hw_iface_t *fake_hw_iface(void) { return &iface; }

bool fake_did(const fake_hw_t *f, const char *action)
{
    return fake_count(f, action) > 0;
}

uint8_t fake_count(const fake_hw_t *f, const char *action)
{
    uint8_t n = 0;
    for (uint8_t i = 0; i < f->action_count; i++) {
        if (strcmp(f->actions[i], action) == 0) {
            n++;
        }
    }
    return n;
}
