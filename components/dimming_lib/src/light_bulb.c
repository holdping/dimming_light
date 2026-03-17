#include "light_bulb.h"

#include <math.h>
#include <stdlib.h>

#include "platform_timer.h"

#define LIGHT_BULB_MIN_CHANNEL_COUNT 1U
#define LIGHT_BULB_WAIT_STEP_MS      20U

typedef struct {
    uint8_t channels[3];
    uint32_t values[3];
    uint8_t count;
} light_bulb_transition_pack_t;

struct light_bulb {
    dimming_handle_t dimming;
    light_bulb_driver_t driver;
    light_bulb_scene_t scene;
    light_bulb_channel_map_t map;
    uint8_t channel_count;
    uint32_t max_value;
    bool effect_running;
    uint32_t rng_state;
};

/* dimming_driver_cb_t has no user argument, so this module currently supports one active instance. */
static struct light_bulb *s_active_bulb = NULL;

static bool light_bulb_channel_in_range(uint8_t channel, uint8_t channel_count)
{
    return channel < channel_count;
}

static bool light_bulb_map_channel_valid(uint8_t channel, uint8_t channel_count)
{
    return (channel == LIGHT_BULB_CHANNEL_INVALID) || light_bulb_channel_in_range(channel, channel_count);
}

static bool light_bulb_has_rgb_channels(light_bulb_handle_t handle)
{
    if (!handle) {
        return false;
    }

    return (handle->map.red != LIGHT_BULB_CHANNEL_INVALID) &&
           (handle->map.green != LIGHT_BULB_CHANNEL_INVALID) &&
           (handle->map.blue != LIGHT_BULB_CHANNEL_INVALID);
}

static bool light_bulb_has_cct_channels(light_bulb_handle_t handle)
{
    if (!handle) {
        return false;
    }

    return (handle->map.warm != LIGHT_BULB_CHANNEL_INVALID) &&
           (handle->map.cool != LIGHT_BULB_CHANNEL_INVALID);
}

static uint32_t light_bulb_clamp_u32(uint32_t value, uint32_t max_value)
{
    return (value > max_value) ? max_value : value;
}

static uint32_t light_bulb_scale_u8_to_max(uint8_t value, uint32_t max_value)
{
    if (max_value == 0U) {
        return 0U;
    }
    return ((uint32_t)value * max_value + 127U) / 255U;
}

static uint8_t light_bulb_scale_max_to_u8(uint32_t value, uint32_t max_value)
{
    if (max_value == 0U) {
        return 0U;
    }

    uint32_t scaled = (value * 255U + (max_value / 2U)) / max_value;
    if (scaled > 255U) {
        scaled = 255U;
    }
    return (uint8_t)scaled;
}

static uint32_t light_bulb_next_rand(light_bulb_handle_t handle)
{
    handle->rng_state = handle->rng_state * 1664525U + 1013904223U;
    return handle->rng_state;
}

static void light_bulb_delay_with_abort(light_bulb_handle_t handle, uint32_t delay_ms)
{
    uint32_t remaining = delay_ms;
    while (remaining > 0U && handle && handle->effect_running) {
        uint32_t step = (remaining > LIGHT_BULB_WAIT_STEP_MS) ? LIGHT_BULB_WAIT_STEP_MS : remaining;
        platform_delay_ms(step);
        remaining -= step;
    }
}

static void light_bulb_wait_fade_done_with_abort(light_bulb_handle_t handle)
{
    if (!handle || !handle->dimming) {
        return;
    }

    while (handle->effect_running && dimming_is_fading(handle->dimming)) {
        platform_delay_ms(LIGHT_BULB_WAIT_STEP_MS);
    }
}

static bool light_bulb_prepare_transition_duration(light_bulb_handle_t handle,
                                                   const uint8_t *channels,
                                                   const uint32_t *values,
                                                   uint8_t count,
                                                   uint32_t duration_ms)
{
    if (!handle || !handle->dimming || !channels || !values || count == 0U) {
        return false;
    }

    for (uint8_t i = 0; i < count; i++) {
        if (!light_bulb_channel_in_range(channels[i], handle->channel_count)) {
            return false;
        }
    }

    dimming_stop_all_fades(handle->dimming);
    return dimming_set_multiple_with_fade(handle->dimming, channels, values, count, duration_ms);
}

static bool light_bulb_set_channels_immediate(light_bulb_handle_t handle,
                                              const uint8_t *channels,
                                              const uint32_t *values,
                                              uint8_t count)
{
    if (!handle || !handle->dimming || !channels || !values || count == 0U) {
        return false;
    }

    dimming_stop_all_fades(handle->dimming);

    bool ok = true;
    for (uint8_t i = 0; i < count; i++) {
        if (!light_bulb_channel_in_range(channels[i], handle->channel_count)) {
            return false;
        }
        if (!dimming_set_immediate(handle->dimming, channels[i], values[i])) {
            ok = false;
        }
    }
    return ok;
}

static bool light_bulb_build_rgb_transition(light_bulb_handle_t handle,
                                            light_bulb_color_t color,
                                            light_bulb_transition_pack_t *pack)
{
    if (!handle || !pack) {
        return false;
    }

    if (handle->map.red == LIGHT_BULB_CHANNEL_INVALID ||
        handle->map.green == LIGHT_BULB_CHANNEL_INVALID ||
        handle->map.blue == LIGHT_BULB_CHANNEL_INVALID) {
        return false;
    }

    pack->channels[0] = handle->map.red;
    pack->channels[1] = handle->map.green;
    pack->channels[2] = handle->map.blue;
    pack->values[0] = light_bulb_scale_u8_to_max(color.red, handle->max_value);
    pack->values[1] = light_bulb_scale_u8_to_max(color.green, handle->max_value);
    pack->values[2] = light_bulb_scale_u8_to_max(color.blue, handle->max_value);
    pack->count = 3U;
    return true;
}

static bool light_bulb_build_cct_transition(light_bulb_handle_t handle,
                                            uint32_t warm,
                                            uint32_t cool,
                                            light_bulb_transition_pack_t *pack)
{
    if (!handle || !pack) {
        return false;
    }

    if (handle->map.warm == LIGHT_BULB_CHANNEL_INVALID ||
        handle->map.cool == LIGHT_BULB_CHANNEL_INVALID) {
        return false;
    }

    pack->channels[0] = handle->map.warm;
    pack->channels[1] = handle->map.cool;
    pack->values[0] = light_bulb_clamp_u32(warm, handle->max_value);
    pack->values[1] = light_bulb_clamp_u32(cool, handle->max_value);
    pack->count = 2U;
    return true;
}

static bool light_bulb_transition_color_duration(light_bulb_handle_t handle,
                                                 light_bulb_color_t color,
                                                 uint32_t duration_ms)
{
    light_bulb_transition_pack_t pack;
    if (!light_bulb_build_rgb_transition(handle, color, &pack)) {
        return false;
    }

    return light_bulb_prepare_transition_duration(handle, pack.channels, pack.values, pack.count, duration_ms);
}

static bool light_bulb_transition_cct_duration(light_bulb_handle_t handle,
                                               uint32_t warm,
                                               uint32_t cool,
                                               uint32_t duration_ms)
{
    light_bulb_transition_pack_t pack;
    if (!light_bulb_build_cct_transition(handle, warm, cool, &pack)) {
        return false;
    }

    return light_bulb_prepare_transition_duration(handle, pack.channels, pack.values, pack.count, duration_ms);
}

static bool light_bulb_set_color_immediate(light_bulb_handle_t handle, light_bulb_color_t color)
{
    light_bulb_transition_pack_t pack;
    if (!light_bulb_build_rgb_transition(handle, color, &pack)) {
        return false;
    }

    return light_bulb_set_channels_immediate(handle, pack.channels, pack.values, pack.count);
}

static bool light_bulb_effect_begin(light_bulb_handle_t handle)
{
    if (!handle || !handle->dimming) {
        return false;
    }

    handle->effect_running = true;
    return true;
}

static void light_bulb_effect_end(light_bulb_handle_t handle)
{
    if (handle) {
        handle->effect_running = false;
    }
}

static light_bulb_color_t light_bulb_hsv_to_rgb(uint16_t hue, uint8_t sat, uint8_t val)
{
    light_bulb_color_t out = {0U, 0U, 0U};
    if (sat == 0U) {
        out.red = val;
        out.green = val;
        out.blue = val;
        return out;
    }

    uint16_t region = hue / 60U;
    uint16_t rem = (hue - region * 60U) * 255U / 60U;

    uint8_t p = (uint8_t)(((uint16_t)val * (255U - sat)) / 255U);
    uint8_t q = (uint8_t)(((uint16_t)val * (255U - ((uint16_t)sat * rem) / 255U)) / 255U);
    uint8_t t = (uint8_t)(((uint16_t)val * (255U - ((uint16_t)sat * (255U - rem)) / 255U)) / 255U);

    switch (region % 6U) {
        case 0: out.red = val; out.green = t; out.blue = p; break;
        case 1: out.red = q; out.green = val; out.blue = p; break;
        case 2: out.red = p; out.green = val; out.blue = t; break;
        case 3: out.red = p; out.green = q; out.blue = val; break;
        case 4: out.red = t; out.green = p; out.blue = val; break;
        default: out.red = val; out.green = p; out.blue = q; break;
    }

    return out;
}

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
    config->map.red = 0U;
    config->map.green = LIGHT_BULB_CHANNEL_INVALID;
    config->map.blue = LIGHT_BULB_CHANNEL_INVALID;
    config->map.warm = 0U;
    config->map.cool = LIGHT_BULB_CHANNEL_INVALID;
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

    if (!light_bulb_map_channel_valid(config->map.red, channel_count) ||
        !light_bulb_map_channel_valid(config->map.green, channel_count) ||
        !light_bulb_map_channel_valid(config->map.blue, channel_count) ||
        !light_bulb_map_channel_valid(config->map.warm, channel_count) ||
        !light_bulb_map_channel_valid(config->map.cool, channel_count)) {
        return NULL;
    }

    struct light_bulb *bulb = (struct light_bulb *)calloc(1, sizeof(struct light_bulb));
    if (!bulb) {
        return NULL;
    }

    bulb->driver = config->driver;
    bulb->scene = config->scene;
    bulb->map = config->map;
    bulb->channel_count = channel_count;
    bulb->max_value = (config->max_value > 0U) ? config->max_value : 255U;
    bulb->effect_running = false;
    bulb->rng_state = platform_get_time_ms() ^ 0xA5A55A5AU;

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
    light_bulb_color_t color = {red, green, blue};
    return light_bulb_transition_color_duration(handle, color, handle ? handle->scene.fade_duration_ms : 0U);
}

bool light_bulb_transition_color(light_bulb_handle_t handle, light_bulb_color_t color)
{
    return light_bulb_transition_color_duration(handle, color, handle ? handle->scene.fade_duration_ms : 0U);
}

bool light_bulb_transition_cct(light_bulb_handle_t handle, uint32_t warm, uint32_t cool)
{
    return light_bulb_transition_cct_duration(handle, warm, cool, handle ? handle->scene.fade_duration_ms : 0U);
}

bool light_bulb_transition_cct_pair(light_bulb_handle_t handle, light_bulb_cct_t cct)
{
    return light_bulb_transition_cct(handle, cct.warm, cct.cool);
}

bool light_bulb_transition_cw(light_bulb_handle_t handle, uint32_t cool_value)
{
    if (!handle || !handle->dimming || handle->map.cool == LIGHT_BULB_CHANNEL_INVALID) {
        return false;
    }

    if (handle->map.warm != LIGHT_BULB_CHANNEL_INVALID && handle->map.warm != handle->map.cool) {
        return light_bulb_transition_cct(handle, 0U, cool_value);
    }

    uint8_t channel = handle->map.cool;
    uint32_t value = light_bulb_clamp_u32(cool_value, handle->max_value);
    return light_bulb_prepare_transition_duration(handle, &channel, &value, 1U, handle->scene.fade_duration_ms);
}

bool light_bulb_transition_ww(light_bulb_handle_t handle, uint32_t warm_value)
{
    if (!handle || !handle->dimming || handle->map.warm == LIGHT_BULB_CHANNEL_INVALID) {
        return false;
    }

    if (handle->map.cool != LIGHT_BULB_CHANNEL_INVALID && handle->map.cool != handle->map.warm) {
        return light_bulb_transition_cct(handle, warm_value, 0U);
    }

    uint8_t channel = handle->map.warm;
    uint32_t value = light_bulb_clamp_u32(warm_value, handle->max_value);
    return light_bulb_prepare_transition_duration(handle, &channel, &value, 1U, handle->scene.fade_duration_ms);
}

bool light_bulb_transition_cct_percent(light_bulb_handle_t handle, uint8_t cool_percent, uint32_t brightness)
{
    if (!handle) {
        return false;
    }

    if (cool_percent > 100U) {
        cool_percent = 100U;
    }

    uint32_t clamped_brightness = light_bulb_clamp_u32(brightness, handle->max_value);
    uint32_t cool = (clamped_brightness * (uint32_t)cool_percent) / 100U;
    uint32_t warm = clamped_brightness - cool;
    return light_bulb_transition_cct(handle, warm, cool);
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

    if (!light_bulb_effect_begin(handle)) {
        return;
    }

    uint32_t loops_done = 0U;
    while (handle->effect_running && (loop_count == 0U || loops_done < loop_count)) {
        for (size_t i = 0; i < color_count && handle->effect_running; i++) {
            if (!light_bulb_transition_color(handle, colors[i])) {
                continue;
            }
            light_bulb_wait_fade_done_with_abort(handle);
            light_bulb_delay_with_abort(handle, handle->scene.hold_duration_ms);
        }

        if (loop_count != 0U) {
            loops_done++;
        }
    }

    light_bulb_effect_end(handle);
}

void light_bulb_run_rainbow_effect(light_bulb_handle_t handle, uint32_t duration_ms, uint32_t loop_count)
{
    if (!handle || !handle->dimming || !light_bulb_effect_begin(handle)) {
        return;
    }

    const uint16_t steps = 24U;
    uint32_t step_ms = (duration_ms > 0U) ? (duration_ms / steps) : 80U;
    if (step_ms == 0U) {
        step_ms = 1U;
    }

    uint32_t loops_done = 0U;
    while (handle->effect_running && (loop_count == 0U || loops_done < loop_count)) {
        for (uint16_t i = 0; i < steps && handle->effect_running; i++) {
            uint16_t hue = (uint16_t)((360U * i) / steps);
            light_bulb_color_t color = light_bulb_hsv_to_rgb(hue, 255U, 255U);
            (void)light_bulb_transition_color_duration(handle, color, step_ms);
            light_bulb_wait_fade_done_with_abort(handle);
        }
        if (loop_count != 0U) {
            loops_done++;
        }
    }

    light_bulb_effect_end(handle);
}

void light_bulb_run_breath_effect(light_bulb_handle_t handle,
                                  light_bulb_color_t color,
                                  uint32_t period_ms,
                                  uint32_t loop_count)
{
    if (!handle || !handle->dimming || !light_bulb_effect_begin(handle)) {
        return;
    }

    uint32_t half_period = (period_ms > 0U) ? (period_ms / 2U) : 1000U;
    if (half_period == 0U) {
        half_period = 1U;
    }

    const light_bulb_color_t off = LIGHT_BULB_COLOR_OFF;
    uint32_t loops_done = 0U;
    while (handle->effect_running && (loop_count == 0U || loops_done < loop_count)) {
        (void)light_bulb_transition_color_duration(handle, color, half_period);
        light_bulb_wait_fade_done_with_abort(handle);
        if (!handle->effect_running) {
            break;
        }
        (void)light_bulb_transition_color_duration(handle, off, half_period);
        light_bulb_wait_fade_done_with_abort(handle);
        if (loop_count != 0U) {
            loops_done++;
        }
    }

    light_bulb_effect_end(handle);
}

void light_bulb_run_pulse_effect(light_bulb_handle_t handle,
                                 light_bulb_color_t color,
                                 uint32_t period_ms,
                                 uint32_t loop_count)
{
    if (!handle || !handle->dimming || !light_bulb_effect_begin(handle)) {
        return;
    }

    uint32_t cycle_ms = (period_ms > 0U) ? period_ms : 500U;
    uint32_t on_ms = (cycle_ms / 5U > 0U) ? (cycle_ms / 5U) : 1U;
    uint32_t off_ms = (cycle_ms > on_ms) ? (cycle_ms - on_ms) : 1U;
    const light_bulb_color_t off = LIGHT_BULB_COLOR_OFF;

    uint32_t loops_done = 0U;
    while (handle->effect_running && (loop_count == 0U || loops_done < loop_count)) {
        (void)light_bulb_transition_color_duration(handle, color, on_ms);
        light_bulb_wait_fade_done_with_abort(handle);
        if (!handle->effect_running) {
            break;
        }
        (void)light_bulb_set_color_immediate(handle, off);
        light_bulb_delay_with_abort(handle, off_ms);
        if (loop_count != 0U) {
            loops_done++;
        }
    }

    light_bulb_effect_end(handle);
}

void light_bulb_run_strobe_effect(light_bulb_handle_t handle,
                                  light_bulb_color_t color,
                                  uint32_t on_ms,
                                  uint32_t off_ms,
                                  uint32_t loop_count)
{
    if (!handle || !handle->dimming || !light_bulb_effect_begin(handle)) {
        return;
    }

    uint32_t on_delay = (on_ms > 0U) ? on_ms : 50U;
    uint32_t off_delay = (off_ms > 0U) ? off_ms : 50U;
    const light_bulb_color_t off = LIGHT_BULB_COLOR_OFF;

    uint32_t loops_done = 0U;
    while (handle->effect_running && (loop_count == 0U || loops_done < loop_count)) {
        (void)light_bulb_set_color_immediate(handle, color);
        light_bulb_delay_with_abort(handle, on_delay);
        if (!handle->effect_running) {
            break;
        }
        (void)light_bulb_set_color_immediate(handle, off);
        light_bulb_delay_with_abort(handle, off_delay);
        if (loop_count != 0U) {
            loops_done++;
        }
    }

    light_bulb_effect_end(handle);
}

void light_bulb_run_fire_effect(light_bulb_handle_t handle, uint32_t intensity, uint32_t duration_ms)
{
    if (!handle || !handle->dimming || !light_bulb_effect_begin(handle)) {
        return;
    }

    uint32_t level = (intensity > 100U) ? 100U : intensity;
    if (level == 0U) {
        level = 1U;
    }

    uint32_t total_ms = (duration_ms > 0U) ? duration_ms : 5000U;
    uint32_t start = platform_get_time_ms();

    while (handle->effect_running) {
        uint32_t elapsed = platform_get_time_ms() - start;
        if (elapsed >= total_ms) {
            break;
        }

        uint32_t rnd = light_bulb_next_rand(handle);
        uint32_t base_pct = 55U + (rnd % 46U);
        uint32_t cool_pct = 8U + ((rnd >> 8) % 25U);

        uint32_t brightness = (handle->max_value * base_pct * level) / 10000U;
        uint32_t cool = (brightness * cool_pct) / 100U;
        uint32_t warm = brightness;

        (void)light_bulb_transition_cct_duration(handle, warm, cool, 60U);
        light_bulb_wait_fade_done_with_abort(handle);
        light_bulb_delay_with_abort(handle, 40U);
    }

    light_bulb_effect_end(handle);
}

void light_bulb_run_color_wipe(light_bulb_handle_t handle,
                               const light_bulb_color_t *colors,
                               size_t color_count,
                               uint32_t wipe_duration_ms,
                               uint32_t loop_count)
{
    if (!handle || !handle->dimming || !colors || color_count == 0U || !light_bulb_effect_begin(handle)) {
        return;
    }

    uint32_t step_duration = (wipe_duration_ms > 0U) ? wipe_duration_ms : 300U;
    uint32_t loops_done = 0U;

    while (handle->effect_running && (loop_count == 0U || loops_done < loop_count)) {
        for (size_t i = 0; i < color_count && handle->effect_running; i++) {
            (void)light_bulb_transition_color_duration(handle, colors[i], step_duration);
            light_bulb_wait_fade_done_with_abort(handle);
        }
        if (loop_count != 0U) {
            loops_done++;
        }
    }

    light_bulb_effect_end(handle);
}

void light_bulb_run_theater_chase(light_bulb_handle_t handle,
                                  light_bulb_color_t color1,
                                  light_bulb_color_t color2,
                                  uint32_t speed_ms,
                                  uint32_t loop_count)
{
    if (!handle || !handle->dimming || !light_bulb_effect_begin(handle)) {
        return;
    }

    uint32_t step_ms = (speed_ms > 0U) ? speed_ms : 120U;
    uint32_t loops_done = 0U;
    while (handle->effect_running && (loop_count == 0U || loops_done < loop_count)) {
        (void)light_bulb_transition_color_duration(handle, color1, step_ms);
        light_bulb_wait_fade_done_with_abort(handle);
        if (!handle->effect_running) {
            break;
        }
        (void)light_bulb_transition_color_duration(handle, color2, step_ms);
        light_bulb_wait_fade_done_with_abort(handle);
        if (loop_count != 0U) {
            loops_done++;
        }
    }

    light_bulb_effect_end(handle);
}
void light_bulb_run_scanner_effect(light_bulb_handle_t handle,
                                   light_bulb_color_t color,
                                   uint32_t speed_ms,
                                   uint32_t loop_count)
{
    if (!handle || !handle->dimming || !light_bulb_effect_begin(handle)) {
        return;
    }

    uint32_t step_ms = (speed_ms > 0U) ? speed_ms : 80U;
    const uint8_t levels[] = {10U, 30U, 60U, 100U, 60U, 30U};
    uint32_t loops_done = 0U;

    while (handle->effect_running && (loop_count == 0U || loops_done < loop_count)) {
        for (size_t i = 0; i < (sizeof(levels) / sizeof(levels[0])) && handle->effect_running; i++) {
            light_bulb_color_t frame = light_bulb_color_brightness(color, levels[i]);
            (void)light_bulb_transition_color_duration(handle, frame, step_ms);
            light_bulb_wait_fade_done_with_abort(handle);
        }
        if (loop_count != 0U) {
            loops_done++;
        }
    }

    light_bulb_effect_end(handle);
}

void light_bulb_run_larson_scanner(light_bulb_handle_t handle,
                                   light_bulb_color_t color,
                                   uint32_t speed_ms,
                                   uint32_t loop_count)
{
    if (!handle || !handle->dimming || !light_bulb_effect_begin(handle)) {
        return;
    }

    uint32_t step_ms = (speed_ms > 0U) ? speed_ms : 60U;
    const uint8_t levels[] = {5U, 20U, 55U, 100U, 55U, 20U};
    uint32_t loops_done = 0U;

    while (handle->effect_running && (loop_count == 0U || loops_done < loop_count)) {
        for (size_t i = 0; i < (sizeof(levels) / sizeof(levels[0])) && handle->effect_running; i++) {
            light_bulb_color_t frame = light_bulb_color_brightness(color, levels[i]);
            (void)light_bulb_transition_color_duration(handle, frame, step_ms);
            light_bulb_wait_fade_done_with_abort(handle);
        }
        if (loop_count != 0U) {
            loops_done++;
        }
    }

    light_bulb_effect_end(handle);
}

void light_bulb_run_lightning_effect(light_bulb_handle_t handle, uint32_t intensity, uint32_t duration_ms)
{
    if (!handle || !handle->dimming || !light_bulb_effect_begin(handle)) {
        return;
    }

    bool has_rgb = light_bulb_has_rgb_channels(handle);
    bool has_cct = light_bulb_has_cct_channels(handle);
    if (!has_rgb && !has_cct) {
        light_bulb_effect_end(handle);
        return;
    }

    uint32_t level = (intensity > 100U) ? 100U : intensity;
    if (level == 0U) {
        level = 1U;
    }

    uint32_t total_ms = (duration_ms > 0U) ? duration_ms : 7000U;
    uint32_t start_ms = platform_get_time_ms();
    const light_bulb_color_t off = LIGHT_BULB_COLOR_OFF;

    while (handle->effect_running) {
        uint32_t elapsed = platform_get_time_ms() - start_ms;
        if (elapsed >= total_ms) {
            break;
        }

        uint32_t rnd = light_bulb_next_rand(handle);
        uint32_t idle_ms = 120U + ((100U - level) * 8U) + (rnd % 220U);
        light_bulb_delay_with_abort(handle, idle_ms);
        if (!handle->effect_running) {
            break;
        }

        uint32_t burst = 1U + ((rnd >> 8) % ((level >= 70U) ? 4U : 3U));
        for (uint32_t i = 0; i < burst && handle->effect_running; i++) {
            rnd = light_bulb_next_rand(handle);
            uint32_t flash_ms = 18U + (rnd % 65U);
            uint32_t decay_ms = 24U + ((rnd >> 8) % 70U);

            light_bulb_color_t flash_color = {
                (uint8_t)(200U + ((rnd >> 16) % 56U)),
                (uint8_t)(210U + ((rnd >> 24) % 46U)),
                255U
            };

            uint8_t bright_pct = (uint8_t)(35U + ((rnd >> 4) % 66U));
            bright_pct = (uint8_t)((bright_pct * level + 99U) / 100U);
            if (bright_pct == 0U) {
                bright_pct = 1U;
            }

            if (has_rgb) {
                light_bulb_color_t frame = light_bulb_color_brightness(flash_color, bright_pct);
                (void)light_bulb_transition_color_duration(handle, frame, flash_ms);
            } else {
                uint32_t brightness = (handle->max_value * bright_pct) / 100U;
                uint32_t cool = brightness;
                uint32_t warm = (brightness * 20U) / 100U;
                (void)light_bulb_transition_cct_duration(handle, warm, cool, flash_ms);
            }
            light_bulb_wait_fade_done_with_abort(handle);
            if (!handle->effect_running) {
                break;
            }

            if (has_rgb) {
                (void)light_bulb_transition_color_duration(handle, off, decay_ms);
            } else {
                (void)light_bulb_transition_cct_duration(handle, 0U, 0U, decay_ms);
            }
            light_bulb_wait_fade_done_with_abort(handle);
            if (!handle->effect_running) {
                break;
            }

            uint32_t gap_ms = 18U + ((rnd >> 12) % 90U);
            light_bulb_delay_with_abort(handle, gap_ms);
        }
    }

    light_bulb_effect_end(handle);
}

void light_bulb_run_ocean_wave_effect(light_bulb_handle_t handle, uint32_t period_ms, uint32_t loop_count)
{
    if (!handle || !handle->dimming || !light_bulb_effect_begin(handle)) {
        return;
    }

    bool has_rgb = light_bulb_has_rgb_channels(handle);
    bool has_cct = light_bulb_has_cct_channels(handle);
    if (!has_rgb && !has_cct) {
        light_bulb_effect_end(handle);
        return;
    }

    const uint16_t steps = 20U;
    uint32_t step_ms = (period_ms > 0U) ? (period_ms / steps) : 140U;
    if (step_ms == 0U) {
        step_ms = 1U;
    }

    const light_bulb_color_t deep_blue = {0U, 44U, 160U};
    const light_bulb_color_t sea_cyan = {34U, 170U, 226U};
    uint32_t loops_done = 0U;

    while (handle->effect_running && (loop_count == 0U || loops_done < loop_count)) {
        for (uint16_t i = 0U; i < steps && handle->effect_running; i++) {
            uint32_t saw = (uint32_t)i * 200U / (steps - 1U);
            uint8_t mix = (saw <= 100U) ? (uint8_t)saw : (uint8_t)(200U - saw);
            uint8_t brightness = (uint8_t)(35U + (mix / 2U));

            if (has_rgb) {
                light_bulb_color_t blend = light_bulb_color_mix(deep_blue, sea_cyan, mix);
                light_bulb_color_t frame = light_bulb_color_brightness(blend, brightness);
                (void)light_bulb_transition_color_duration(handle, frame, step_ms);
            } else {
                uint32_t total = (handle->max_value * brightness) / 100U;
                uint32_t cool_pct = 35U + (mix / 2U);
                uint32_t cool = (total * cool_pct) / 100U;
                uint32_t warm = (total > cool) ? (total - cool) : 0U;
                (void)light_bulb_transition_cct_duration(handle, warm, cool, step_ms);
            }
            light_bulb_wait_fade_done_with_abort(handle);
        }

        if (loop_count != 0U) {
            loops_done++;
        }
    }

    light_bulb_effect_end(handle);
}

void light_bulb_run_aurora_effect(light_bulb_handle_t handle, uint32_t period_ms, uint32_t loop_count)
{
    if (!handle || !handle->dimming || !light_bulb_effect_begin(handle)) {
        return;
    }

    bool has_rgb = light_bulb_has_rgb_channels(handle);
    bool has_cct = light_bulb_has_cct_channels(handle);
    if (!has_rgb && !has_cct) {
        light_bulb_effect_end(handle);
        return;
    }

    const uint16_t steps = 28U;
    uint32_t step_ms = (period_ms > 0U) ? (period_ms / steps) : 120U;
    if (step_ms == 0U) {
        step_ms = 1U;
    }

    uint32_t loops_done = 0U;
    while (handle->effect_running && (loop_count == 0U || loops_done < loop_count)) {
        for (uint16_t i = 0U; i < steps && handle->effect_running; i++) {
            uint32_t rnd = light_bulb_next_rand(handle);
            uint16_t hue = (uint16_t)(90U + ((210U * i) / steps) + (rnd % 15U));
            if (hue >= 360U) {
                hue = (uint16_t)(hue - 360U);
            }

            if (has_rgb) {
                uint8_t sat = (uint8_t)(165U + ((rnd >> 8) % 75U));
                uint8_t val = (uint8_t)(95U + ((rnd >> 16) % 140U));
                light_bulb_color_t frame = light_bulb_hsv_to_rgb(hue, sat, val);
                (void)light_bulb_transition_color_duration(handle, frame, step_ms);
            } else {
                uint8_t brightness = (uint8_t)(35U + ((rnd >> 8) % 50U));
                uint8_t cool_pct = (uint8_t)(20U + ((hue * 55U) / 359U));
                uint32_t total = (handle->max_value * brightness) / 100U;
                uint32_t cool = (total * cool_pct) / 100U;
                uint32_t warm = (total > cool) ? (total - cool) : 0U;
                (void)light_bulb_transition_cct_duration(handle, warm, cool, step_ms);
            }
            light_bulb_wait_fade_done_with_abort(handle);
        }

        if (loop_count != 0U) {
            loops_done++;
        }
    }

    light_bulb_effect_end(handle);
}

void light_bulb_run_forest_breeze_effect(light_bulb_handle_t handle, uint32_t period_ms, uint32_t loop_count)
{
    if (!handle || !handle->dimming || !light_bulb_effect_begin(handle)) {
        return;
    }

    const uint16_t steps = 18U;
    uint32_t step_ms = (period_ms > 0U) ? (period_ms / steps) : 180U;
    if (step_ms == 0U) {
        step_ms = 1U;
    }

    bool has_rgb = light_bulb_has_rgb_channels(handle);
    bool has_cct = light_bulb_has_cct_channels(handle);
    if (!has_rgb && !has_cct) {
        light_bulb_effect_end(handle);
        return;
    }
    const light_bulb_color_t leaf_shadow = {48U, 82U, 34U};
    const light_bulb_color_t leaf_sun = {140U, 178U, 88U};
    uint32_t loops_done = 0U;

    while (handle->effect_running && (loop_count == 0U || loops_done < loop_count)) {
        for (uint16_t i = 0U; i < steps && handle->effect_running; i++) {
            uint32_t rnd = light_bulb_next_rand(handle);
            uint32_t saw = (uint32_t)i * 200U / (steps - 1U);
            uint8_t phase = (saw <= 100U) ? (uint8_t)saw : (uint8_t)(200U - saw);

            if (has_cct) {
                uint8_t brightness_pct = (uint8_t)(30U + (phase / 2U));
                uint8_t cool_pct = (uint8_t)(12U + (phase / 3U));
                uint8_t jitter = (uint8_t)(rnd % 6U);
                if (brightness_pct + jitter < 100U) {
                    brightness_pct = (uint8_t)(brightness_pct + jitter);
                }
                if (cool_pct + (jitter / 2U) < 60U) {
                    cool_pct = (uint8_t)(cool_pct + (jitter / 2U));
                }

                uint32_t brightness = (handle->max_value * brightness_pct) / 100U;
                uint32_t cool = (brightness * cool_pct) / 100U;
                uint32_t warm = (brightness > cool) ? (brightness - cool) : 0U;
                (void)light_bulb_transition_cct_duration(handle, warm, cool, step_ms);
            } else if (has_rgb) {
                light_bulb_color_t blend = light_bulb_color_mix(leaf_shadow, leaf_sun, phase);
                uint8_t brightness = (uint8_t)(28U + (phase / 2U) + (rnd % 8U));
                if (brightness > 100U) {
                    brightness = 100U;
                }
                light_bulb_color_t frame = light_bulb_color_brightness(blend, brightness);
                (void)light_bulb_transition_color_duration(handle, frame, step_ms);
            }

            light_bulb_wait_fade_done_with_abort(handle);
        }

        if (loop_count != 0U) {
            loops_done++;
        }
    }

    light_bulb_effect_end(handle);
}

bool light_bulb_apply_relax_scene(light_bulb_handle_t handle)
{
    const light_bulb_scene_t scene = LIGHT_BULB_SCENE_RELAX;
    if (!light_bulb_apply_scene(handle, &scene)) {
        return false;
    }
    return light_bulb_transition_cct_percent(handle, 20U, (handle->max_value * 70U) / 100U);
}

bool light_bulb_apply_reading_scene(light_bulb_handle_t handle)
{
    const light_bulb_scene_t scene = LIGHT_BULB_SCENE_READING;
    if (!light_bulb_apply_scene(handle, &scene)) {
        return false;
    }
    return light_bulb_transition_cct_percent(handle, 70U, handle->max_value);
}

bool light_bulb_apply_night_scene(light_bulb_handle_t handle)
{
    const light_bulb_scene_t scene = LIGHT_BULB_SCENE_NIGHT;
    if (!light_bulb_apply_scene(handle, &scene)) {
        return false;
    }
    return light_bulb_transition_cct_percent(handle, 8U, (handle->max_value * 18U) / 100U);
}

bool light_bulb_apply_party_scene(light_bulb_handle_t handle)
{
    const light_bulb_scene_t scene = LIGHT_BULB_SCENE_PARTY;
    if (!light_bulb_apply_scene(handle, &scene)) {
        return false;
    }
    return light_bulb_transition_color(handle, (light_bulb_color_t)LIGHT_BULB_COLOR_MAGENTA);
}

bool light_bulb_apply_movie_scene(light_bulb_handle_t handle)
{
    const light_bulb_scene_t scene = LIGHT_BULB_SCENE_MOVIE;
    if (!light_bulb_apply_scene(handle, &scene)) {
        return false;
    }
    return light_bulb_transition_cct_percent(handle, 15U, (handle->max_value * 35U) / 100U);
}

bool light_bulb_sunrise_simulation(light_bulb_handle_t handle)
{
    const light_bulb_scene_t scene = LIGHT_BULB_SCENE_SUNRISE;
    if (!light_bulb_apply_scene(handle, &scene)) {
        return false;
    }
    return light_bulb_transition_cct_percent(handle, 35U, handle->max_value);
}

bool light_bulb_sunset_simulation(light_bulb_handle_t handle)
{
    const light_bulb_scene_t scene = LIGHT_BULB_SCENE_SUNSET;
    if (!light_bulb_apply_scene(handle, &scene)) {
        return false;
    }
    return light_bulb_transition_cct_percent(handle, 10U, (handle->max_value * 30U) / 100U);
}

light_bulb_color_t light_bulb_color_mix(light_bulb_color_t color1, light_bulb_color_t color2, uint8_t ratio)
{
    if (ratio > 100U) {
        ratio = 100U;
    }

    uint32_t inv = 100U - ratio;
    light_bulb_color_t out = {
        (uint8_t)((color1.red * inv + color2.red * ratio + 50U) / 100U),
        (uint8_t)((color1.green * inv + color2.green * ratio + 50U) / 100U),
        (uint8_t)((color1.blue * inv + color2.blue * ratio + 50U) / 100U)
    };
    return out;
}

light_bulb_color_t light_bulb_color_brightness(light_bulb_color_t color, uint8_t brightness_percent)
{
    if (brightness_percent > 100U) {
        brightness_percent = 100U;
    }

    light_bulb_color_t out = {
        (uint8_t)((color.red * brightness_percent + 50U) / 100U),
        (uint8_t)((color.green * brightness_percent + 50U) / 100U),
        (uint8_t)((color.blue * brightness_percent + 50U) / 100U)
    };
    return out;
}

light_bulb_color_t light_bulb_color_temperature_to_rgb(uint32_t kelvin, uint8_t brightness)
{
    if (kelvin < 1000U) {
        kelvin = 1000U;
    } else if (kelvin > 40000U) {
        kelvin = 40000U;
    }

    float temp = (float)kelvin / 100.0f;
    float red;
    float green;
    float blue;

    if (temp <= 66.0f) {
        red = 255.0f;
        green = 99.4708025861f * logf(temp) - 161.1195681661f;
        if (temp <= 19.0f) {
            blue = 0.0f;
        } else {
            blue = 138.5177312231f * logf(temp - 10.0f) - 305.0447927307f;
        }
    } else {
        red = 329.698727446f * powf(temp - 60.0f, -0.1332047592f);
        green = 288.1221695283f * powf(temp - 60.0f, -0.0755148492f);
        blue = 255.0f;
    }

    if (red < 0.0f) red = 0.0f;
    if (red > 255.0f) red = 255.0f;
    if (green < 0.0f) green = 0.0f;
    if (green > 255.0f) green = 255.0f;
    if (blue < 0.0f) blue = 0.0f;
    if (blue > 255.0f) blue = 255.0f;

    light_bulb_color_t out = {
        (uint8_t)(((uint32_t)red * brightness + 127U) / 255U),
        (uint8_t)(((uint32_t)green * brightness + 127U) / 255U),
        (uint8_t)(((uint32_t)blue * brightness + 127U) / 255U)
    };
    return out;
}

light_bulb_cct_t light_bulb_color_temperature_to_cct(uint32_t kelvin, uint32_t brightness)
{
    if (kelvin < 2000U) {
        kelvin = 2000U;
    } else if (kelvin > 6500U) {
        kelvin = 6500U;
    }

    float ratio = (float)(kelvin - 2000U) / 4500.0f;
    if (ratio < 0.0f) ratio = 0.0f;
    if (ratio > 1.0f) ratio = 1.0f;

    uint32_t cool = (uint32_t)(brightness * ratio + 0.5f);
    uint32_t warm = (brightness > cool) ? (brightness - cool) : 0U;
    light_bulb_cct_t out = {warm, cool};
    return out;
}

bool light_bulb_get_current_rgb(light_bulb_handle_t handle, uint8_t *red, uint8_t *green, uint8_t *blue)
{
    if (!handle || !handle->dimming || !red || !green || !blue) {
        return false;
    }

    if (handle->map.red == LIGHT_BULB_CHANNEL_INVALID ||
        handle->map.green == LIGHT_BULB_CHANNEL_INVALID ||
        handle->map.blue == LIGHT_BULB_CHANNEL_INVALID) {
        return false;
    }

    uint32_t r = dimming_get_current_value(handle->dimming, handle->map.red);
    uint32_t g = dimming_get_current_value(handle->dimming, handle->map.green);
    uint32_t b = dimming_get_current_value(handle->dimming, handle->map.blue);

    *red = light_bulb_scale_max_to_u8(r, handle->max_value);
    *green = light_bulb_scale_max_to_u8(g, handle->max_value);
    *blue = light_bulb_scale_max_to_u8(b, handle->max_value);
    return true;
}

bool light_bulb_get_current_cct(light_bulb_handle_t handle, uint32_t *warm, uint32_t *cool)
{
    if (!handle || !handle->dimming || !warm || !cool) {
        return false;
    }

    if (handle->map.warm == LIGHT_BULB_CHANNEL_INVALID ||
        handle->map.cool == LIGHT_BULB_CHANNEL_INVALID) {
        return false;
    }

    *warm = dimming_get_current_value(handle->dimming, handle->map.warm);
    *cool = dimming_get_current_value(handle->dimming, handle->map.cool);
    return true;
}

light_bulb_color_t light_bulb_get_current_color(light_bulb_handle_t handle)
{
    light_bulb_color_t out = {0U, 0U, 0U};
    (void)light_bulb_get_current_rgb(handle, &out.red, &out.green, &out.blue);
    return out;
}

light_bulb_cct_t light_bulb_get_current_cct_pair(light_bulb_handle_t handle)
{
    light_bulb_cct_t out = {0U, 0U};
    (void)light_bulb_get_current_cct(handle, &out.warm, &out.cool);
    return out;
}

bool light_bulb_start_effect(light_bulb_handle_t handle, const light_bulb_effect_config_t *config)
{
    if (!handle || !config) {
        return false;
    }

    uint32_t speed_ms = (config->speed_ms > 0U) ? config->speed_ms : 1000U;
    uint32_t loop_count = (config->color_count > 0U) ? config->color_count : 1U;
    light_bulb_color_t color = (config->colors && config->color_count > 0U)
                                   ? config->colors[0]
                                   : (light_bulb_color_t)LIGHT_BULB_COLOR_WHITE;

    switch (config->type) {
        case LIGHT_BULB_EFFECT_BREATH:
            light_bulb_run_breath_effect(handle, color, speed_ms, loop_count);
            return true;
        case LIGHT_BULB_EFFECT_PULSE:
            light_bulb_run_pulse_effect(handle, color, speed_ms, loop_count);
            return true;
        case LIGHT_BULB_EFFECT_RAINBOW:
            light_bulb_run_rainbow_effect(handle, speed_ms, loop_count);
            return true;
        case LIGHT_BULB_EFFECT_FIRE:
            light_bulb_run_fire_effect(handle, config->intensity, speed_ms);
            return true;
        case LIGHT_BULB_EFFECT_STROBE:
            light_bulb_run_strobe_effect(handle, color, speed_ms / 2U, speed_ms / 2U, loop_count);
            return true;
        case LIGHT_BULB_EFFECT_COLOR_WIPE:
            if (!config->colors || config->color_count == 0U) {
                return false;
            }
            light_bulb_run_color_wipe(handle, config->colors, config->color_count, speed_ms, loop_count);
            return true;
        case LIGHT_BULB_EFFECT_THEATER_CHASE: {
            light_bulb_color_t color2 = (config->colors && config->color_count > 1U)
                                            ? config->colors[1]
                                            : (light_bulb_color_t)LIGHT_BULB_COLOR_OFF;
            light_bulb_run_theater_chase(handle, color, color2, speed_ms, loop_count);
            return true;
        }
        case LIGHT_BULB_EFFECT_SCANNER:
            light_bulb_run_scanner_effect(handle, color, speed_ms, loop_count);
            return true;
        case LIGHT_BULB_EFFECT_LARSON_SCANNER:
            light_bulb_run_larson_scanner(handle, color, speed_ms, loop_count);
            return true;
        case LIGHT_BULB_EFFECT_LIGHTNING:
            light_bulb_run_lightning_effect(handle, config->intensity, speed_ms);
            return true;
        case LIGHT_BULB_EFFECT_OCEAN_WAVE:
            light_bulb_run_ocean_wave_effect(handle, speed_ms, loop_count);
            return true;
        case LIGHT_BULB_EFFECT_AURORA:
            light_bulb_run_aurora_effect(handle, speed_ms, loop_count);
            return true;
        case LIGHT_BULB_EFFECT_FOREST_BREEZE:
            light_bulb_run_forest_breeze_effect(handle, speed_ms, loop_count);
            return true;
        default:
            return false;
    }
}

void light_bulb_stop_effect(light_bulb_handle_t handle)
{
    if (!handle) {
        return;
    }

    handle->effect_running = false;
    light_bulb_stop(handle);
}

bool light_bulb_is_effect_running(light_bulb_handle_t handle)
{
    return (handle != NULL) && handle->effect_running;
}

