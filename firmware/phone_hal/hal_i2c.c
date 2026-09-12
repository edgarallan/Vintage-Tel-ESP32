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

#include "esp_log.h"

static const char *TAG = "hal_i2c";

static i2c_master_bus_handle_t s_bus;

i2c_master_bus_handle_t hal_i2c_bus(void)
{
    if (s_bus) {
        return s_bus;
    }

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
