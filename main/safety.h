#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void safety_init(void);
bool safety_can_turn_on(int channel);
void safety_on_relay_state_change(int channel, bool on);
void safety_apply_policy_now(void);

// Config getters/setters (persisted)
bool safety_get_away_mode(void);
void safety_set_away_mode(bool on);

bool safety_get_schedule_enforce(void);
void safety_set_schedule_enforce(bool on);

void safety_get_schedule_windows(uint16_t* w1_start, uint16_t* w1_end, uint16_t* w2_start, uint16_t* w2_end);
void safety_set_schedule_windows(uint16_t w1_start, uint16_t w1_end, uint16_t w2_start, uint16_t w2_end);

void safety_set_max_on_seconds(int channel, uint32_t seconds);
uint32_t safety_get_max_on_seconds(int channel);

#ifdef __cplusplus
}
#endif