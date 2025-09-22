#include "storage.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include <string.h>

static const char* TAG = "storage";
static nvs_handle_t s_nvs = 0;

esp_err_t storage_init(void) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) return err;
    err = nvs_open("cfg", NVS_READWRITE, &s_nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t storage_set_u32(const char* key, uint32_t v) {
    esp_err_t err = nvs_set_u32(s_nvs, key, v);
    if (err == ESP_OK) err = nvs_commit(s_nvs);
    return err;
}

esp_err_t storage_get_u32(const char* key, uint32_t* out, uint32_t default_v) {
    uint32_t v = 0;
    esp_err_t err = nvs_get_u32(s_nvs, key, &v);
    if (err == ESP_OK) {
        *out = v; return ESP_OK;
    }
    *out = default_v;
    return ESP_OK;
}

esp_err_t storage_set_bool(const char* key, bool v) {
    return storage_set_u32(key, v ? 1u : 0u);
}

esp_err_t storage_get_bool(const char* key, bool* out, bool default_v) {
    uint32_t tmp = 0;
    esp_err_t err = storage_get_u32(key, &tmp, default_v ? 1u : 0u);
    *out = tmp != 0;
    return err;
}

esp_err_t storage_set_str(const char* key, const char* v) {
    esp_err_t err = nvs_set_str(s_nvs, key, v ? v : "");
    if (err == ESP_OK) err = nvs_commit(s_nvs);
    return err;
}

esp_err_t storage_get_str(const char* key, char* out, size_t out_sz, const char* default_v) {
    size_t required = 0;
    esp_err_t err = nvs_get_str(s_nvs, key, NULL, &required);
    if (err == ESP_OK && required > 0 && required <= out_sz) {
        err = nvs_get_str(s_nvs, key, out, &required);
        return err;
    }
    // default
    if (default_v) {
        strncpy(out, default_v, out_sz - 1);
        out[out_sz - 1] = 0;
    } else if (out_sz) {
        out[0] = 0;
    }
    return ESP_OK;
}