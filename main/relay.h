#pragma once

#include "driver/gpio.h"
#include <stdbool.h>
#include "esp_err.h"

// Map relays 1..4 to GPIOs 8..11
#define RELAY1_GPIO 8
#define RELAY2_GPIO 9
#define RELAY3_GPIO 10
#define RELAY4_GPIO 11

// If your relay board is active-low, set this to 0
#ifndef RELAY_ACTIVE_LEVEL
#define RELAY_ACTIVE_LEVEL 1
#endif

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t relay_init(void);

// channel is 1..4
esp_err_t relay_set_channel(int channel, bool on);

// Convenience: set all relays at once
esp_err_t relay_set_all(bool on);

// Get current state of a relay (returns true if ON)
bool relay_get_channel(int channel);

#ifdef __cplusplus
}
#endif
