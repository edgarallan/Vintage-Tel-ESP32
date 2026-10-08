#ifndef MAIN_RIAVVIO_H
#define MAIN_RIAVVIO_H

/* Scrive a log il motivo dell'avvio e, se era un riavvio anomalo, lo conta in
   NVS. Va chiamata dopo nvs_flash_init(). */
void riavvio_registra(void);

#endif /* MAIN_RIAVVIO_H */
