#include "esp_system.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c.h"

#include "relay.h"
#include "scd4x.h"
#include "wifi.h"
#include "time_sync.h"
#include "storage.h"
#include "safety.h"
#include "ha_mqtt.h"
#include "ota.h"

static const char* TAG = "app";

static void scd4x_task(void* arg) {
    (void)arg;
    const i2c_port_t port = (i2c_port_t)CONFIG_I2C_PORT;

    // Initialize I2C and sensor
    ESP_ERROR_CHECK(scd4x_i2c_init(port, CONFIG_I2C_SDA_GPIO, CONFIG_I2C_SCL_GPIO, CONFIG_I2C_CLK_HZ));
    scd4x_reinit(port);
    vTaskDelay(pdMS_TO_TICKS(50));
    scd4x_stop_periodic_measurement(port);
    vTaskDelay(pdMS_TO_TICKS(500));
    scd4x_start_periodic_measurement(port);

    // First measurement available after ~5s
    vTaskDelay(pdMS_TO_TICKS(5500));

    while (1) {
        scd4x_measurement_t m = {0};
        esp_err_t err = scd4x_read_measurement(port, &m);
        if (err == ESP_OK) {
            // Filter out zeros that sometimes appear if data not ready
            if (m.co2_ppm > 0.0f) {
                ha_mqtt_publish_scd4x(m.co2_ppm, m.temperature_c, m.humidity_rh);
                ESP_LOGI(TAG, "SCD41: CO2=%.0f ppm T=%.2f C RH=%.1f%%", m.co2_ppm, m.temperature_c, m.humidity_rh);
            }
        } else {
            ESP_LOGW(TAG, "scd4x_read_measurement failed: %s", esp_err_to_name(err));
            // Attempt a reinit if repeated failures
        }
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

static void make_device_identity(char* dev_id, size_t id_sz, char* dev_name, size_t name_sz) {
    uint8_t mac[6] = {0};
    esp_efuse_mac_get_default(mac);
    snprintf(dev_id, id_sz, "esp32s3-%02x%02x%02x", mac[3], mac[4], mac[5]);
    snprintf(dev_name, name_sz, "%s", CONFIG_DEVICE_NAME);
}

void app_main(void) {
    // Storage first (NVS)
    storage_init();

    // Hardware
    ESP_ERROR_CHECK(relay_init());

    // Safety + timers + persisted config
    safety_init();

    // Networking
    wifi_init_and_start();
    time_sync_init();

    // OTA
    ota_init();

    // MQTT + HA discovery
    char device_id[32];
    char device_name[64];
    make_device_identity(device_id, sizeof(device_id), device_name, sizeof(device_name));
    ha_mqtt_start(device_name, device_id);

    // Sensor task
    xTaskCreatePinnedToCore(scd4x_task, "scd4x_task", 4096, NULL, 5, NULL, tskNO_AFFINITY);

    // Idle: nothing else to do here; tasks and callbacks do the work
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}