#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Initialize safety manager (loads config from Kconfig and NVS)
void safety_init(void);

// Called when relay state changes; updates timers and enforcement
void safety_on_relay_state_change(int channel, bool on);

// Returns true if it's allowed to turn on this relay in current modes
bool safety_can_turn_on(int channel);

// Away Mode controls
void safety_set_away_mode(bool on);
bool safety_get_away_mode(void);

// Per-relay max ON seconds controls
void safety_set_max_on_seconds(int channel, uint32_t seconds);
uint32_t safety_get_max_on_seconds(int channel);

// Schedule windows and enforcement
void safety_set_schedule_windows(uint16_t w1_start_min, uint16_t w1_end_min,
                                 uint16_t w2_start_min, uint16_t w2_end_min);
void safety_get_schedule_windows(uint16_t* w1_start_min, uint16_t* w1_end_min,
                                 uint16_t* w2_start_min, uint16_t* w2_end_min);
void safety_set_schedule_enforce(bool on);
bool safety_get_schedule_enforce(void);

// Apply safety policy immediately (e.g., on MQTT/Wi-Fi disconnect)
void safety_apply_policy_now(void);

// Masks
uint8_t safety_get_away_allowed_mask(void);
uint8_t safety_get_schedule_allowed_mask(void);

#ifdef __cplusplus
}
#endif