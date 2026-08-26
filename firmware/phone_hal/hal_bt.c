/*
 * hal_bt.c — vivavoce Bluetooth HFP verso il cellulare.
 *
 * STATO: segnaposto. La configurazione dello stack e' gia' decisa e scritta
 * in sdkconfig.defaults (Bluedroid, BR/EDR only, ruolo Hands-Free, percorso
 * audio vHCI, Wide Band Speech, PBAC), ma il codice di connessione non c'e'
 * ancora: e' l'ultimo pezzo del bring-up, dopo che disco, gancio e campanello
 * sono stati verificati.
 *
 * bt_is_connected() ritorna false, quindi la macchina a stati si comporta
 * correttamente come un telefono senza linea: si puo' comporre un numero e
 * vedere il tono di occupato, che e' proprio cio' che serve per collaudare
 * il disco senza un cellulare accoppiato.
 *
 * Il MAC del cellulare non va mai nei sorgenti ne' stampato per intero a log.
 */

#include "hal_priv.h"

#include "esp_log.h"

static const char *TAG = "hal_bt";

void hal_bt_init(void)
{
    ESP_LOGW(TAG, "HFP non ancora implementato: il telefono risulta scollegato");
}

bool hal_bt_place_call(const char *number)
{
    ESP_LOGI(TAG, "chiamata richiesta verso %s (ignorata: HFP assente)",
             number ? number : "");
    return false;
}

bool hal_bt_answer(void)   { ESP_LOGI(TAG, "risposta (ignorata)");   return false; }
bool hal_bt_reject(void)   { ESP_LOGI(TAG, "rifiuto (ignorato)");    return false; }
bool hal_bt_hangup(void)   { ESP_LOGI(TAG, "riaggancio (ignorato)"); return false; }

bool hal_bt_send_dtmf(char digit)
{
    ESP_LOGI(TAG, "DTMF '%c' (ignorato)", digit);
    return false;
}

bool hal_bt_is_connected(void) { return false; }
