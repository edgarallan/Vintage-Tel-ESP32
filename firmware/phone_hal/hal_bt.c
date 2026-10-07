/*
 * hal_bt.c — vivavoce Bluetooth HFP verso il cellulare.
 *
 * L'ESP32 fa da Hands-Free unit, il cellulare da Audio Gateway: e' lo stesso
 * ruolo di un vivavoce da auto, ed e' il motivo per cui il cellulare accetta
 * di girargli le chiamate.
 *
 * L'AUDIO PASSA DA QUI, ma non lo tocca: due callback dello stack travasano
 * PCM da e per le code di hal_audio.c, che e' l'unico a parlare con l'I2S.
 *
 * Il PCM arriva gia' decodificato perche' sdkconfig lascia il codec INTERNO a
 * Bluedroid (CONFIG_BT_HFP_USE_EXTERNAL_CODEC non impostato). Col codec
 * esterno riceveremmo frame mSBC codificati e ci toccherebbe scrivere un
 * decoder — cosa che l'esempio ufficiale di ESP-IDF fa, ed e' il motivo per cui
 * il suo codice non somiglia a questo.
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
#include "esp_hf_client_legacy_api.h"
#include "esp_log.h"
#include "esp_pbac_api.h"
#include "esp_timer.h"
#include "nvs.h"

#include "vcard.h"

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

/*
 * Rubrica via PBAP: si scaricano SOLO I PREFERITI del cellulare, e di ognuno
 * solo nome e numeri. Con 1200 contatti in rubrica la scelta di cosa tenere
 * deve farla chi usa il telefono, e i preferiti sono una scelta che ha gia'
 * fatto — e che cambia dal cellulare, senza toccare questo apparecchio.
 *
 * Lo scaricamento riparte a ogni collegamento del cellulare e la rubrica vive
 * solo in RAM: senza cellulare collegato non arrivano chiamate, quindi non
 * servirebbe a nessuno ricordarla fra un riavvio e l'altro.
 */
#define PBAP_RUBRICA      "telecom/fav.vcf"
#define PBAP_REPO_PREFERITI  0x08   /* bit "favorites" in peer_supported_repo */

/* Proprieta' richieste: VERSION, FN, N, TEL. Le foto restano sul cellulare. */
#define PBAP_PROPRIETA    ((1ULL << 0) | (1ULL << 1) | (1ULL << 2) | (1ULL << 7))

/* Quanto il task di Bluedroid puo' aspettare che la coda del telefono si
   liberi. Una rubrica sono decine di eventi di fila, e la coda ne tiene 16:
   buttarli come si fa col battito vorrebbe dire perdere contatti. */
#define PBAP_ATTESA_CODA  pdMS_TO_TICKS(200)

static vcard_t  s_vcard;
static bool     s_pb_primo;   /* il prossimo pacchetto e' il primo della risposta */
static uint16_t s_pb_voci;

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

static void metti_in_coda(const phone_ev_t *ev)
{
    if (xQueueSend(s_evt_q, ev, PBAP_ATTESA_CODA) != pdTRUE) {
        ESP_LOGW(TAG, "rubrica: coda piena, voce persa");
        return;
    }
    if (ev->type == EV_PB_ADD) {
        s_pb_voci++;
    }
}

static void voce_rubrica(const char *nome, const char *numero, void *ctx)
{
    (void)ctx;
    phone_ev_t ev = {
        .type   = EV_PB_ADD,
        .now_ms = (uint32_t)(esp_timer_get_time() / 1000),
    };
    snprintf(ev.name, sizeof(ev.name), "%s", nome);
    snprintf(ev.caller, sizeof(ev.caller), "%s", numero);
    metti_in_coda(&ev);
}

/* I codici di errore che si vedranno davvero, tradotti in cosa fare. */
static const char *spiega_errore_pbap(esp_pbac_status_t r)
{
    switch (r) {
    case ESP_PBAC_UNAUTHORIZED:
    case ESP_PBAC_FORBIDDEN:
        return "accesso ai contatti negato: attivalo sul cellulare, "
               "Bluetooth > Vintage Tel > Contatti";
    case ESP_PBAC_NOT_FOUND:
    case ESP_PBAC_NOT_IMPLEMENTED:
    case ESP_PBAC_BAD_REQUEST:
        return "il cellulare non espone i preferiti";
    default:
        return "errore";
    }
}

static void avvia_scaricamento(esp_pbac_conn_hdl_t h)
{
    esp_pbac_pull_phone_book_app_param_t par = {
        .include_property_selector = 1,
        .include_format            = 1,
        .include_max_list_count    = 1,
        .format                    = 0x01,              /* vCard 3.0 */
        .max_list_count            = PB_MAX_CONTACTS,
        .property_selector         = PBAP_PROPRIETA,
    };
    vcard_init(&s_vcard, voce_rubrica, NULL);
    s_pb_primo = true;
    s_pb_voci  = 0;

    const esp_err_t err = esp_pbac_pull_phone_book(h, PBAP_RUBRICA, &par);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "rubrica: richiesta rifiutata dallo stack: %s",
                 esp_err_to_name(err));
        esp_pbac_disconnect(h);
    }
}

static void pbac_cb(esp_pbac_event_t event, esp_pbac_param_t *param)
{
    switch (event) {
    case ESP_PBAC_CONNECTION_STATE_EVT:
        if (param->conn_stat.connected) {
            const uint8_t repo = param->conn_stat.peer_supported_repo;
            ESP_LOGI(TAG, "rubrica: collegato, il cellulare offre 0x%02X%s", repo,
                     (repo & PBAP_REPO_PREFERITI) ? " (preferiti compresi)" : "");
            avvia_scaricamento(param->conn_stat.handle);
        } else if (param->conn_stat.reason != ESP_PBAC_SUCCESS) {
            ESP_LOGW(TAG, "rubrica: collegamento non riuscito (0x%02X)",
                     param->conn_stat.reason);
        }
        break;

    case ESP_PBAC_PULL_PHONE_BOOK_RESPONSE_EVT: {
        const struct pbac_pull_phone_book_rsp_param *r = &param->pull_phone_book_rsp;

        if (r->result != ESP_PBAC_SUCCESS) {
            ESP_LOGW(TAG, "rubrica: %s (0x%02X)",
                     spiega_errore_pbap(r->result), r->result);
            esp_pbac_disconnect(r->handle);
            break;
        }
        /* Si svuota solo quando il cellulare ha accettato: un rifiuto non
           deve lasciare il telefono senza i nomi che aveva. */
        if (s_pb_primo) {
            s_pb_primo = false;
            const phone_ev_t ev = { .type = EV_PB_CLEAR };
            metti_in_coda(&ev);
        }
        if (r->data && r->data_len) {
            vcard_feed(&s_vcard, r->data, r->data_len);
        }
        if (r->final) {
            vcard_end(&s_vcard);
            ESP_LOGI(TAG, "rubrica: %u numeri dai preferiti", s_pb_voci);
            esp_pbac_disconnect(r->handle);
        }
        break;
    }

    default:
        break;
    }
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

/*
 * I due versi dell'audio. Girano nel task di Bluedroid, quindi non fanno altro
 * che spostare byte: qualunque attesa qui dentro rallenta lo stack Bluetooth.
 */
static void audio_in_cb(const uint8_t *buf, uint32_t len)
{
    hal_audio_rx_push(buf, len);
}

static uint32_t audio_out_cb(uint8_t *buf, uint32_t len)
{
    return (uint32_t)hal_audio_tx_pop(buf, len);
}

/* Chiamata dal task audio quando ha un frame pronto da mandare. */
void hal_bt_audio_pronto(void)
{
    esp_hf_client_outgoing_data_ready();
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
            /* La rubrica si chiede solo a collegamento fatto: e' il momento
               in cui il cellulare ha gia' accettato questo apparecchio. La
               prima volta Android chiede il permesso con una notifica. */
            if (esp_pbac_connect(param->conn_stat.remote_bda) != ESP_OK) {
                ESP_LOGW(TAG, "rubrica: lo stack non apre il PBAP");
            }
            break;
        case ESP_HF_CLIENT_CONNECTION_STATE_DISCONNECTED:
            if (s_slc) {
                ESP_LOGW(TAG, "cellulare scollegato");
            }
            s_slc  = false;
            s_call = false;
            hal_audio_set_chiamata(false);
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

    case ESP_HF_CLIENT_CIND_CALL_EVT: {
        const bool prima = s_call;
        s_call = (param->call.status == ESP_HF_CALL_STATUS_CALL_IN_PROGRESS);

        /* Il passaggio da "nessuna chiamata" a "chiamata attiva" e' il momento
           in cui dall'altra parte hanno alzato: e' cio' che distingue una
           uscente che sta squillando da una conversazione vera. */
        if (s_call && !prima) {
            ESP_LOGI(TAG, "hanno risposto");
            send_ev(EV_CALL_ANSWERED, NULL);
        }
        valuta_fine();
        break;
    }

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

    case ESP_HF_CLIENT_AUDIO_STATE_EVT: {
        const esp_hf_client_audio_state_t st = param->audio_stat.state;
        const bool su = (st == ESP_HF_CLIENT_AUDIO_STATE_CONNECTED ||
                         st == ESP_HF_CLIENT_AUDIO_STATE_CONNECTED_MSBC);

        if (su) {
            ESP_LOGI(TAG, "canale voce aperto%s",
                     st == ESP_HF_CLIENT_AUDIO_STATE_CONNECTED_MSBC
                         ? " (mSBC 16 kHz)" : " (CVSD 8 kHz)");

            /* La cornetta e l'I2S girano a 16 kHz fissi. Con mSBC coincidono;
               con CVSD il cellulare parla a 8 kHz e la voce uscirebbe al
               doppio della velocita'. Non e' ancora gestito, e vale la pena
               dirlo invece di lasciare indovinare. */
            if (st != ESP_HF_CLIENT_AUDIO_STATE_CONNECTED_MSBC) {
                ESP_LOGW(TAG, "CVSD a 8 kHz: manca la conversione, la voce sara' sbagliata");
            }

            esp_hf_client_register_data_callback(audio_in_cb, audio_out_cb);
            hal_audio_set_chiamata(true);
        } else if (st == ESP_HF_CLIENT_AUDIO_STATE_DISCONNECTED) {
            ESP_LOGI(TAG, "canale voce chiuso");
            hal_audio_set_chiamata(false);
        }
        break;
    }

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

    ESP_ERROR_CHECK(esp_pbac_register_callback(pbac_cb));
    ESP_ERROR_CHECK(esp_pbac_init());

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

bool hal_bt_release_audio(void)
{
    if (!s_slc) {
        return false;
    }
    /* Chiudere il canale voce dal lato vivavoce e' il modo, previsto
       dall'HFP, per dire al cellulare "parla tu": Android riporta la
       conversazione sul proprio auricolare. La chiamata resta in piedi. */
    const esp_err_t err = esp_hf_client_disconnect_audio(s_peer);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "non riesco a restituire l'audio al cellulare: %s",
                 esp_err_to_name(err));
        return false;
    }
    ESP_LOGI(TAG, "risposto dal cellulare: audio restituito");
    return true;
}
