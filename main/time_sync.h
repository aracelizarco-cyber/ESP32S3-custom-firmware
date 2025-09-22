#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void time_sync_init(void);
bool time_is_synced(void);
uint32_t time_minutes_since_midnight_local(void);
// Helper to evaluate if current local time is within any of two windows
bool time_within_windows(uint16_t w1_start_min, uint16_t w1_end_min,
                         uint16_t w2_start_min, uint16_t w2_end_min,
                         uint32_t now_min_local);

#ifdef __cplusplus
}
#endif