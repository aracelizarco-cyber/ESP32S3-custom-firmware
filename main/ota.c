#include "ota.h"
#include "esp_https_ota.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "storage.h"
#include "sdkconfig.h"
#include "esp_system.h"
#include <string.h>
#include <stdio.h>

static const char* TAG = "ota";

static void ota_task(void* arg) {
    const char* url = (const char*)arg;
    char stored[256] = {0};
    if (!url || strlen(url) == 0) {
        if (storage_get_ota_url(stored, sizeof(stored))) {
            url = stored;
        } else {
            url = CONFIG_OTA_URL;
        }
    }

    if (!url || strlen(url) == 0) {
        ESP_LOGE(TAG, "No OTA URL configured");
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGW(TAG, "Starting OTA from: %s", url);

    esp_http_client_config_t http_cfg = {
        .url = url,
#if CONFIG_OTA_SKIP_CERT_VERIFY
        .cert_pem = NULL,
        .skip_cert_common_name_check = true,
#else
        .cert_pem = NULL, // set server cert here if needed
#endif
        .timeout_ms = 30 * 1000,
    };
    esp_https_ota_config_t ota_cfg = {
        .http_config = &http_cfg,
    };

    esp_err_t ret = esp_https_ota(&ota_cfg);
    if (ret == ESP_OK) {
        ESP_LOGW(TAG, "OTA successful, restarting...");
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart();
    } else {
        ESP_LOGE(TAG, "OTA failed: %s", esp_err_to_name(ret));
    }
    vTaskDelete(NULL);
}

void ota_init(void) {
    // nothing for now
}

void ota_set_url(const char* url) {
    if (url && strlen(url)) {
        storage_set_ota_url(url);
        ESP_LOGI(TAG, "OTA URL saved");
    }
}

void ota_get_url(char* buf, int buf_sz) {
    if (!buf || buf_sz <= 0) return;
    if (!storage_get_ota_url(buf, buf_sz)) {
        snprintf(buf, buf_sz, "%s", CONFIG_OTA_URL);
    }
}

void ota_trigger(const char* url_opt) {
    xTaskCreatePinnedToCore(ota_task, "ota_task", 8192, (void*)url_opt, 5, NULL, tskNO_AFFINITY);
}