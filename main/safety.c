#include "safety.h"
#include "storage.h"
#include "relay.h"
#include "ha_mqtt.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "time.h"
#include "sdkconfig.h"
#include <inttypes.h>

static const char* TAG = "safety";

static bool s_away = false;
static bool s_sched_enf = false;
static uint16_t s_w1s = 0, s_w1e = 0, s_w2s = 0, s_w2e = 0; // minutes since midnight
static uint32_t s_max_on_sec[4] = {0,0,0,0};

// Track ON start times (microseconds)
static int64_t s_on_start_us[4] = {0,0,0,0};

static int minute_of_day(void) {
    time_t now; time(&now);
    struct tm tmv; localtime_r(&now, &tmv);
    return tmv.tm_hour * 60 + tmv.tm_min;
}

static bool within_window(uint16_t start, uint16_t end, int minute) {
    if (start == end) return false; // disabled
    if (start < end) {
        return minute >= (int)start && minute < (int)end;
    } else { // overnight wrapping
        return minute >= (int)start || minute < (int)end;
    }
}

void safety_init(void) {
    // Load persisted values or defaults from Kconfig
    bool b;
    storage_get_bool("away", &b, CONFIG_SAFETY_AWAY_DEFAULT); s_away = b;
    storage_get_bool("sched_enf", &b, CONFIG_SAFETY_SCHEDULE_ENFORCE_DEFAULT); s_sched_enf = b;

    uint32_t v;
    storage_get_u32("w1s", &v, CONFIG_SAFETY_W1_START_DEFAULT); s_w1s = (uint16_t)v;
    storage_get_u32("w1e", &v, CONFIG_SAFETY_W1_END_DEFAULT); s_w1e = (uint16_t)v;
    storage_get_u32("w2s", &v, CONFIG_SAFETY_W2_START_DEFAULT); s_w2s = (uint16_t)v;
    storage_get_u32("w2e", &v, CONFIG_SAFETY_W2_END_DEFAULT); s_w2e = (uint16_t)v;

    storage_get_u32("max1", &v, CONFIG_SAFETY_RELAY1_MAX_ON_MIN_DEFAULT * 60U); s_max_on_sec[0] = v;
    storage_get_u32("max2", &v, CONFIG_SAFETY_RELAY2_MAX_ON_MIN_DEFAULT * 60U); s_max_on_sec[1] = v;
    storage_get_u32("max3", &v, CONFIG_SAFETY_RELAY3_MAX_ON_MIN_DEFAULT * 60U); s_max_on_sec[2] = v;
    storage_get_u32("max4", &v, CONFIG_SAFETY_RELAY4_MAX_ON_MIN_DEFAULT * 60U); s_max_on_sec[3] = v;

    // Initialize start times if relays are already on (unlikely at boot)
    for (int i=0;i<4;i++) {
        if (relay_get_channel(i+1)) s_on_start_us[i] = esp_timer_get_time();
    }
}

bool safety_can_turn_on(int channel) {
    if (channel < 1 || channel > 4) return false;
    if (s_away) return false;
    if (!s_sched_enf) return true;

    int mod = minute_of_day();
    bool ok = within_window(s_w1s, s_w1e, mod) || within_window(s_w2s, s_w2e, mod);
    return ok;
}

void safety_on_relay_state_change(int channel, bool on) {
    if (channel < 1 || channel > 4) return;
    int idx = channel - 1;
    if (on) {
        s_on_start_us[idx] = esp_timer_get_time();
    } else {
        s_on_start_us[idx] = 0;
    }
}

static void enforce_max_on(void) {
    int64_t now = esp_timer_get_time();
    for (int i=0;i<4;i++) {
        if (relay_get_channel(i+1) && s_max_on_sec[i] > 0 && s_on_start_us[i] > 0) {
            int64_t elapsed_sec = (now - s_on_start_us[i]) / 1000000LL;
            if (elapsed_sec >= (int64_t)s_max_on_sec[i]) {
                ESP_LOGW(TAG, "Relay %d exceeded max-on (%" PRId64 "s >= %us), turning OFF", i+1, elapsed_sec, s_max_on_sec[i]);
                relay_set_channel(i+1, false);
                s_on_start_us[i] = 0;
                ha_mqtt_publish_relay_state(i+1, false);
            }
        }
    }
}

void safety_apply_policy_now(void) {
    // Enforce away/schedule immediately
    if (s_away || s_sched_enf) {
        int mod = minute_of_day();
        for (int ch=1; ch<=4; ++ch) {
            bool should_off = false;
            if (s_away) should_off = true;
            if (!should_off && s_sched_enf) {
                bool ok = within_window(s_w1s, s_w1e, mod) || within_window(s_w2s, s_w2e, mod);
                should_off = !ok;
            }
            if (should_off && relay_get_channel(ch)) {
                relay_set_channel(ch, false);
                s_on_start_us[ch-1] = 0;
                ha_mqtt_publish_relay_state(ch, false);
            }
        }
    }
    enforce_max_on();
}

bool safety_get_away_mode(void) { return s_away; }
void safety_set_away_mode(bool on) {
    s_away = on;
    storage_set_bool("away", on);
}

bool safety_get_schedule_enforce(void) { return s_sched_enf; }
void safety_set_schedule_enforce(bool on) {
    s_sched_enf = on;
    storage_set_bool("sched_enf", on);
}

void safety_get_schedule_windows(uint16_t* w1_start, uint16_t* w1_end, uint16_t* w2_start, uint16_t* w2_end) {
    if (w1_start) *w1_start = s_w1s;
    if (w1_end) *w1_end = s_w1e;
    if (w2_start) *w2_start = s_w2s;
    if (w2_end) *w2_end = s_w2e;
}
safety_set_schedule_windows(uint16_t w1_start, uint16_t w1_end, uint16_t w2_start, uint16_t w2_end) {
    s_w1s = w1_start; s_w1e = w1_end; s_w2s = w2_start; s_w2e = w2_end;
    storage_set_u32("w1s", s_w1s);
    storage_set_u32("w1e", s_w1e);
    storage_set_u32("w2s", s_w2s);
    storage_set_u32("w2e", s_w2e);
}

void safety_set_max_on_seconds(int channel, uint32_t seconds) {
    if (channel < 1 || channel > 4) return;
    s_max_on_sec[channel-1] = seconds;
    const char* keys[] = {"max1","max2","max3","max4"};
    storage_set_u32(keys[channel-1], seconds);
}
uint32_t safety_get_max_on_seconds(int channel) {
    if (channel < 1 || channel > 4) return 0;
    return s_max_on_sec[channel-1];
}