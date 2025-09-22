#pragma once

#include "driver/i2c.h"
#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SCD4X_I2C_ADDR 0x62

typedef struct {
    float co2_ppm;
    float temperature_c;
    float humidity_rh;
} scd4x_measurement_t;

// Initialize I2C master (port, pins, speed)
esp_err_t scd4x_i2c_init(i2c_port_t port, int sda_gpio, int scl_gpio, uint32_t clk_speed_hz);

// Soft-reset/reinit sensor
esp_err_t scd4x_reinit(i2c_port_t port);

// Start periodic measurement (typical mode)
esp_err_t scd4x_start_periodic_measurement(i2c_port_t port);

// Stop periodic measurement
esp_err_t scd4x_stop_periodic_measurement(i2c_port_t port);

// Read the latest measurement (CO2 ppm, temp C, RH %). Should be called ~5s or more after start and then every 5s.
esp_err_t scd4x_read_measurement(i2c_port_t port, scd4x_measurement_t* out);

// Optional: read serial number (3 words)
esp_err_t scd4x_read_serial_number(i2c_port_t port, uint16_t sn_words[3]);

#ifdef __cplusplus
}
#endif
