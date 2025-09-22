#include "storage.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include <string.h>

static const char* TAG = "storage";
static nvs_handle_t s_nvs = 0;

void storage_init(void) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }
    err = nvs_open("conf", NVS_READWRITE, &s_nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
    }
}

static const char* key_for_max(int ch) {
    switch (ch) {
        case 1: return "r1_max";
        case 2: return "r2_max";
        case 3: return "r3_max";
        case 4: return "r4_max";
        default: return NULL;
    }
}

bool storage_get_max_on_seconds(int ch, uint32_t* out) {
    if (!s_nvs || !out) return false;
    const char* key = key_for_max(ch);
    if (!key) return false;
    uint32_t v = 0;
    esp_err_t err = nvs_get_u32(s_nvs, key, &v);
    if (err == ESP_OK) { *out = v; return true; }
    return false;
}

bool storage_set_max_on_seconds(int ch, uint32_t seconds) {
    if (!s_nvs) return false;
    const char* key = key_for_max(ch);
    if (!key) return false;
    esp_err_t err = nvs_set_u32(s_nvs, key, seconds);
    if (err != ESP_OK) return false;
    nvs_commit(s_nvs);
    return true;
}

bool storage_get_schedule_window(int idx, uint16_t* start_min, uint16_t* end_min) {
    if (!s_nvs || !start_min || !end_min) return false;
    char k1[16], k2[16];
    snprintf(k1, sizeof(k1), "w%d_start", idx);
    snprintf(k2, sizeof(k2), "w%d_end", idx);
    uint32_t s=0,e=0;
    esp_err_t e1 = nvs_get_u32(s_nvs, k1, &s);
    esp_err_t e2 = nvs_get_u32(s_nvs, k2, &e);
    if (e1 == ESP_OK && e2 == ESP_OK) {
        *start_min = (uint16_t)s;
        *end_min = (uint16_t)e;
        return true;
    }
    return false;
}

bool storage_set_schedule_window(int idx, uint16_t start_min, uint16_t end_min) {
    if (!s_nvs) return false;
    char k1[16], k2[16];
    snprintf(k1, sizeof(k1), "w%d_start", idx);
    snprintf(k2, sizeof(k2), "w%d_end", idx);
    esp_err_t e1 = nvs_set_u32(s_nvs, k1, start_min);
    esp_err_t e2 = nvs_set_u32(s_nvs, k2, end_min);
    if (e1 != ESP_OK || e2 != ESP_OK) return false;
    nvs_commit(s_nvs);
    return true;
}

bool storage_get_ota_url(char* buf, size_t buf_sz) {
    if (!s_nvs || !buf || buf_sz==0) return false;
    size_t len = buf_sz;
    esp_err_t err = nvs_get_str(s_nvs, "ota_url", buf, &len);
    return err == ESP_OK;
}

bool storage_set_ota_url(const char* url) {
    if (!s_nvs) return false;
    esp_err_t err = nvs_set_str(s_nvs, "ota_url", url ? url : "");
    if (err != ESP_OK) return false;
    nvs_commit(s_nvs);
    return true;
}