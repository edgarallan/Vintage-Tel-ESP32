/*
 * hal_codec.c — il codec audio WM8960.
 *
 * STATO: per ora solo il collegamento di controllo. Il codec ha due interfacce
 * indipendenti — I2C per i comandi, I2S per l'audio — e questa e' la prima. Si
 * puo' verificare adesso perche' NON richiede l'MCLK, che e' il pezzo ancora
 * da risolvere nella mappa GPIO (vedi hardware/pinout.md).
 *
 * IL WM8960 NON SI PUO' RILEGGERE. Sul suo bus a due fili i registri sono di
 * sola scrittura: l'indirizzo sta in 7 bit e il dato in 9, impacchettati in due
 * byte come
 *
 *     byte 0 = [ A6 A5 A4 A3 A2 A1 A0 | D8 ]
 *     byte 1 = [ D7 D6 D5 D4 D3 D2 D1 D0 ]
 *
 * Non esiste quindi un registro di identificazione da confrontare, e nemmeno un
 * modo di verificare che una configurazione sia stata presa. L'unica conferma
 * disponibile e' l'ACK sul bus: il chip c'e' e risponde al suo indirizzo. Tutto
 * il resto si giudica dal suono.
 */

#include "hal_priv.h"

#include "esp_log.h"

static const char *TAG = "hal_codec";

#define CODEC_ADDR   0x1A     /* con CSB a massa; 0x1B se tirato alto */
#define CODEC_HZ     400000

/* Registro di reset: scriverci qualunque cosa riporta il chip ai valori
   iniziali. Utile all'avvio, perche' un riavvio dell'ESP32 non azzera il codec:
   senza, il firmware ripartirebbe trovando registri configurati da chissa'
   quale esecuzione precedente. */
#define REG_RESET    0x0F

static i2c_master_dev_handle_t s_dev;
static bool s_presente;

esp_err_t hal_codec_write(uint8_t reg, uint16_t val)
{
    if (!s_dev) {
        return ESP_ERR_INVALID_STATE;
    }
    const uint8_t buf[2] = {
        (uint8_t)((reg << 1) | ((val >> 8) & 0x01)),
        (uint8_t)(val & 0xFF),
    };
    return i2c_master_transmit(s_dev, buf, sizeof(buf), 100);
}

bool hal_codec_presente(void)
{
    return s_presente;
}

void hal_codec_init(void)
{
    i2c_master_bus_handle_t bus = hal_i2c_bus();
    if (!bus) {
        return;
    }

    /* Prima di parlargli, chiedere se c'e'. Senza questo un codec scollegato
       darebbe una sfilza di errori di trasmissione a ogni registro, invece di
       una riga sola che dice la verita'. */
    if (i2c_master_probe(bus, CODEC_ADDR, 100) != ESP_OK) {
        ESP_LOGW(TAG, "nessun WM8960 a 0x%02X: audio non disponibile", CODEC_ADDR);
        return;
    }

    const i2c_device_config_t dev = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = CODEC_ADDR,
        .scl_speed_hz    = CODEC_HZ,
    };
    if (i2c_master_bus_add_device(bus, &dev, &s_dev) != ESP_OK) {
        ESP_LOGE(TAG, "codec trovato ma non agganciato al bus");
        return;
    }

    if (hal_codec_write(REG_RESET, 0x000) != ESP_OK) {
        ESP_LOGE(TAG, "il codec risponde all'indirizzo ma rifiuta le scritture");
        return;
    }

    s_presente = true;
    ESP_LOGI(TAG, "WM8960 presente a 0x%02X e resettato", CODEC_ADDR);
    ESP_LOGW(TAG, "solo controllo: l'audio manca ancora dell'MCLK e dell'I2S");
}
