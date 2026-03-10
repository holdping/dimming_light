#include "light_bulb.h"

#include <stdlib.h>

#include "platform_timer.h"

#define LIGHT_BULB_MIN_CHANNEL_COUNT 3U
#define LIGHT_BULB_WAIT_STEP_MS      20U

struct light_bulb {
    dimming_handle_t dimming;
    light_bulb_driver_t driver;
    light_bulb_scene_t scene;
    uint8_t channel_count;
    uint32_t max_value;
};

/* dimming_driver_cb_t has no user argument, so this module currently supports one active instance. */
static struct light_bulb *s_active_bulb = NULL;

static void light_bulb_driver_callback(uint8_t channel, uint32_t value)
{
    struct light_bulb *bulb = s_active_bulb;
    if (!bulb || !bulb->driver.set_channel || channel >= bulb->channel_count) {
        return;
    }

    (void)bulb->driver.set_channel(bulb->driver.user_ctx, channel, value, bulb->max_value);
}

void light_bulb_get_default_config(light_bulb_config_t *config)
{
    if (!config) {
        return;
    }

    config->dimming_timer_period_ms = 12U;
    config->max_value = 255U;
    config->scene.fade_duration_ms = 1200U;
    config->scene.hold_duration_ms = 300U;
    config->scene.gamma_type = GAMMA_22;
    config->scene.gamma_enabled = true;
    config->driver.user_ctx = NULL;
    config->driver.channel_count = LIGHT_BULB_MIN_CHANNEL_COUNT;
    config->driver.init = NULL;
    config->driver.deinit = NULL;
    config->driver.set_channel = NULL;
}

light_bulb_handle_t light_bulb_create(const light_bulb_config_t *config)
{
    if (!config || !config->driver.set_channel || s_active_bulb) {
        return NULL;
    }

    uint8_t channel_count = config->driver.channel_count;
    if (channel_count < LIGHT_BULB_MIN_CHANNEL_COUNT) {
        return NULL;
    }

    struct light_bulb *bulb = (struct light_bulb *)calloc(1, sizeof(struct light_bulb));
    if (!bulb) {
        return NULL;
    }

    bulb->driver = config->driver;
    bulb->scene = config->scene;
    bulb->channel_count = channel_count;
    bulb->max_value = (config->max_value > 0U) ? config->max_value : 255U;

    if (bulb->driver.init && !bulb->driver.init(bulb->driver.user_ctx)) {
        free(bulb);
        return NULL;
    }

    dimming_gamma_config_t *gamma_configs =
        (dimming_gamma_config_t *)calloc(channel_count, sizeof(dimming_gamma_config_t));
    dimming_fade_config_t *fade_configs =
        (dimming_fade_config_t *)calloc(channel_count, sizeof(dimming_fade_config_t));

    if (!gamma_configs || !fade_configs) {
        free(gamma_configs);
        free(fade_configs);
        if (bulb->driver.deinit) {
            bulb->driver.deinit(bulb->driver.user_ctx);
        }
        free(bulb);
        return NULL;
    }

    for (uint8_t i = 0; i < channel_count; i++) {
        gamma_configs[i].enabled = bulb->scene.gamma_enabled;
        gamma_configs[i].type = bulb->scene.gamma_enabled ? bulb->scene.gamma_type : GAMMA_NONE;
        fade_configs[i].enabled = true;
        fade_configs[i].default_duration_ms = bulb->scene.fade_duration_ms;
    }

    dimming_config_t dimming_config = {
        .channel_count = channel_count,
        .max_value = bulb->max_value,
        .timer_period_ms = (config->dimming_timer_period_ms > 0U) ? config->dimming_timer_period_ms : 12U,
        .gamma_configs = gamma_configs,
        .fade_configs = fade_configs
    };

    s_active_bulb = bulb;
    bulb->dimming = dimming_init(&dimming_config, light_bulb_driver_callback);

    free(gamma_configs);
    free(fade_configs);

    if (!bulb->dimming) {
        s_active_bulb = NULL;
        if (bulb->driver.deinit) {
            bulb->driver.deinit(bulb->driver.user_ctx);
        }
        free(bulb);
        return NULL;
    }

    return bulb;
}

void light_bulb_destroy(light_bulb_handle_t handle)
{
    if (!handle) {
        return;
    }

    if (handle->dimming) {
        dimming_deinit(handle->dimming);
        handle->dimming = NULL;
    }

    if (handle->driver.deinit) {
        handle->driver.deinit(handle->driver.user_ctx);
    }

    if (s_active_bulb == handle) {
        s_active_bulb = NULL;
    }

    free(handle);
}

bool light_bulb_apply_scene(light_bulb_handle_t handle, const light_bulb_scene_t *scene)
{
    if (!handle || !handle->dimming || !scene) {
        return false;
    }

    for (uint8_t i = 0; i < handle->channel_count; i++) {
        gamma_type_t gamma = scene->gamma_enabled ? scene->gamma_type : GAMMA_NONE;
        if (!dimming_set_gamma_type(handle->dimming, i, gamma)) {
            return false;
        }
    }

    handle->scene = *scene;
    return true;
}

bool light_bulb_transition_rgb(light_bulb_handle_t handle, uint8_t red, uint8_t green, uint8_t blue)
{
    if (!handle || !handle->dimming || handle->channel_count < LIGHT_BULB_MIN_CHANNEL_COUNT) {
        return false;
    }

    dimming_stop_all_fades(handle->dimming);
    return dimming_set_rgb(handle->dimming, red, green, blue, handle->scene.fade_duration_ms);
}

bool light_bulb_transition_color(light_bulb_handle_t handle, light_bulb_color_t color)
{
    return light_bulb_transition_rgb(handle, color.red, color.green, color.blue);
}

bool light_bulb_is_fading(light_bulb_handle_t handle)
{
    if (!handle || !handle->dimming) {
        return false;
    }

    return dimming_is_fading(handle->dimming);
}

void light_bulb_wait_fade_done(light_bulb_handle_t handle)
{
    if (!handle || !handle->dimming) {
        return;
    }

    while (dimming_is_fading(handle->dimming)) {
        platform_delay_ms(LIGHT_BULB_WAIT_STEP_MS);
    }
}

void light_bulb_stop(light_bulb_handle_t handle)
{
    if (!handle || !handle->dimming) {
        return;
    }

    dimming_stop_all_fades(handle->dimming);
}

void light_bulb_run_color_cycle(light_bulb_handle_t handle,
                                const light_bulb_color_t *colors,
                                size_t color_count,
                                uint32_t loop_count)
{
    if (!handle || !handle->dimming || !colors || color_count == 0U) {
        return;
    }

    uint32_t loops_done = 0U;
    while (loop_count == 0U || loops_done < loop_count) {
        for (size_t i = 0; i < color_count; i++) {
            if (!light_bulb_transition_color(handle, colors[i])) {
                continue;
            }

            light_bulb_wait_fade_done(handle);
            platform_delay_ms(handle->scene.hold_duration_ms);
        }

        if (loop_count != 0U) {
            loops_done++;
        }
    }
}
