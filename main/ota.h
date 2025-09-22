#pragma once
#include "esp_err.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void ota_init(void);
void ota_set_url(const char* url);
void ota_get_url(char* out, size_t out_sz);
esp_err_t ota_trigger(const char* url_or_null); // starts OTA task

#ifdef __cplusplus
}
#endif