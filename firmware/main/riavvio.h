#ifndef MAIN_RIAVVIO_H
#define MAIN_RIAVVIO_H

/* Scrive a log il motivo dell'avvio e, se era un riavvio anomalo, lo conta in
   NVS. Se e' il terzo calo di tensione di fila NON RITORNA: manda l'ESP32 in
   sonno profondo per non alimentare il circolo dei riavvii. Va chiamata dopo
   nvs_flash_init() e prima di accendere il Bluetooth. */
void riavvio_registra(void);

#endif /* MAIN_RIAVVIO_H */
