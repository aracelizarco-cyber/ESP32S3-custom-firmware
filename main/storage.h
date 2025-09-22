#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t storage_init(void);

// Generic helpers
esp_err_t storage_set_u32(const char* key, uint32_t v);
esp_err_t storage_get_u32(const char* key, uint32_t* out, uint32_t default_v);
esp_err_t storage_set_bool(const char* key, bool v);
esp_err_t storage_get_bool(const char* key, bool* out, bool default_v);
esp_err_t storage_set_str(const char* key, const char* v);
esp_err_t storage_get_str(const char* key, char* out, size_t out_sz, const char* default_v);

#ifdef __cplusplus
}
#endif