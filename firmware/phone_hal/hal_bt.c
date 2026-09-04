/*
 * hal_bt.c — vivavoce Bluetooth HFP verso il cellulare.
 *
 * L'ESP32 fa da Hands-Free unit, il cellulare da Audio Gateway: e' lo stesso
 * ruolo di un vivavoce da auto, ed e' il motivo per cui il cellulare accetta
 * di girargli le chiamate.
 *
 * QUESTO MODULO NON PORTA ANCORA L'AUDIO. Fa il controllo chiamata —
 * connessione, squillo, numero del chiamante, risposta, rifiuto, riaggancio,
 * DTMF — mentre la voce arrivera' col codec, in un passo successivo. La
 * separazione e' voluta: il controllo chiamata non dipende da nessun
 * componente hardware, quindi si puo' verificare subito, e quando l'audio si
 * aggiungera' i suoi difetti non si confonderanno con quelli del controllo.
 *
 * ISOLA DI THREAD. I callback di Bluedroid girano nel suo task, non nel task
 * del telefono. Qui dentro non si tocca mai lo stato della macchina a stati:
 * si deposita un phone_ev_t nella coda, esattamente come fa l'ISR dei GPIO.
 * E' lo stesso motivo per cui il progetto non ha mutex.
 *
 * Il MAC del cellulare non compare mai nei sorgenti, e nei log ne escono solo
 * gli ultimi due byte: basta a distinguere due apparecchi in fase di collaudo
 * e non e' un identificativo utile a nessun altro.
 */

#include <string.h>

#include "phone_hal.h"
#include "hal_priv.h"

#include "esp_bt.h"
#include "esp_bt_device.h"
#include "esp_bt_main.h"
#include "esp_gap_bt_api.h"
#include "esp_hf_client_api.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"

static const char *TAG = "hal_bt";

/* Nome con cui il telefono compare nella lista di accoppiamento del cellulare. */
#define BT_DEV_NAME  "Vintage Tel"

/*
 * Quanti RING aspettare prima di annunciare una chiamata senza numero.
 *
 * Nella sequenza HFP il numero arriva con un +CLIP che segue il RING di
 * qualche millisecondo, quindi annunciare al CLIP costa un ritardo
 * impercettibile e fa arrivare il nome del chiamante al display fin dal primo
 * squillo. Ma un chiamante anonimo il +CLIP non lo manda mai, e aspettarlo
 * per sempre significherebbe un telefono che non suona: al secondo RING si
 * annuncia comunque, senza numero.
 */
#define RING_SENZA_NUMERO  2

static QueueHandle_t s_evt_q;

/* MAC dell'ultimo cellulare accoppiato, ricordato fra un riavvio e l'altro.
   Senza, dopo ogni riavvio bisogna riconnettersi a mano dal cellulare: l'AG
   non ripropone la connessione da solo, e un telefono di casa che pretende un
   gesto sul cellulare a ogni black-out non e' un telefono di casa. */
#define NVS_SPAZIO   "vtel"
#define NVS_CHIAVE   "peer"

static esp_bd_addr_t s_peer;
static bool          s_ho_peer;
static esp_timer_handle_t s_riconn;
static uint32_t           s_tentativi;

/* Connessione di livello servizio: sotto questa soglia l'AG non risponde ai
   comandi, quindi e' lei e non la connessione RFCOMM a dire "collegato". */
static bool s_slc;

/* Indicatori di chiamata come li riporta l'AG. */
static bool                       s_call;    /* c'e' una chiamata in corso */
static esp_hf_call_setup_status_t s_setup;   /* squillo o composizione */

/* Annuncio della chiamata entrante alla macchina a stati. */
static bool    s_in_corso;    /* c'e' qualcosa: squillo o conversazione */
static bool    s_annunciata;  /* l'evento e' gia' stato mandato */
static uint8_t s_ring;        /* RING ricevuti per questa chiamata */
static char    s_clip[PB_NUMBER_LEN];

/* Ultimi due byte del MAC: identifica l'apparecchio senza esporlo. */
static const char *mac_corto(const uint8_t *bda)
{
    static char buf[12];
    snprintf(buf, sizeof(buf), "..%02X:%02X", bda[4], bda[5]);
    return buf;
}

static void send_ev(phone_ev_type_t type, const char *caller)
{
    phone_ev_t ev = {
        .type   = type,
        .now_ms = (uint32_t)(esp_timer_get_time() / 1000),
    };
    if (caller) {
        snprintf(ev.caller, sizeof(ev.caller), "%s", caller);
    }
    xQueueSend(s_evt_q, &ev, 0);
}

static void azzera_chiamata(void)
{
    s_in_corso   = false;
    s_annunciata = false;
    s_ring       = 0;
    s_clip[0]    = '\0';
}

/* Annuncia lo squillo appena si sa chi chiama, o appena e' chiaro che non si
   sapra'. Chiamata piu' volte: e' l'annuncio a proteggersi dai doppioni,
   perche' la macchina a stati ignora un secondo EV_INCOMING_CALL mentre
   squilla e il nome del chiamante andrebbe perso. */
static void annuncia_se_pronto(void)
{
    if (!s_in_corso || s_annunciata) {
        return;
    }
    if (s_clip[0] == '\0' && s_ring < RING_SENZA_NUMERO) {
        return;
    }
    s_annunciata = true;
    ESP_LOGI(TAG, "chiamata in arrivo da %s",
             s_clip[0] ? s_clip : "numero sconosciuto");
    send_ev(EV_INCOMING_CALL, s_clip);
}

/* Quando entrambi gli indicatori tornano a riposo la chiamata e' finita, da
   qualunque lato sia stata chiusa. */
static void valuta_fine(void)
{
    if (s_call || s_setup != ESP_HF_CALL_SETUP_STATUS_IDLE) {
        s_in_corso = true;
        return;
    }
    if (!s_in_corso) {
        return;
    }
    azzera_chiamata();
    ESP_LOGI(TAG, "chiamata terminata");
    send_ev(EV_CALL_ENDED, NULL);
}

static void carica_peer(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_SPAZIO, NVS_READONLY, &h) != ESP_OK) {
        return;
    }
    size_t len = sizeof(s_peer);
    if (nvs_get_blob(h, NVS_CHIAVE, s_peer, &len) == ESP_OK &&
        len == sizeof(s_peer)) {
        s_ho_peer = true;
        ESP_LOGI(TAG, "cellulare noto: %s", mac_corto(s_peer));
    }
    nvs_close(h);
}

static void salva_peer(const uint8_t *bda)
{
    if (s_ho_peer && memcmp(s_peer, bda, sizeof(s_peer)) == 0) {
        return;   /* gia' quello: non si consuma la flash per nulla */
    }
    memcpy(s_peer, bda, sizeof(s_peer));
    s_ho_peer = true;

    nvs_handle_t h;
    if (nvs_open(NVS_SPAZIO, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGW(TAG, "non riesco a ricordare il cellulare");
        return;
    }
    if (nvs_set_blob(h, NVS_CHIAVE, s_peer, sizeof(s_peer)) == ESP_OK) {
        nvs_commit(h);
        ESP_LOGI(TAG, "cellulare ricordato: %s", mac_corto(s_peer));
    }
    nvs_close(h);
}

/* Ritenta la connessione finche' il cellulare non torna a portata. Gira su un
   timer e non su un task: e' un tentativo ogni dieci secondi, non vale uno
   stack dedicato.

   L'esito di ogni tentativo va a log. Buttarlo via rendeva il difetto
   invisibile: un telefono che non si ricollega e non dice perche' costringe a
   indovinare fra "non ci prova", "ci prova e il cellulare rifiuta" e "ci prova
   e lo stack e' occupato". */
static void riprova_connessione(void *arg)
{
    (void)arg;
    if (s_slc || !s_ho_peer) {
        return;
    }

    s_tentativi++;
    const esp_err_t err = esp_hf_client_connect(s_peer);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "tentativo %lu di ricollegarmi a %s: accettato",
                 (unsigned long)s_tentativi, mac_corto(s_peer));
    } else {
        ESP_LOGW(TAG, "tentativo %lu di ricollegarmi a %s: %s",
                 (unsigned long)s_tentativi, mac_corto(s_peer),
                 esp_err_to_name(err));
    }
}

static void hf_cb(esp_hf_client_cb_event_t event, esp_hf_client_cb_param_t *param)
{
    switch (event) {
    case ESP_HF_CLIENT_CONNECTION_STATE_EVT:
        switch (param->conn_stat.state) {
        case ESP_HF_CLIENT_CONNECTION_STATE_SLC_CONNECTED:
            s_slc = true;
            ESP_LOGI(TAG, "cellulare collegato (%s)",
                     mac_corto(param->conn_stat.remote_bda));
            salva_peer(param->conn_stat.remote_bda);
            break;
        case ESP_HF_CLIENT_CONNECTION_STATE_DISCONNECTED:
            if (s_slc) {
                ESP_LOGW(TAG, "cellulare scollegato");
            }
            s_slc  = false;
            s_call = false;
            s_setup = ESP_HF_CALL_SETUP_STATUS_IDLE;
            /* Una chiamata in corso quando cade il collegamento e' finita
               comunque: senza questo la macchina a stati resterebbe in
               conversazione con un telefono che non c'e' piu'. */
            valuta_fine();
            break;
        default:
            break;
        }
        break;

    case ESP_HF_CLIENT_CIND_CALL_EVT:
        s_call = (param->call.status == ESP_HF_CALL_STATUS_CALL_IN_PROGRESS);
        valuta_fine();
        break;

    case ESP_HF_CLIENT_CIND_CALL_SETUP_EVT:
        s_setup = param->call_setup.status;
        if (s_setup == ESP_HF_CALL_SETUP_STATUS_INCOMING && !s_in_corso) {
            azzera_chiamata();
            s_in_corso = true;
        }
        valuta_fine();
        break;

    case ESP_HF_CLIENT_CLIP_EVT:
        if (param->clip.number) {
            snprintf(s_clip, sizeof(s_clip), "%s", param->clip.number);
        }
        annuncia_se_pronto();
        break;

    case ESP_HF_CLIENT_RING_IND_EVT:
        if (s_ring < UINT8_MAX) {
            s_ring++;
        }
        annuncia_se_pronto();
        break;

    case ESP_HF_CLIENT_AUDIO_STATE_EVT:
        /* Il canale voce non e' ancora collegato a niente: qui si vede solo se
           l'AG ha negoziato mSBC a 16 kHz o e' ricaduto su CVSD a 8 kHz, che
           e' l'informazione che servira' quando arrivera' il codec. */
        ESP_LOGI(TAG, "audio SCO: stato %d%s", param->audio_stat.state,
                 param->audio_stat.state == ESP_HF_CLIENT_AUDIO_STATE_CONNECTED_MSBC
                     ? " (mSBC 16 kHz)" : "");
        break;

    case ESP_HF_CLIENT_PROF_STATE_EVT:
        ESP_LOGI(TAG, "profilo HFP pronto");
        break;

    default:
        break;
    }
}

static void gap_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param)
{
    switch (event) {
    case ESP_BT_GAP_AUTH_CMPL_EVT:
        if (param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS) {
            ESP_LOGI(TAG, "accoppiato con %s", mac_corto(param->auth_cmpl.bda));
        } else {
            ESP_LOGE(TAG, "accoppiamento fallito (stato %d)",
                     param->auth_cmpl.stat);
        }
        break;

    case ESP_BT_GAP_CFM_REQ_EVT:
        /* Confronto del codice numerico: il telefono non ha modo di mostrarlo
           ne' un tasto per confermarlo, quindi accetta. Vale la stessa
           considerazione di un vivavoce da auto senza display. */
        ESP_LOGI(TAG, "conferma accoppiamento: %lu",
                 (unsigned long)param->cfm_req.num_val);
        esp_bt_gap_ssp_confirm_reply(param->cfm_req.bda, true);
        break;

    default:
        break;
    }
}

void hal_bt_init(QueueHandle_t evt_q)
{
    s_evt_q = evt_q;
    s_setup = ESP_HF_CALL_SETUP_STATUS_IDLE;
    azzera_chiamata();

    /* Il BLE e' spento in sdkconfig: la sua memoria del controller si
       restituisce all'heap, altrimenti resta prenotata per una radio che
       questo progetto non usa mai. */
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_BLE));

    esp_bt_controller_config_t cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bt_controller_init(&cfg));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT));

    ESP_ERROR_CHECK(esp_bluedroid_init());
    ESP_ERROR_CHECK(esp_bluedroid_enable());

    ESP_ERROR_CHECK(esp_bt_gap_set_device_name(BT_DEV_NAME));
    ESP_ERROR_CHECK(esp_bt_gap_register_callback(gap_cb));

    ESP_ERROR_CHECK(esp_hf_client_register_callback(hf_cb));
    ESP_ERROR_CHECK(esp_hf_client_init());

    /* Accoppiamento sicuro senza tastiera ne' display sull'apparecchio. */
    esp_bt_io_cap_t iocap = ESP_BT_IO_CAP_NONE;
    ESP_ERROR_CHECK(esp_bt_gap_set_security_param(ESP_BT_SP_IOCAP_MODE, &iocap,
                                                  sizeof(iocap)));

    /* TODO: la modalita' visibile andra' accesa solo su richiesta, quando
       esistera' il pulsante di configurazione. Per ora resta accesa sempre,
       che e' comodo in collaudo ma lascia il telefono accoppiabile da
       chiunque sia nel raggio. */
    ESP_ERROR_CHECK(esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE,
                                             ESP_BT_GENERAL_DISCOVERABLE));

    carica_peer();

    const esp_timer_create_args_t targs = {
        .callback = riprova_connessione,
        .name     = "bt_riconn",
    };
    ESP_ERROR_CHECK(esp_timer_create(&targs, &s_riconn));
    ESP_ERROR_CHECK(esp_timer_start_periodic(s_riconn, 10 * 1000 * 1000));

    /* Niente tentativo immediato. Al riavvio il cellulare ristabilisce da solo
       il collegamento radio, e chiamare connect() mentre lo sta facendo
       produce un ACL "gia' esistente" (stato 0x0b) che Bluedroid non sa
       recuperare: molla il ritentativo e resta fermo. Il primo tentativo
       arriva col timer, dopo dieci secondi, quando la radio si e' assestata. */

    ESP_LOGI(TAG, "in attesa di accoppiamento come \"%s\"", BT_DEV_NAME);
}

bool hal_bt_place_call(const char *number)
{
    if (!s_slc || !number || !number[0]) {
        return false;
    }
    const esp_err_t err = esp_hf_client_dial(number);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "chiamata rifiutata dallo stack: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}

bool hal_bt_answer(void)
{
    return s_slc && esp_hf_client_answer_call() == ESP_OK;
}

bool hal_bt_reject(void)
{
    return s_slc && esp_hf_client_reject_call() == ESP_OK;
}

bool hal_bt_hangup(void)
{
    /* In HFP chiudere una chiamata in corso e rifiutarne una entrante sono lo
       stesso comando AT: e' l'AG a sapere quale delle due sta succedendo. */
    return s_slc && esp_hf_client_reject_call() == ESP_OK;
}

bool hal_bt_send_dtmf(char digit)
{
    return s_slc && esp_hf_client_send_dtmf(digit) == ESP_OK;
}

bool hal_bt_is_connected(void)
{
    return s_slc;
}
