#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Initialize and start MQTT, HA discovery, and subscriptions.
void ha_mqtt_start(const char* device_name, const char* device_id);

// Publish sensor updates
void ha_mqtt_publish_scd4x(float co2_ppm, float temp_c, float rh_pct);

// Notify MQTT of relay state changes and modes/config updates
void ha_mqtt_publish_relay_state(int channel, bool on);
void ha_mqtt_publish_away_state(bool on);
void ha_mqtt_publish_schedule_state(bool enforce_on);
void ha_mqtt_publish_max_on_minutes(int channel, uint32_t minutes);
void ha_mqtt_publish_schedule_windows(void);
void ha_mqtt_publish_ota_url(void);

#ifdef __cplusplus
}
#endif