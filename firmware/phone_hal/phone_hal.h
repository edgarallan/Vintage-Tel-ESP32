/*
 * hal.h — l'altra meta' del confine dichiarato in core/hw_iface.h.
 *
 * core/ non sa cosa sia un GPIO; hal/ non sa cosa sia uno stato del telefono.
 * L'unico punto di contatto e' la struct hw_iface_t che hal_init() riempie,
 * piu' la coda di eventi su cui le sorgenti hardware depositano i phone_ev_t.
 */

#ifndef PHONE_HAL_H
#define PHONE_HAL_H

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "hw_iface.h"
#include "phone_fsm.h"

/*
 * Inizializza i driver e restituisce l'interfaccia da passare a phone_init().
 * Gli eventi prodotti dall'hardware finiscono tutti in evt_q: e' l'unico
 * canale verso la macchina a stati, ed e' cio' che rende superfluo un mutex.
 */
const hw_iface_t *hal_init(QueueHandle_t evt_q);

/* Millisecondi dall'avvio — la stessa sorgente di tempo per tutti i moduli. */
uint32_t hal_now_ms(void);

#endif /* PHONE_HAL_H */
