#include "time_sync.h"
#include "esp_sntp.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "esp_idf_version.h"
#include <time.h>
#include <sys/time.h>
#include <string.h>
#include <stdlib.h>

static const char* TAG = "time_sync";
static volatile bool s_synced = false;

static void sntp_sync_cb(struct timeval *tv) {
    (void)tv;
    s_synced = true;
    ESP_LOGI(TAG, "Time synchronized");
}

void time_sync_init(void) {
    // TZ
    setenv("TZ", CONFIG_TIME_TZ, 1);
    tzset();

    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, CONFIG_TIME_NTP_SERVER);
    sntp_set_time_sync_notification_cb(sntp_sync_cb);
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    sntp_set_sync_mode(SNTP_SYNC_MODE_SMOOTH);
#endif
    sntp_init();
    ESP_LOGI(TAG, "SNTP init: server=%s, TZ=%s", CONFIG_TIME_NTP_SERVER, CONFIG_TIME_TZ);
}

bool time_is_synced(void) {
    return s_synced;
}

uint32_t time_minutes_since_midnight_local(void) {
    time_t now = 0;
    time(&now);
    struct tm lt;
    localtime_r(&now, &lt);
    return (uint32_t)lt.tm_hour * 60u + (uint32_t)lt.tm_min;
}

static bool within_window(uint32_t now, uint16_t start, uint16_t end) {
    // Support wrap-around (e.g., 22:00..02:00)
    if (start == end) return false; // disabled
    if (start < end) {
        return (now >= start) && (now < end);
    } else {
        return (now >= start) || (now < end);
    }
}

bool time_within_windows(uint16_t w1_start_min, uint16_t w1_end_min,
                         uint16_t w2_start_min, uint16_t w2_end_min,
                         uint32_t now_min_local) {
    return within_window(now_min_local, w1_start_min, w1_end_min) ||
           within_window(now_min_local, w2_start_min, w2_end_min);
}