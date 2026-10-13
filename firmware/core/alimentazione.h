/*
 * alimentazione.h — quando smettere di ripartire.
 *
 * Con la batteria quasi vuota l'ESP32 entra in un circolo: si avvia, accende
 * il Bluetooth, il picco di corrente fa crollare la tensione, il rilevatore di
 * calo lo riavvia. Fra il 10 e il 13/10/2026 e' successo 18.363 volte, una ogni
 * quindici secondi: la cella si svuota fino in fondo e a ogni giro si scrive
 * in flash. Qui c'e' solo la regola, testabile sul PC; il sonno sta in
 * main/riavvio.c.
 */

#ifndef CORE_ALIMENTAZIONE_H
#define CORE_ALIMENTAZIONE_H

#include <stdbool.h>
#include <stdint.h>

/* Riavvii di fila per calo di tensione dopo i quali si smette di insistere.
   Uno puo' essere un picco isolato; tre di fila sono la batteria. */
#define ALIM_SERIE_MAX 3

/* La serie di riavvii per calo di tensione dopo l'avvio corrente: cresce se
   anche questo e' un calo, altrimenti riparte da zero. Satura, non trabocca. */
uint8_t alim_serie(uint8_t serie, bool calo_di_tensione);

/* Vero se e' il momento di dormire invece di avviare il telefono. */
bool alim_deve_dormire(uint8_t serie);

#endif /* CORE_ALIMENTAZIONE_H */
