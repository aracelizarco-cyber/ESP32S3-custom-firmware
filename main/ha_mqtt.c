#include "ha_mqtt.h"
#include "relay.h"
#include "safety.h"
#include "ota.h"

#include "esp_log.h"
#include "mqtt_client.h"
#include "esp_system.h"
#include "sdkconfig.h"
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <stdlib.h>
#include <ctype.h>
#include <strings.h>

static const char* TAG = "ha_mqtt";

static esp_mqtt_client_handle_t s_client = NULL;
static char s_device_id[32] = {0};
static char s_device_name[64] = {0};

static char s_base_topic[128] = {0};         // e.g., greenhouse/<id>
static char s_availability_topic[160] = {0}; // base/status
static char s_ha_prefix[64] = {0};           // e.g., homeassistant

static void publish_discovery(void);
static void publish_availability(bool online);
static void publish_initial_states(void);

static void publish(const char* topic, const char* payload, int qos, bool retain) {
    if (!s_client) return;
    int msg_id = esp_mqtt_client_publish(s_client, topic, payload, 0, qos, retain);
    if (msg_id < 0) {
        ESP_LOGW(TAG, "Publish failed to %s", topic);
    }
}

static void subscribe(const char* topic, int qos) {
    if (!s_client) return;
    int msg_id = esp_mqtt_client_subscribe(s_client, topic, qos);
    if (msg_id >= 0) {
        ESP_LOGI(TAG, "Subscribed: %s", topic);
    } else {
        ESP_LOGW(TAG, "Subscribe failed: %s", topic);
    }
}

static bool parse_bool(const char* payload, int len, bool* out) {
    if (!payload || len <= 0 || !out) return false;
    if (strncasecmp(payload, "ON", 2) == 0 || strncasecmp(payload, "TRUE", 4) == 0 || strncmp(payload, "1", 1) == 0) {
        *out = true; return true;
    }
    if (strncasecmp(payload, "OFF", 3) == 0 || strncasecmp(payload, "FALSE", 5) == 0 || strncmp(payload, "0", 1) == 0) {
        *out = false; return true;
    }
    return false;
}

static bool parse_u32(const char* payload, int len, uint32_t* out) {
    if (!payload || len <= 0 || !out) return false;
    char tmp[32];
    int n = len < (int)sizeof(tmp)-1 ? len : (int)sizeof(tmp)-1;
    memcpy(tmp, payload, n); tmp[n] = 0;
    char* end = NULL;
    unsigned long v = strtoul(tmp, &end, 10);
    if (end == tmp) return false;
    *out = (uint32_t)v;
    return true;
}

static void on_command_relay(int channel, const char* payload, int len) {
    bool req_on = false;
    if (!parse_bool(payload, len, &req_on)) return;

    if (req_on && !safety_can_turn_on(channel)) {
        ESP_LOGW(TAG, "Command blocked: relay %d ON not allowed", channel);
        ha_mqtt_publish_relay_state(channel, relay_get_channel(channel));
        return;
    }

    relay_set_channel(channel, req_on);
    safety_on_relay_state_change(channel, req_on);
    ha_mqtt_publish_relay_state(channel, relay_get_channel(channel));
}

static void on_command_away(const char* payload, int len) {
    bool on=false; if (!parse_bool(payload, len, &on)) return;
    safety_set_away_mode(on);
    ha_mqtt_publish_away_state(safety_get_away_mode());
}

static void on_command_schedule_enforce(const char* payload, int len) {
    bool on=false; if (!parse_bool(payload, len, &on)) return;
    safety_set_schedule_enforce(on);
    ha_mqtt_publish_schedule_state(safety_get_schedule_enforce());
}

static void on_command_max_on(int channel, const char* payload, int len) {
    // minutes -> seconds
    uint32_t minutes=0; if (!parse_u32(payload, len, &minutes)) return;
    uint32_t sec = minutes * 60u;
    safety_set_max_on_seconds(channel, sec);
    ha_mqtt_publish_max_on_minutes(channel, minutes);
}

static void on_command_window(const char* which, const char* payload, int len) {
    // minutes 0..1439
    uint32_t v=0; if (!parse_u32(payload, len, &v)) return;
    if (v > 1439) v = 1439;
    uint16_t w1s, w1e, w2s, w2e;
    safety_get_schedule_windows(&w1s, &w1e, &w2s, &w2e);
    if (strcmp(which, "w1_start") == 0) w1s = (uint16_t)v;
    else if (strcmp(which, "w1_end") == 0) w1e = (uint16_t)v;
    else if (strcmp(which, "w2_start") == 0) w2s = (uint16_t)v;
    else if (strcmp(which, "w2_end") == 0) w2e = (uint16_t)v;
    else return;

    safety_set_schedule_windows(w1s, w1e, w2s, w2e);
    ha_mqtt_publish_schedule_windows();
}

static void on_command_ota_url(const char* payload, int len) {
    char url[256];
    int n = len < (int)sizeof(url)-1 ? len : (int)sizeof(url)-1;
    memcpy(url, payload, n); url[n] = 0;
    ota_set_url(url);
    ha_mqtt_publish_ota_url();
}

static void on_command_ota_start(void) {
    char url[256]; ota_get_url(url, sizeof(url));
    ESP_LOGW(TAG, "OTA trigger -> %s", url);
    ota_trigger(url);
}

static esp_err_t mqtt_event_handler_cb(esp_mqtt_event_handle_t event) {
    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED: {
            ESP_LOGI(TAG, "MQTT connected");
            publish_availability(true);
            publish_discovery();
            publish_initial_states();
            // Subscriptions on connect
            char topic[192];
            for (int ch = 1; ch <= 4; ++ch) {
                snprintf(topic, sizeof(topic), "%s/relay/%d/set", s_base_topic, ch);
                subscribe(topic, 1);
                snprintf(topic, sizeof(topic), "%s/relay/%d/max_on/set", s_base_topic, ch);
                subscribe(topic, 1);
            }
            snprintf(topic, sizeof(topic), "%s/mode/away/set", s_base_topic); subscribe(topic, 1);
            snprintf(topic, sizeof(topic), "%s/schedule/enforce/set", s_base_topic); subscribe(topic, 1);
            const char* keys[] = {"w1_start","w1_end","w2_start","w2_end"};
            for (int i=0;i<4;i++) { snprintf(topic, sizeof(topic), "%s/schedule/%s/set", s_base_topic, keys[i]); subscribe(topic, 1);}            
            snprintf(topic, sizeof(topic), "%s/ota/url/set", s_base_topic); subscribe(topic, 1);
            snprintf(topic, sizeof(topic), "%s/ota/update", s_base_topic); subscribe(topic, 1);
            break; }
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "MQTT disconnected");
            // LWT will show offline; enforce safety now
            safety_apply_policy_now();
            break;
        case MQTT_EVENT_DATA: {
            const char* topic = event->topic;
            int tlen = event->topic_len;
            const char* data = event->data;
            int dlen = event->data_len;

            if (tlen <= 0) break;
            // Relay commands
            for (int ch = 1; ch <= 4; ++ch) {
                char tpat[192];
                snprintf(tpat, sizeof(tpat), "%s/relay/%d/set", s_base_topic, ch);
                if ((int)strlen(tpat) == tlen && strncmp(topic, tpat, tlen) == 0) {
                    on_command_relay(ch, data, dlen);
                    return ESP_OK;
                }
                snprintf(tpat, sizeof(tpat), "%s/relay/%d/max_on/set", s_base_topic, ch);
                if ((int)strlen(tpat) == tlen && strncmp(topic, tpat, tlen) == 0) {
                    on_command_max_on(ch, data, dlen);
                    return ESP_OK;
                }
            }
            // Away
            {
                char tpat[192];
                snprintf(tpat, sizeof(tpat), "%s/mode/away/set", s_base_topic);
                if ((int)strlen(tpat) == tlen && strncmp(topic, tpat, tlen) == 0) {
                    on_command_away(data, dlen);
                    return ESP_OK;
                }
            }
            // Schedule enforce
            {
                char tpat[192];
                snprintf(tpat, sizeof(tpat), "%s/schedule/enforce/set", s_base_topic);
                if ((int)strlen(tpat) == tlen && strncmp(topic, tpat, tlen) == 0) {
                    on_command_schedule_enforce(data, dlen);
                    return ESP_OK;
                }
            }
            // Window numbers
            const char* keys[] = {"w1_start","w1_end","w2_start","w2_end"};
            for (size_t i=0;i<4;i++) {
                char tpat[192];
                snprintf(tpat, sizeof(tpat), "%s/schedule/%s/set", s_base_topic, keys[i]);
                if ((int)strlen(tpat) == tlen && strncmp(topic, tpat, tlen) == 0) {
                    on_command_window(keys[i], data, dlen);
                    return ESP_OK;
                }
            }
            // OTA URL and trigger
            {
                char tpat[192];
                snprintf(tpat, sizeof(tpat), "%s/ota/url/set", s_base_topic);
                if ((int)strlen(tpat) == tlen && strncmp(topic, tpat, tlen) == 0) {
                    on_command_ota_url(data, dlen);
                    return ESP_OK;
                }
            }
            {
                char tpat[192];
                snprintf(tpat, sizeof(tpat), "%s/ota/update", s_base_topic);
                if ((int)strlen(tpat) == tlen && strncmp(topic, tpat, tlen) == 0) {
                    on_command_ota_start();
                    return ESP_OK;
                }
            }
            break;
        }
        default:
            break;
    }
    return ESP_OK;
}

static void mqtt_event_handler(void* handler_args, esp_event_base_t base, int32_t event_id, void* event_data) {
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    mqtt_event_handler_cb(event);
}

void ha_mqtt_start(const char* device_name, const char* device_id) {
    snprintf(s_device_name, sizeof(s_device_name), "%s", device_name);
    snprintf(s_device_id, sizeof(s_device_id), "%s", device_id);
    snprintf(s_ha_prefix, sizeof(s_ha_prefix), "%s", CONFIG_HA_PREFIX);
    snprintf(s_base_topic, sizeof(s_base_topic), "%s/%s", CONFIG_MQTT_BASE_TOPIC, s_device_id);
    snprintf(s_availability_topic, sizeof(s_availability_topic), "%s/status", s_base_topic);

    esp_mqtt_client_config_t cfg = {
        .broker.address.uri = CONFIG_MQTT_BROKER_URI,
        .credentials.username = strlen(CONFIG_MQTT_USERNAME) ? CONFIG_MQTT_USERNAME : NULL,
        .credentials.authentication.password = strlen(CONFIG_MQTT_PASSWORD) ? CONFIG_MQTT_PASSWORD : NULL,
        .session.last_will.topic = s_availability_topic,
        .session.last_will.msg = "offline",
        .session.last_will.retain = true,
        .session.last_will.qos = 1,
    };
    s_client = esp_mqtt_client_init(&cfg);
    esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(s_client);
}

static void add_device_block(char* out, size_t out_sz) {
    // Home Assistant device block (long-form keys)
    snprintf(out, out_sz,
        "\"device\":{\"identifiers\":[\"%s\"],\"name\":\"%s\",\"manufacturer\":\"ESP\",\"model\":\"ESP32-S3\",\"sw_version\":\"relay_scd41\"}",
        s_device_id, s_device_name);
}

static void publish_discovery(void) {
    char dev[256];
    add_device_block(dev, sizeof(dev));

    // Relays (switch)
    for (int ch = 1; ch <= 4; ++ch) {
        char topic[256];
        snprintf(topic, sizeof(topic), "%s/switch/%s/relay%d/config", s_ha_prefix, s_device_id, ch);

        char cmd_t[160], stat_t[160];
        snprintf(cmd_t, sizeof(cmd_t), "%s/relay/%d/set", s_base_topic, ch);
        snprintf(stat_t, sizeof(stat_t), "%s/relay/%d/state", s_base_topic, ch);

        char payload[768];
        snprintf(payload, sizeof(payload,
            "{\"
            \"name\":\"%s Relay %d\",
            \"unique_id\":\"%s_relay%d\",
            \"command_topic\":\"%s\",
            \"state_topic\":\"%s\",
            \"availability_topic\":\"%s\",
            \"payload_available\":\"online\",
            \"payload_not_available\":\"offline\",\"payload_on\":\"ON\",\"payload_off\":\"OFF\",
            %s
            "},
            s_device_name, ch, s_device_id, ch, cmd_t, stat_t, s_availability_topic, dev);
        publish(topic, payload, 1, true);
    }
    ...