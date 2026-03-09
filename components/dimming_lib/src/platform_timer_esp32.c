/**
 * @file platform_timer_esp32.c
 * @brief ESP32 platform timer implementation
 */

#include "platform_timer.h"

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdlib.h>

typedef struct {
    esp_timer_handle_t esp_timer;
    platform_timer_callback_t user_callback;
    void *user_arg;
    uint64_t period_us;
    bool is_periodic;
    bool is_running;
} esp32_timer_context_t;

static void esp32_timer_callback(void *arg)
{
    esp32_timer_context_t *ctx = (esp32_timer_context_t *)arg;
    if (ctx && ctx->user_callback) {
        ctx->user_callback(ctx->user_arg);
    }
}

bool platform_timer_create(const platform_timer_config_t *config, platform_timer_handle_t *handle)
{
    if (!config || !handle || !config->callback) {
        return false;
    }

    esp32_timer_context_t *ctx = (esp32_timer_context_t *)calloc(1, sizeof(esp32_timer_context_t));
    if (!ctx) {
        return false;
    }

    ctx->user_callback = config->callback;
    ctx->user_arg = config->callback_arg;
    ctx->period_us = (uint64_t)(config->period_ms > 0U ? config->period_ms : 1U) * 1000ULL;
    ctx->is_periodic = config->is_periodic;
    ctx->is_running = false;

    esp_timer_create_args_t timer_args = {
        .callback = esp32_timer_callback,
        .arg = ctx,
        .dispatch_method = ESP_TIMER_TASK,
        .name = config->name ? config->name : "platform_timer",
        .skip_unhandled_events = false,
    };

    if (esp_timer_create(&timer_args, &ctx->esp_timer) != ESP_OK) {
        free(ctx);
        return false;
    }

    *handle = (platform_timer_handle_t)ctx;
    return true;
}

void platform_timer_delete(platform_timer_handle_t handle)
{
    if (!handle) {
        return;
    }

    esp32_timer_context_t *ctx = (esp32_timer_context_t *)handle;
    if (ctx->is_running) {
        (void)esp_timer_stop(ctx->esp_timer);
    }

    (void)esp_timer_delete(ctx->esp_timer);
    free(ctx);
}

bool platform_timer_start(platform_timer_handle_t handle)
{
    if (!handle) {
        return false;
    }

    esp32_timer_context_t *ctx = (esp32_timer_context_t *)handle;
    if (ctx->is_running) {
        return true;
    }

    esp_err_t err = ctx->is_periodic
                        ? esp_timer_start_periodic(ctx->esp_timer, ctx->period_us)
                        : esp_timer_start_once(ctx->esp_timer, ctx->period_us);
    if (err != ESP_OK) {
        return false;
    }

    ctx->is_running = true;
    return true;
}

bool platform_timer_stop(platform_timer_handle_t handle)
{
    if (!handle) {
        return false;
    }

    esp32_timer_context_t *ctx = (esp32_timer_context_t *)handle;
    if (!ctx->is_running) {
        return true;
    }

    if (esp_timer_stop(ctx->esp_timer) != ESP_OK) {
        return false;
    }

    ctx->is_running = false;
    return true;
}

bool platform_timer_is_running(platform_timer_handle_t handle)
{
    if (!handle) {
        return false;
    }

    esp32_timer_context_t *ctx = (esp32_timer_context_t *)handle;
    return ctx->is_running;
}

uint32_t platform_get_time_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

void platform_delay_ms(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

