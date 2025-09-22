#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void ha_mqtt_start(const char* device_name, const char* device_id);

// Publishers for state reflections
void ha_mqtt_publish_relay_state(int channel, bool on);
void ha_mqtt_publish_max_on_minutes(int channel, uint32_t minutes);
void ha_mqtt_publish_schedule_state(bool enforce);
void ha_mqtt_publish_away_state(bool away);
void ha_mqtt_publish_schedule_windows(void);
void ha_mqtt_publish_scd4x(float co2_ppm, float temperature_c, float humidity_rh);
void ha_mqtt_publish_ota_url(void);

#ifdef __cplusplus
}
#endif