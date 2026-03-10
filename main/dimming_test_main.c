#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "driver/ledc.h"

#include "light_bulb.h"

#define APP_CHANNEL_COUNT   3U
#define APP_MAX_VALUE       255U

typedef struct {
    int gpio_red;
    int gpio_green;
    int gpio_blue;
    ledc_timer_t ledc_timer;
    ledc_mode_t ledc_mode;
    ledc_timer_bit_t duty_resolution;
    uint32_t frequency_hz;
    ledc_channel_t channels[APP_CHANNEL_COUNT];
    uint32_t pwm_max_duty;
} esp32_ledc_driver_t;

static esp32_ledc_driver_t s_ledc_driver = {
    .gpio_red = 5,
    .gpio_green = 6,
    .gpio_blue = 7,
    .ledc_timer = LEDC_TIMER_0,
    .ledc_mode = LEDC_LOW_SPEED_MODE,
    .duty_resolution = LEDC_TIMER_13_BIT,
    .frequency_hz = 4000U,
    .channels = {LEDC_CHANNEL_0, LEDC_CHANNEL_1, LEDC_CHANNEL_2},
    .pwm_max_duty = 8191U
};

static const light_bulb_color_t s_color_cycle[] = {
    {255, 0, 0},
    {0, 255, 0},
    {0, 0, 255},
    {255, 255, 255},
    {0, 255, 255},
    {255, 0, 255},
    {255, 255, 0}
};

static bool app_ledc_driver_init(void *user_ctx)
{
    esp32_ledc_driver_t *driver = (esp32_ledc_driver_t *)user_ctx;
    if (!driver) {
        return false;
    }

    ledc_timer_config_t timer_cfg = {
        .speed_mode = driver->ledc_mode,
        .duty_resolution = driver->duty_resolution,
        .timer_num = driver->ledc_timer,
        .freq_hz = (int)driver->frequency_hz,
        .clk_cfg = LEDC_AUTO_CLK
    };

    if (ledc_timer_config(&timer_cfg) != ESP_OK) {
        return false;
    }

    const int gpios[APP_CHANNEL_COUNT] = {driver->gpio_red, driver->gpio_green, driver->gpio_blue};
    for (uint8_t i = 0; i < APP_CHANNEL_COUNT; i++) {
        ledc_channel_config_t channel_cfg = {
            .speed_mode = driver->ledc_mode,
            .channel = driver->channels[i],
            .timer_sel = driver->ledc_timer,
            .intr_type = LEDC_INTR_DISABLE,
            .gpio_num = gpios[i],
            .duty = 0,
            .hpoint = 0
        };

        if (ledc_channel_config(&channel_cfg) != ESP_OK) {
            return false;
        }
    }

    uint32_t bits = (uint32_t)driver->duty_resolution;
    driver->pwm_max_duty = (bits > 0U && bits < 32U) ? ((1UL << bits) - 1UL) : 8191U;
    return true;
}

static void app_ledc_driver_deinit(void *user_ctx)
{
    (void)user_ctx;
}

static bool app_ledc_driver_set_channel(void *user_ctx, uint8_t channel, uint32_t value, uint32_t max_value)
{
    esp32_ledc_driver_t *driver = (esp32_ledc_driver_t *)user_ctx;
    if (!driver || channel >= APP_CHANNEL_COUNT || max_value == 0U) {
        return false;
    }

    uint32_t duty = (value * driver->pwm_max_duty + (max_value / 2U)) / max_value;
    if (ledc_set_duty(driver->ledc_mode, driver->channels[channel], duty) != ESP_OK) {
        return false;
    }

    return (ledc_update_duty(driver->ledc_mode, driver->channels[channel]) == ESP_OK);
}

void app_main(void)
{
    printf("\n=== Light Bulb Portable Demo (ESP32 Adapter) ===\n");

    light_bulb_config_t bulb_config;
    light_bulb_get_default_config(&bulb_config);

    bulb_config.max_value = APP_MAX_VALUE;
    bulb_config.driver.user_ctx = &s_ledc_driver;
    bulb_config.driver.channel_count = APP_CHANNEL_COUNT;
    bulb_config.driver.init = app_ledc_driver_init;
    bulb_config.driver.deinit = app_ledc_driver_deinit;
    bulb_config.driver.set_channel = app_ledc_driver_set_channel;

    bulb_config.scene.fade_duration_ms = 3000U;
    bulb_config.scene.hold_duration_ms = 1500U;
    bulb_config.scene.gamma_enabled = true;
    bulb_config.scene.gamma_type = GAMMA_22;

    light_bulb_handle_t bulb = light_bulb_create(&bulb_config);
    if (!bulb) {
        printf("Failed to create portable light_bulb\n");
        return;
    }

    light_bulb_run_color_cycle(
        bulb,
        s_color_cycle,
        sizeof(s_color_cycle) / sizeof(s_color_cycle[0]),
        0U
    );
}
