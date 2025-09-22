#include "safety.h"
#include "relay.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "time_sync.h"
#include "storage.h"
#include <string.h>

static const char* TAG = "safety";

// Away mode + masks
static bool s_away_mode = CONFIG_AWAY_MODE_DEFAULT_ON;
static uint8_t s_away_allowed_mask = (uint8_t)CONFIG_AWAY_ALLOWED_MASK;

// Per-relay max-on seconds
static uint32_t s_max_on_seconds[4] = {
    CONFIG_R1_MAX_ON_SECONDS,
    CONFIG_R2_MAX_ON_SECONDS,
    CONFIG_R3_MAX_ON_SECONDS,
    CONFIG_R4_MAX_ON_SECONDS
};

// Schedule enforcement
static bool s_schedule_enforce = CONFIG_SCHEDULE_ENFORCE_DEFAULT_ON;
static uint8_t s_schedule_allowed_mask = (uint8_t)CONFIG_SCHEDULE_ALLOWED_MASK;
static uint16_t s_w1_start = CONFIG_SCH_W1_START_MIN;
static uint16_t s_w1_end   = CONFIG_SCH_W1_END_MIN;
static uint16_t s_w2_start = CONFIG_SCH_W2_START_MIN;
static uint16_t s_w2_end   = CONFIG_SCH_W2_END_MIN;

// Per-relay elapsed counters
static uint32_t s_on_elapsed_sec[4] = {0,0,0,0};

// 1s tick timer
static TimerHandle_t s_tick_timer;

static void enforce_away(void) {
    if (!s_away_mode) return;
    for (int ch = 1; ch <= 4; ++ch) {
        bool allowed = (s_away_allowed_mask & (1 << (ch-1))) != 0;
        if (!allowed && relay_get_channel(ch)) {
            ESP_LOGW(TAG, "Away: OFF relay %d", ch);
            relay_set_channel(ch, false);
        }
    }
}

static void enforce_schedule(void) {
    if (!s_schedule_enforce) return;
    uint32_t now_min = time_minutes_since_midnight_local();
    bool within = time_within_windows(s_w1_start, s_w1_end, s_w2_start, s_w2_end, now_min);
    if (within) return;

    // Outside windows: turn off relays not allowed by mask
    for (int ch = 1; ch <= 4; ++ch) {
        bool allowed = (s_schedule_allowed_mask & (1 << (ch-1))) != 0;
        if (!allowed && relay_get_channel(ch)) {
            ESP_LOGW(TAG, "Schedule: OFF relay %d (outside windows)", ch);
            relay_set_channel(ch, false);
        }
    }
}

static void safety_tick_cb(TimerHandle_t xTimer) {
    // Per-relay auto-off
    for (int ch = 1; ch <= 4; ++ch) {
        bool on = relay_get_channel(ch);
        if (on) {
            if (s_on_elapsed_sec[ch-1] < 0xFFFFFFFE) {
                s_on_elapsed_sec[ch-1]++;
            }
            uint32_t limit = s_max_on_seconds[ch-1];
            if (limit > 0 && s_on_elapsed_sec[ch-1] >= limit) {
                ESP_LOGW(TAG, "Relay %d auto-off (max %us)", ch, limit);
                relay_set_channel(ch, false);
                s_on_elapsed_sec[ch-1] = 0;
            }
        } else {
            s_on_elapsed_sec[ch-1] = 0;
        }
    }

    enforce_away();
    enforce_schedule();
}

void safety_init(void) {
    memset(s_on_elapsed_sec, 0, sizeof(s_on_elapsed_sec));

    // Load overrides from NVS if present
    for (int ch = 1; ch <= 4; ++ch) {
        uint32_t v = 0;
        if (storage_get_max_on_seconds(ch, &v)) {
            s_max_on_seconds[ch-1] = v;
        }
    }
    uint16_t s,e;
    if (storage_get_schedule_window(1, &s, &e)) { s_w1_start = s; s_w1_end = e; }
    if (storage_get_schedule_window(2, &s, &e)) { s_w2_start = s; s_w2_end = e; }

    if (s_tick_timer == NULL) {
        s_tick_timer = xTimerCreate("safety_tick", pdMS_TO_TICKS(1000), pdTRUE, NULL, safety_tick_cb);
        if (s_tick_timer) xTimerStart(s_tick_timer, 0);
    }

    ESP_LOGI(TAG, "Safety init: away=%d, away_mask=0x%02X, schedule=%d, sch_mask=0x%02X, W1=%u..%u, W2=%u..%u",
             s_away_mode, s_away_allowed_mask, s_schedule_enforce, s_schedule_allowed_mask,
             s_w1_start, s_w1_end, s_w2_start, s_w2_end);
}

void safety_on_relay_state_change(int channel, bool on) {
    if (channel < 1 || channel > 4) return;
    if (!on) {
        s_on_elapsed_sec[channel-1] = 0;
        return;
    }
    // If turning ON, check policy
    if (s_away_mode) {
        bool allowed = (s_away_allowed_mask & (1 << (channel-1))) == 0;
        if (!allowed) {
            ESP_LOGW(TAG, "Blocked ON relay %d (away)", channel);
            relay_set_channel(channel, false);
            return;
        }
    }
    if (s_schedule_enforce) {
        uint32_t now_min = time_minutes_since_midnight_local();
        bool within = time_within_windows(s_w1_start, s_w1_end, s_w2_start, s_w2_end, now_min);
        bool allowed = (s_schedule_allowed_mask & (1 << (channel-1))) == 0;
        if (!within && !allowed) {
            ESP_LOGW(TAG, "Blocked ON relay %d (outside schedule)", channel);
            relay_set_channel(channel, false);
            return;
        }
    }
    s_on_elapsed_sec[channel-1] = 0;
}

bool safety_can_turn_on(int channel) {
    if (channel < 1 || channel > 4) return false;
    if (s_away_mode) {
        if ((s_away_allowed_mask & (1 << (channel-1))) == 0) return false;
    }
    if (s_schedule_enforce) {
        uint32_t now_min = time_minutes_since_midnight_local();
        bool within = time_within_windows(s_w1_start, s_w1_end, s_w2_start, s_w2_end, now_min);
        bool allowed = (s_schedule_allowed_mask & (1 << (channel-1))) == 0;
        if (!within && !allowed) return false;
    }
    return true;
}

void safety_set_away_mode(bool on) {
    if (s_away_mode == on) return;
    s_away_mode = on;
    ESP_LOGW(TAG, "Away mode -> %d", s_away_mode);
    if (s_away_mode) safety_apply_policy_now();
}

bool safety_get_away_mode(void) {
    return s_away_mode;
}

void safety_set_max_on_seconds(int channel, uint32_t seconds) {
    if (channel < 1 || channel > 4) return;
    s_max_on_seconds[channel-1] = seconds;
    storage_set_max_on_seconds(channel, seconds);
    ESP_LOGI(TAG, "Relay %d max-on set to %us", channel, seconds);
}

uint32_t safety_get_max_on_seconds(int channel) {
    if (channel < 1 || channel > 4) return 0;
    return s_max_on_seconds[channel-1];
}

void safety_set_schedule_windows(uint16_t w1_start_min, uint16_t w1_end_min,
                                 uint16_t w2_start_min, uint16_t w2_end_min) {
    s_w1_start = w1_start_min; s_w1_end = w1_end_min;
    s_w2_start = w2_start_min; s_w2_end = w2_end_min;
    storage_set_schedule_window(1, s_w1_start, s_w1_end);
    storage_set_schedule_window(2, s_w2_start, s_w2_end);
    ESP_LOGI(TAG, "Schedule windows set: W1=%u..%u W2=%u..%u", s_w1_start, s_w1_end, s_w2_start, s_w2_end);
    safety_apply_policy_now();
}

void safety_get_schedule_windows(uint16_t* w1_start_min, uint16_t* w1_end_min,
                                 uint16_t* w2_start_min, uint16_t* w2_end_min) {
    if (w1_start_min) *w1_start_min = s_w1_start;
    if (w1_end_min)   *w1_end_min   = s_w1_end;
    if (w2_start_min) *w2_start_min = s_w2_start;
    if (w2_end_min)   *w2_end_min   = s_w2_end;
}

void safety_set_schedule_enforce(bool on) {
    if (s_schedule_enforce == on) return;
    s_schedule_enforce = on;
    ESP_LOGW(TAG, "Schedule enforce -> %d", s_schedule_enforce);
    safety_apply_policy_now();
}

bool safety_get_schedule_enforce(void) {
    return s_schedule_enforce;
}

void safety_apply_policy_now(void) {
    enforce_away();
    enforce_schedule();
}

uint8_t safety_get_away_allowed_mask(void) {
    return s_away_allowed_mask;
}

uint8_t safety_get_schedule_allowed_mask(void) {
    return s_schedule_allowed_mask;
}