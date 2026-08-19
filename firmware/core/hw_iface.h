/*
 * hw_iface.h — il confine tra logica e hardware.
 *
 * Questo file e' il motivo per cui core/ e' testabile su un PC senza ESP32:
 * la logica non chiama mai un driver, chiama questa struct. In produzione
 * la riempie hal/ con le funzioni ESP-IDF; nei test la riempie un finto
 * dispositivo che registra le chiamate ricevute.
 *
 * Regola non negoziabile: nessun file sotto core/ include mai esp_*.h.
 */

#ifndef CORE_HW_IFACE_H
#define CORE_HW_IFACE_H

#include <stdbool.h>
#include <stdint.h>

/* Colori del LED di stato, gia' interpretati: il core non conosce l'RGB. */
typedef enum {
    LED_IDLE,      /* blu, respiro lento */
    LED_DIALING,   /* bianco fisso */
    LED_CALLING,   /* giallo pulsante */
    LED_RINGING,   /* rosso lampeggiante */
    LED_IN_CALL,   /* verde, respiro lento */
    LED_ERROR,     /* rosso fisso */
} led_pattern_t;

/* Toni di sistema italiani (ITU-T E.180 + standard Telecom). */
typedef enum {
    TONE_NONE,
    TONE_DIAL,      /* 425 Hz continuo */
    TONE_BUSY,      /* 425 Hz, 500 ms on / 500 ms off */
    TONE_KEYPRESS,  /* 800 Hz, 50 ms */
} tone_t;

/*
 * Operazioni che la logica puo' chiedere all'hardware.
 * Ogni puntatore puo' essere NULL: il core deve tollerarlo (display e LED
 * sono opzionali, e nei test spesso se ne riempie solo una parte).
 */
typedef struct {
    void (*set_led)(led_pattern_t pattern);
    void (*play_tone)(tone_t tone);

    void (*bell_start)(void);
    void (*bell_stop)(void);

    void (*display_state)(const char *state, const char *extra);
    void (*display_incoming)(const char *name, const char *number);

    /* Ritornano true se il comando e' stato accettato dal telefono accoppiato. */
    bool (*bt_place_call)(const char *number);
    bool (*bt_answer)(void);
    bool (*bt_reject)(void);
    bool (*bt_hangup)(void);
    bool (*bt_send_dtmf)(char digit);
    bool (*bt_is_connected)(void);

    /* Millisecondi dall'avvio. Nei test e' un contatore pilotabile a mano,
       ed e' cio' che rende i test deterministici invece che a tempo reale. */
    uint32_t (*now_ms)(void);
} hw_iface_t;

#endif /* CORE_HW_IFACE_H */
