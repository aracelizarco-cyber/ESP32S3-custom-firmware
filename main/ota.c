#include "ota.h"
#include "esp_https_ota.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "storage.h"
#include "sdkconfig.h"
#include <string.h>

static const char* TAG = "ota";
static char s_url[256] = {0};

void ota_init(void) {
    // Load saved URL or default from Kconfig
    storage_get_str("ota_url", s_url, sizeof(s_url), CONFIG_OTA_DEFAULT_URL);
}

void ota_set_url(const char* url) {
    if (!url) url = "";
    strncpy(s_url, url, sizeof(s_url)-1);
    s_url[sizeof(s_url)-1] = 0;
    storage_set_str("ota_url", s_url);
}

void ota_get_url(char* out, size_t out_sz) {
    if (!out || !out_sz) return;
    strncpy(out, s_url, out_sz - 1);
    out[out_sz - 1] = 0;
}

static void ota_task(void* arg) {
    const char* url = (const char*)arg;
    char local[256];
    if (url && url[0]) {
        strncpy(local, url, sizeof(local)-1);
        local[sizeof(local)-1] = 0;
    } else {
        strncpy(local, s_url, sizeof(local)-1);
        local[sizeof(local)-1] = 0;
    }

    if (!local[0]) {
        ESP_LOGE(TAG, "No OTA URL configured");
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Starting OTA from %s", local);

    esp_http_client_config_t http_cfg = {
        .url = local,
        .timeout_ms = 15000,
#if CONFIG_OTA_USE_SERVER_CERT
        .cert_pem = (const char*)CONFIG_OTA_SERVER_CERT_PEM,
#else
        .cert_pem = NULL,
#endif
    };
    esp_https_ota_config_t ota_cfg = {
        .http_config = &http_cfg,
    };

    esp_err_t ret = esp_https_ota(&ota_cfg);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "OTA successful, rebooting...");
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart();
    } else {
        ESP_LOGE(TAG, "OTA failed: %s", esp_err_to_name(ret));
    }
    vTaskDelete(NULL);
}

esp_err_t ota_trigger(const char* url_or_null) {
    BaseType_t ok = xTaskCreate(ota_task, "ota_task", 8192, (void*)url_or_null, 5, NULL);
    return ok == pdPASS ? ESP_OK : ESP_FAIL;
}