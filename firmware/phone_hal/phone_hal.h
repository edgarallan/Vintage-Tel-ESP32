/*
 * hal.h — l'altra meta' del confine dichiarato in core/hw_iface.h.
 *
 * core/ non sa cosa sia un GPIO; hal/ non sa cosa sia uno stato del telefono.
 * L'unico punto di contatto e' la struct hw_iface_t che phone_hal_init() riempie,
 * piu' la coda di eventi su cui le sorgenti hardware depositano i phone_ev_t.
 */

#ifndef PHONE_HAL_H
#define PHONE_HAL_H

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "hw_iface.h"
#include "phone_fsm.h"

/*
 * NOTA SUL NOME: questa funzione NON puo' chiamarsi "hal_init". Abilitando il
 * Bluetooth entra nel link libpp.a, la libreria binaria di PHY e coesistenza,
 * che esporta gia' un hal_init() suo. Il linker muore con un "multiple
 * definition of hal_init" che non nomina nessun file di questo progetto.
 * E' lo stesso inciampo, un piano piu' sotto, gia' documentato in
 * phone_hal/CMakeLists.txt per il nome della cartella.
 *
 * Inizializza i driver e restituisce l'interfaccia da passare a phone_init().
 * Gli eventi prodotti dall'hardware finiscono tutti in evt_q: e' l'unico
 * canale verso la macchina a stati, ed e' cio' che rende superfluo un mutex.
 */
const hw_iface_t *phone_hal_init(QueueHandle_t evt_q);

/* Millisecondi dall'avvio — la stessa sorgente di tempo per tutti i moduli. */
uint32_t hal_now_ms(void);

#endif /* PHONE_HAL_H */
