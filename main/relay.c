#include "relay.h"
#include "esp_log.h"

static const char* TAG = "relay";

static const gpio_num_t relay_gpios[4] = {
    RELAY1_GPIO, RELAY2_GPIO, RELAY3_GPIO, RELAY4_GPIO
};

static bool relay_states[4] = {false, false, false, false};

esp_err_t relay_init(void) {
    gpio_config_t cfg = {
        .pin_bit_mask =
            (1ULL << RELAY1_GPIO) |
            (1ULL << RELAY2_GPIO) |
            (1ULL << RELAY3_GPIO) |
            (1ULL << RELAY4_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    esp_err_t err = gpio_config(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gpio_config failed: %s", esp_err_to_name(err));
        return err;
    }

    // Safe default: all OFF
    for (int i = 0; i < 4; ++i) {
        gpio_set_level(relay_gpios[i], !RELAY_ACTIVE_LEVEL);
        relay_states[i] = false;
    }
    ESP_LOGI(TAG, "Relays initialized (active level=%d)", RELAY_ACTIVE_LEVEL);
    return ESP_OK;
}

esp_err_t relay_set_channel(int channel, bool on) {
    if (channel < 1 || channel > 4) {
        return ESP_ERR_INVALID_ARG;
    }
    gpio_num_t gpio = relay_gpios[channel - 1];
    esp_err_t err = gpio_set_level(gpio, on ? RELAY_ACTIVE_LEVEL : !RELAY_ACTIVE_LEVEL);
    if (err == ESP_OK) {
        relay_states[channel - 1] = on;
    }
    return err;
}

esp_err_t relay_set_all(bool on) {
    for (int i = 0; i < 4; ++i) {
        esp_err_t err = gpio_set_level(relay_gpios[i], on ? RELAY_ACTIVE_LEVEL : !RELAY_ACTIVE_LEVEL);
        if (err != ESP_OK) return err;
        relay_states[i] = on;
    }
    return ESP_OK;
}

bool relay_get_channel(int channel) {
    if (channel < 1 || channel > 4) return false;
    return relay_states[channel - 1];
}