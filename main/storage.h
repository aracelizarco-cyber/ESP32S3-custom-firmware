#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void storage_init(void);

// Per-relay max-on seconds
bool storage_get_max_on_seconds(int ch, uint32_t* out);
bool storage_set_max_on_seconds(int ch, uint32_t seconds);

// Schedule windows (minutes since midnight)
bool storage_get_schedule_window(int idx, uint16_t* start_min, uint16_t* end_min);
bool storage_set_schedule_window(int idx, uint16_t start_min, uint16_t end_min);

// OTA URL
bool storage_get_ota_url(char* buf, size_t buf_sz);
bool storage_set_ota_url(const char* url);

#ifdef __cplusplus
}
#endif