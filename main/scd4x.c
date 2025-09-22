#include "scd4x.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char* TAG = "scd4x";

static uint8_t sensirion_crc8(const uint8_t* data, uint16_t count) {
    uint8_t crc = 0xFF;
    for (uint16_t i = 0; i < count; i++) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; bit++) {
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

static esp_err_t scd4x_write_cmd(i2c_port_t port, uint16_t cmd) {
    uint8_t buf[2] = { (uint8_t)((cmd >> 8) & 0xFF), (uint8_t)(cmd & 0xFF) };
    return i2c_master_write_to_device(port, SCD4X_I2C_ADDR, buf, sizeof(buf), pdMS_TO_TICKS(100));
}

static esp_err_t scd4x_write_cmd_with_args(i2c_port_t port, uint16_t cmd, const uint16_t* args, size_t num_args) {
    uint8_t buf[2 + num_args * 3];
    buf[0] = (uint8_t)((cmd >> 8) & 0xFF);
    buf[1] = (uint8_t)(cmd & 0xFF);
    for (size_t i = 0; i < num_args; ++i) {
        uint8_t msb = (uint8_t)((args[i] >> 8) & 0xFF);
        uint8_t lsb = (uint8_t)(args[i] & 0xFF);
        uint8_t crc = sensirion_crc8((uint8_t[]){msb, lsb}, 2);
        buf[2 + i*3 + 0] = msb;
        buf[2 + i*3 + 1] = lsb;
        buf[2 + i*3 + 2] = crc;
    }
    return i2c_master_write_to_device(port, SCD4X_I2C_ADDR, buf, sizeof(buf), pdMS_TO_TICKS(100));
}

static esp_err_t scd4x_read_words(i2c_port_t port, uint16_t cmd, uint16_t* words, size_t num_words) {
    esp_err_t err = scd4x_write_cmd(port, cmd);
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(1));

    size_t to_read = num_words * 3;
    uint8_t buf[18];
    if (to_read > sizeof(buf)) return ESP_ERR_INVALID_SIZE;

    err = i2c_master_read_from_device(port, SCD4X_I2C_ADDR, buf, to_read, pdMS_TO_TICKS(100));
    if (err != ESP_OK) return err;

    for (size_t i = 0; i < num_words; ++i) {
        uint8_t msb = buf[i*3 + 0];
        uint8_t lsb = buf[i*3 + 1];
        uint8_t crc = buf[i*3 + 2];
        uint8_t calc = sensirion_crc8((uint8_t[]){msb, lsb}, 2);
        if (crc != calc) {
            ESP_LOGE(TAG, "CRC mismatch on word %u", (unsigned)i);
            return ESP_ERR_INVALID_CRC;
        }
        words[i] = ((uint16_t)msb << 8) | lsb;
    }
    return ESP_OK;
}

#define SCD4X_CMD_START_PERIODIC_MEASUREMENT   0x21B1
#define SCD4X_CMD_STOP_PERIODIC_MEASUREMENT    0x3F86
#define SCD4X_CMD_READ_MEASUREMENT             0xEC05
#define SCD4X_CMD_REINIT                       0x3646
#define SCD4X_CMD_READ_SERIAL_NUMBER           0x3682

esp_err_t scd4x_i2c_init(i2c_port_t port, int sda_gpio, int scl_gpio, uint32_t clk_speed_hz) {
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = sda_gpio,
        .scl_io_num = scl_gpio,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
#if SOC_I2C_SUPPORT_REF_TICK
        .clk_flags = 0,
#endif
    };
    conf.master.clk_speed = clk_speed_hz;
    esp_err_t err = i2c_param_config(port, &conf);
    if (err != ESP_OK) return err;
    err = i2c_driver_install(port, conf.mode, 0, 0, 0);
    if (err == ESP_ERR_INVALID_STATE) return ESP_OK;
    return err;
}

esp_err_t scd4x_reinit(i2c_port_t port) {
    esp_err_t err = scd4x_write_cmd(port, SCD4X_CMD_REINIT);
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(20));
    return ESP_OK;
}

esp_err_t scd4x_start_periodic_measurement(i2c_port_t port) {
    return scd4x_write_cmd(port, SCD4X_CMD_START_PERIODIC_MEASUREMENT);
}

esp_err_t scd4x_stop_periodic_measurement(i2c_port_t port) {
    esp_err_t err = scd4x_write_cmd(port, SCD4X_CMD_STOP_PERIODIC_MEASUREMENT);
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(500));
    return ESP_OK;
}

esp_err_t scd4x_read_measurement(i2c_port_t port, scd4x_measurement_t* out) {
    if (!out) return ESP_ERR_INVALID_ARG;

    uint16_t words[3] = {0};
    esp_err_t err = scd4x_read_words(port, SCD4X_CMD_READ_MEASUREMENT, words, 3);
    if (err != ESP_OK) return err;

    uint16_t co2_raw = words[0];
    uint16_t t_raw = words[1];
    uint16_t rh_raw = words[2];

    out->co2_ppm = (float)co2_raw;
    out->temperature_c = -45.0f + (175.0f * (float)t_raw) / 65535.0f;
    out->humidity_rh = (100.0f * (float)rh_raw) / 65535.0f;

    return ESP_OK;
}

esp_err_t scd4x_read_serial_number(i2c_port_t port, uint16_t sn_words[3]) {
    if (!sn_words) return ESP_ERR_INVALID_ARG;
    return scd4x_read_words(port, SCD4X_CMD_READ_SERIAL_NUMBER, sn_words, 3);
}