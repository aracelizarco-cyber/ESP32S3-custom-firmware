#include "time_sync.h"

#include "esp_log.h"
#include "esp_sntp.h"
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <time.h>
#include <string.h>

static const char* TAG = "time_sync";

void time_sync_init(void) {
    // Timezone
    setenv("TZ", CONFIG_TZ_STRING, 1);
    tzset();

    // SNTP
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, CONFIG_SNTP_SERVER);
    sntp_init();

    // Wait for time to be set roughly
    for (int i = 0; i < 20; ++i) {
        time_t now = 0;
        time(&now);
        if (now > 1609459200) { // 2021-01-01
            struct tm tm_info;
            localtime_r(&now, &tm_info);
            char buf[64];
            strftime(buf, sizeof(buf), "%F %T %Z", &tm_info);
            ESP_LOGI(TAG, "Time synced: %s", buf);
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    ESP_LOGW(TAG, "SNTP time not synced yet");
}