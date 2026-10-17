/*
 * hal_i2c.c — il bus I2C, uno solo per tutti.
 *
 * Ci vivono due dispositivi con indirizzi diversi: il display SSD1306 a 0x3C e
 * il codec WM8960 a 0x1A. Il bus va creato una volta sola — crearlo due volte
 * fallisce, perche' la porta e' gia' occupata — quindi sta qui invece che
 * dentro il driver di chi arriva per primo.
 *
 * Prima stava in hal_display.c, che infatti se lo annotava come debito da
 * saldare all'arrivo del codec. E' arrivato.
 */

#include "hal_priv.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_rom_sys.h"

static const char *TAG = "hal_i2c";

static i2c_master_bus_handle_t s_bus;

/*
 * Sblocca un bus rimasto a meta' byte.
 *
 * Se l'ESP32 si riavvia mentre il codec o il display stanno trasmettendo, il
 * dispositivo resta fermo a meta' di un byte con SDA bassa: lui non e' stato
 * riavviato, e aspetta i colpi di clock che mancano. Il bus resta bloccato e
 * il codec "sparisce" — il 17/10/2026 un avvio si e' e uno no, con SDA e SCL a
 * 3,3 V misurati a bus fermo. La cura standard dell'I2C: fino a nove colpi di
 * clock su SCL finche' chi trasmette non molla SDA, poi una condizione di STOP.
 */
static void sblocca_bus(void)
{
    const gpio_config_t scl = {
        .pin_bit_mask = 1ULL << PIN_I2C_SCL,
        .mode         = GPIO_MODE_INPUT_OUTPUT_OD,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
    };
    const gpio_config_t sda = {
        .pin_bit_mask = 1ULL << PIN_I2C_SDA,
        .mode         = GPIO_MODE_INPUT_OUTPUT_OD,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&scl);
    gpio_config(&sda);
    gpio_set_level(PIN_I2C_SCL, 1);
    gpio_set_level(PIN_I2C_SDA, 1);
    esp_rom_delay_us(10);

    int colpi = 0;
    while (gpio_get_level(PIN_I2C_SDA) == 0 && colpi < 9) {
        gpio_set_level(PIN_I2C_SCL, 0);
        esp_rom_delay_us(10);
        gpio_set_level(PIN_I2C_SCL, 1);
        esp_rom_delay_us(10);
        colpi++;
    }

    /* STOP: SDA sale mentre SCL e' alta. */
    gpio_set_level(PIN_I2C_SDA, 0);
    esp_rom_delay_us(10);
    gpio_set_level(PIN_I2C_SCL, 1);
    esp_rom_delay_us(10);
    gpio_set_level(PIN_I2C_SDA, 1);
    esp_rom_delay_us(10);

    if (colpi > 0) {
        ESP_LOGW(TAG, "bus bloccato con SDA bassa: sbloccato con %d colpi di clock%s",
                 colpi, gpio_get_level(PIN_I2C_SDA) ? "" : " (SDA ancora bassa!)");
    }
    gpio_reset_pin(PIN_I2C_SCL);
    gpio_reset_pin(PIN_I2C_SDA);
}

i2c_master_bus_handle_t hal_i2c_bus(void)
{
    if (s_bus) {
        return s_bus;
    }

    sblocca_bus();

    const i2c_master_bus_config_t cfg = {
        .i2c_port          = I2C_NUM_0,
        .sda_io_num        = PIN_I2C_SDA,
        .scl_io_num        = PIN_I2C_SCL,
        .clk_source        = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    const esp_err_t err = i2c_new_master_bus(&cfg, &s_bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "bus non creato: %s", esp_err_to_name(err));
        s_bus = NULL;
        return NULL;
    }

    ESP_LOGI(TAG, "bus su SDA%d/SCL%d", PIN_I2C_SDA, PIN_I2C_SCL);
    return s_bus;
}
