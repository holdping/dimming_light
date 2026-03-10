#include "dimming_lib.h"
#include "platform_timer.h"

#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    gamma_type_t gamma_type;
    bool gamma_enabled;
    bool custom_table_valid;
    uint8_t custom_gamma_table[256];
} gamma_channel_config_t;

typedef struct {
    int64_t start_value;
    int64_t delta_value;
    uint32_t total_steps;
    uint32_t steps_done;
} fade_channel_state_t;

typedef struct {
    bool fade_enabled;
    uint32_t default_duration_ms;
} fade_channel_config_t;

typedef struct {
    dimming_channel_t *channels;
    gamma_channel_config_t *gamma_configs;
    fade_channel_config_t *fade_configs;
    fade_channel_state_t *fade_states;
    uint8_t channel_count;
    uint32_t timer_period_ms;
    dimming_driver_cb_t driver_cb;
    platform_timer_handle_t timer_handle;
} dimming_context_t;

static void dimming_timer_callback(void *arg);
static void update_channel(dimming_context_t *ctx, uint8_t channel);
static uint32_t calculate_step_count(uint32_t start, uint32_t end, uint32_t duration_ms, uint32_t timer_period_ms);
static int32_t calculate_step_size(uint32_t start, uint32_t end, uint32_t step_count);
static uint32_t clamp_to_max(uint32_t value, uint32_t max_value);
static void clear_fade_state(fade_channel_state_t *fade);
static int64_t div_round_nearest_i64(int64_t numerator, int64_t denominator);
static uint32_t calculate_interpolated_value(const dimming_channel_t *ch, const fade_channel_state_t *fade);
static float gamma_type_to_value(gamma_type_t gamma_type);
static uint32_t apply_gamma_internal(const dimming_context_t *ctx, uint8_t channel, uint32_t value);
static uint32_t remove_gamma_internal(const dimming_context_t *ctx, uint8_t channel, uint32_t value);
static void emit_channel_value(const dimming_context_t *ctx, uint8_t channel, uint32_t linear_value);

dimming_handle_t dimming_init(const dimming_config_t *config, dimming_driver_cb_t driver_cb)
{
    if (!config || !driver_cb || config->channel_count == 0) {
        return NULL;
    }

    dimming_context_t *ctx = (dimming_context_t *)calloc(1, sizeof(dimming_context_t));
    if (!ctx) {
        return NULL;
    }

    ctx->channels = (dimming_channel_t *)calloc(config->channel_count, sizeof(dimming_channel_t));
    if (!ctx->channels) {
        free(ctx);
        return NULL;
    }

    ctx->gamma_configs = (gamma_channel_config_t *)calloc(config->channel_count, sizeof(gamma_channel_config_t));
    if (!ctx->gamma_configs) {
        free(ctx->channels);
        free(ctx);
        return NULL;
    }

    ctx->fade_configs = (fade_channel_config_t *)calloc(config->channel_count, sizeof(fade_channel_config_t));
    if (!ctx->fade_configs) {
        free(ctx->gamma_configs);
        free(ctx->channels);
        free(ctx);
        return NULL;
    }

    ctx->fade_states = (fade_channel_state_t *)calloc(config->channel_count, sizeof(fade_channel_state_t));
    if (!ctx->fade_states) {
        free(ctx->fade_configs);
        free(ctx->gamma_configs);
        free(ctx->channels);
        free(ctx);
        return NULL;
    }

    ctx->channel_count = config->channel_count;
    ctx->timer_period_ms = (config->timer_period_ms > 0) ? config->timer_period_ms : 12;
    ctx->driver_cb = driver_cb;

    for (uint8_t i = 0; i < config->channel_count; i++) {
        ctx->channels[i].current_value = 0;
        ctx->channels[i].target_value = 0;
        ctx->channels[i].step_size = 0;
        ctx->channels[i].step_count = 0;
        ctx->channels[i].max_value = config->max_value;
        ctx->channels[i].is_active = true;
        clear_fade_state(&ctx->fade_states[i]);

        ctx->gamma_configs[i].gamma_type = GAMMA_22;
        ctx->gamma_configs[i].gamma_enabled = true;
        ctx->gamma_configs[i].custom_table_valid = false;

        ctx->fade_configs[i].fade_enabled = true;
        ctx->fade_configs[i].default_duration_ms = 0U;
    }

    if (config->gamma_configs) {
        for (uint8_t i = 0; i < config->channel_count; i++) {
            gamma_type_t type = config->gamma_configs[i].type;
            if (type > GAMMA_CUSTOM) {
                type = GAMMA_22;
            }

            ctx->gamma_configs[i].gamma_enabled = config->gamma_configs[i].enabled;
            ctx->gamma_configs[i].gamma_type = ctx->gamma_configs[i].gamma_enabled ? type : GAMMA_NONE;
        }
    }

    if (config->fade_configs) {
        for (uint8_t i = 0; i < config->channel_count; i++) {
            ctx->fade_configs[i].fade_enabled = config->fade_configs[i].enabled;
            ctx->fade_configs[i].default_duration_ms = config->fade_configs[i].default_duration_ms;
        }
    }

    platform_timer_config_t timer_config = {
        .period_ms = ctx->timer_period_ms,
        .is_periodic = true,
        .callback = dimming_timer_callback,
        .callback_arg = ctx,
        .name = "dimming_timer"
    };

    if (!platform_timer_create(&timer_config, &ctx->timer_handle)) {
        free(ctx->fade_states);
        free(ctx->fade_configs);
        free(ctx->gamma_configs);
        free(ctx->channels);
        free(ctx);
        return NULL;
    }

    return (dimming_handle_t)ctx;
}

void dimming_deinit(dimming_handle_t handle)
{
    if (!handle) {
        return;
    }

    dimming_context_t *ctx = (dimming_context_t *)handle;

    if (ctx->timer_handle) {
        platform_timer_stop(ctx->timer_handle);
        platform_timer_delete(ctx->timer_handle);
    }

    free(ctx->fade_states);
    free(ctx->fade_configs);
    free(ctx->gamma_configs);
    free(ctx->channels);
    free(ctx);
}

bool dimming_set_immediate(dimming_handle_t handle, uint8_t channel, uint32_t value)
{
    if (!handle) {
        return false;
    }

    dimming_context_t *ctx = (dimming_context_t *)handle;
    if (channel >= ctx->channel_count) {
        return false;
    }

    dimming_channel_t *ch = &ctx->channels[channel];
    value = clamp_to_max(value, ch->max_value);

    ch->current_value = value;
    ch->target_value = value;
    ch->step_size = 0;
    ch->step_count = 0;
    clear_fade_state(&ctx->fade_states[channel]);

    emit_channel_value(ctx, channel, value);
    return true;
}

bool dimming_set_with_fade(dimming_handle_t handle, uint8_t channel, uint32_t value, uint32_t duration_ms)
{
    if (!handle) {
        return false;
    }

    dimming_context_t *ctx = (dimming_context_t *)handle;
    if (channel >= ctx->channel_count) {
        return false;
    }

    dimming_channel_t *ch = &ctx->channels[channel];
    fade_channel_config_t *fade_cfg = &ctx->fade_configs[channel];
    uint32_t effective_duration_ms = duration_ms;
    value = clamp_to_max(value, ch->max_value);

    if (!fade_cfg->fade_enabled) {
        return dimming_set_immediate(handle, channel, value);
    }

    if (effective_duration_ms == 0U) {
        effective_duration_ms = fade_cfg->default_duration_ms;
    }

    if (effective_duration_ms == 0U) {
        return dimming_set_immediate(handle, channel, value);
    }

    if (ch->target_value == value) {
        return true;
    }

    uint32_t step_count = calculate_step_count(ch->current_value, value, effective_duration_ms, ctx->timer_period_ms);
    int32_t step_size = calculate_step_size(ch->current_value, value, step_count);

    ch->target_value = value;
    ch->step_size = step_size;
    ch->step_count = step_count;
    fade_channel_state_t *fade = &ctx->fade_states[channel];
    fade->start_value = (int64_t)ch->current_value;
    fade->delta_value = (int64_t)ch->target_value - fade->start_value;
    fade->total_steps = step_count;
    fade->steps_done = 0U;

    if (ch->step_count == 0U) {
        ch->current_value = ch->target_value;
        clear_fade_state(fade);
        emit_channel_value(ctx, channel, ch->current_value);
        return true;
    }

    if (!platform_timer_is_running(ctx->timer_handle)) {
        if (!platform_timer_start(ctx->timer_handle)) {
            return false;
        }
    }

    return true;
}

bool dimming_set_multiple_with_fade(dimming_handle_t handle, const uint8_t *channels,
                                    const uint32_t *values, uint8_t count, uint32_t duration_ms)
{
    if (!handle || !channels || !values || count == 0) {
        return false;
    }

    bool any_channel_updated = false;
    for (uint8_t i = 0; i < count; i++) {
        if (dimming_set_with_fade(handle, channels[i], values[i], duration_ms)) {
            any_channel_updated = true;
        }
    }

    return any_channel_updated;
}

void dimming_stop_all_fades(dimming_handle_t handle)
{
    if (!handle) {
        return;
    }

    dimming_context_t *ctx = (dimming_context_t *)handle;
    for (uint8_t i = 0; i < ctx->channel_count; i++) {
        ctx->channels[i].step_count = 0;
        ctx->channels[i].step_size = 0;
        ctx->channels[i].target_value = ctx->channels[i].current_value;
        clear_fade_state(&ctx->fade_states[i]);
    }

    if (ctx->timer_handle && platform_timer_is_running(ctx->timer_handle)) {
        platform_timer_stop(ctx->timer_handle);
    }
}

uint32_t dimming_get_current_value(dimming_handle_t handle, uint8_t channel)
{
    if (!handle) {
        return 0;
    }

    dimming_context_t *ctx = (dimming_context_t *)handle;
    if (channel >= ctx->channel_count) {
        return 0;
    }

    return ctx->channels[channel].current_value;
}

uint32_t dimming_get_target_value(dimming_handle_t handle, uint8_t channel)
{
    if (!handle) {
        return 0;
    }

    dimming_context_t *ctx = (dimming_context_t *)handle;
    if (channel >= ctx->channel_count) {
        return 0;
    }

    return ctx->channels[channel].target_value;
}

bool dimming_is_fading(dimming_handle_t handle)
{
    if (!handle) {
        return false;
    }

    dimming_context_t *ctx = (dimming_context_t *)handle;
    for (uint8_t i = 0; i < ctx->channel_count; i++) {
        if (ctx->channels[i].step_count > 0) {
            return true;
        }
    }

    return false;
}

bool dimming_set_max_value(dimming_handle_t handle, uint8_t channel, uint32_t max_value)
{
    if (!handle) {
        return false;
    }

    dimming_context_t *ctx = (dimming_context_t *)handle;
    if (channel >= ctx->channel_count) {
        return false;
    }

    dimming_channel_t *ch = &ctx->channels[channel];
    ch->max_value = max_value;

    uint32_t new_current = clamp_to_max(ch->current_value, ch->max_value);
    if (new_current != ch->current_value) {
        ch->current_value = new_current;
        emit_channel_value(ctx, channel, ch->current_value);
    }

    ch->target_value = clamp_to_max(ch->target_value, ch->max_value);

    if (ch->step_count > 0U) {
        fade_channel_state_t *fade = &ctx->fade_states[channel];
        fade->start_value = (int64_t)ch->current_value;
        fade->delta_value = (int64_t)ch->target_value - fade->start_value;
        fade->total_steps = ch->step_count;
        fade->steps_done = 0U;
        ch->step_size = calculate_step_size(ch->current_value, ch->target_value, ch->step_count);
    }

    return true;
}

bool dimming_set_rgb(dimming_handle_t handle, uint8_t red, uint8_t green, uint8_t blue, uint32_t duration_ms)
{
    if (!handle || ((dimming_context_t *)handle)->channel_count < 3) {
        return false;
    }

    const uint8_t channels[3] = {0, 1, 2};
    const uint32_t values[3] = {red, green, blue};
    return dimming_set_multiple_with_fade(handle, channels, values, 3, duration_ms);
}

bool dimming_set_cct(dimming_handle_t handle, uint32_t warm, uint32_t cool, uint32_t duration_ms)
{
    if (!handle || ((dimming_context_t *)handle)->channel_count < 2) {
        return false;
    }

    const uint8_t channels[2] = {0, 1};
    const uint32_t values[2] = {warm, cool};
    return dimming_set_multiple_with_fade(handle, channels, values, 2, duration_ms);
}

bool dimming_set_gamma_type(dimming_handle_t handle, uint8_t channel, gamma_type_t gamma_type)
{
    if (!handle || gamma_type > GAMMA_CUSTOM) {
        return false;
    }

    dimming_context_t *ctx = (dimming_context_t *)handle;
    if (channel >= ctx->channel_count) {
        return false;
    }

    gamma_channel_config_t *gamma_cfg = &ctx->gamma_configs[channel];
    gamma_cfg->gamma_type = gamma_type;
    gamma_cfg->gamma_enabled = (gamma_type != GAMMA_NONE);

    emit_channel_value(ctx, channel, ctx->channels[channel].current_value);
    return true;
}

bool dimming_set_custom_gamma_table(dimming_handle_t handle, uint8_t channel, const uint8_t *gamma_table)
{
    if (!handle || !gamma_table) {
        return false;
    }

    dimming_context_t *ctx = (dimming_context_t *)handle;
    if (channel >= ctx->channel_count) {
        return false;
    }

    gamma_channel_config_t *gamma_cfg = &ctx->gamma_configs[channel];
    memcpy(gamma_cfg->custom_gamma_table, gamma_table, sizeof(gamma_cfg->custom_gamma_table));
    gamma_cfg->custom_table_valid = true;
    gamma_cfg->gamma_type = GAMMA_CUSTOM;
    gamma_cfg->gamma_enabled = true;

    emit_channel_value(ctx, channel, ctx->channels[channel].current_value);
    return true;
}

uint32_t dimming_apply_gamma(dimming_handle_t handle, uint8_t channel, uint32_t value)
{
    if (!handle) {
        return value;
    }

    dimming_context_t *ctx = (dimming_context_t *)handle;
    if (channel >= ctx->channel_count) {
        return value;
    }

    return apply_gamma_internal(ctx, channel, value);
}

uint32_t dimming_remove_gamma(dimming_handle_t handle, uint8_t channel, uint32_t value)
{
    if (!handle) {
        return value;
    }

    dimming_context_t *ctx = (dimming_context_t *)handle;
    if (channel >= ctx->channel_count) {
        return value;
    }

    return remove_gamma_internal(ctx, channel, value);
}

bool dimming_enable_gamma(dimming_handle_t handle, uint8_t channel, bool enable)
{
    if (!handle) {
        return false;
    }

    dimming_context_t *ctx = (dimming_context_t *)handle;
    if (channel >= ctx->channel_count) {
        return false;
    }

    gamma_channel_config_t *gamma_cfg = &ctx->gamma_configs[channel];
    gamma_cfg->gamma_enabled = enable;
    if (enable && gamma_cfg->gamma_type == GAMMA_NONE) {
        gamma_cfg->gamma_type = GAMMA_22;
    }

    emit_channel_value(ctx, channel, ctx->channels[channel].current_value);
    return true;
}

void dimming_get_standard_gamma_table(float gamma, uint8_t *table)
{
    if (!table) {
        return;
    }

    if (gamma <= 0.0f) {
        gamma = 1.0f;
    }

    for (uint32_t i = 0; i < 256; i++) {
        float input = (float)i / 255.0f;
        float output = powf(input, gamma) * 255.0f;
        if (output < 0.0f) {
            output = 0.0f;
        }
        if (output > 255.0f) {
            output = 255.0f;
        }
        table[i] = (uint8_t)(output + 0.5f);
    }
}

static void dimming_timer_callback(void *arg)
{
    dimming_context_t *ctx = (dimming_context_t *)arg;
    if (!ctx) {
        return;
    }

    bool any_fading = false;
    for (uint8_t i = 0; i < ctx->channel_count; i++) {
        if (ctx->channels[i].step_count > 0U) {
            update_channel(ctx, i);
            any_fading = true;
        }
    }

    if (!any_fading && ctx->timer_handle) {
        platform_timer_stop(ctx->timer_handle);
    }
}

static void update_channel(dimming_context_t *ctx, uint8_t channel)
{
    dimming_channel_t *ch = &ctx->channels[channel];
    fade_channel_state_t *fade = &ctx->fade_states[channel];

    if (ch->step_count == 0U || fade->total_steps == 0U) {
        ch->step_count = 0U;
        ch->step_size = 0;
        clear_fade_state(fade);
        return;
    }

    if (fade->steps_done < fade->total_steps) {
        fade->steps_done++;
    }

    bool reached_target = (fade->steps_done >= fade->total_steps);

    if (reached_target) {
        ch->current_value = ch->target_value;
        ch->step_size = 0;
        ch->step_count = 0;
        clear_fade_state(fade);
    } else {
        ch->current_value = calculate_interpolated_value(ch, fade);
        ch->step_count = fade->total_steps - fade->steps_done;
    }

    emit_channel_value(ctx, channel, ch->current_value);
}

static uint32_t calculate_step_count(uint32_t start, uint32_t end, uint32_t duration_ms, uint32_t timer_period_ms)
{
    (void)start;
    (void)end;

    if (duration_ms == 0U || timer_period_ms == 0U) {
        return 0U;
    }

    uint32_t required_steps = (duration_ms + timer_period_ms - 1U) / timer_period_ms;
    return (required_steps == 0U) ? 1U : required_steps;
}

static int32_t calculate_step_size(uint32_t start, uint32_t end, uint32_t step_count)
{
    if (step_count == 0U) {
        return 0;
    }

    int64_t difference = (int64_t)end - (int64_t)start;
    int64_t step = difference / (int64_t)step_count;

    if (step > INT32_MAX) {
        step = INT32_MAX;
    } else if (step < INT32_MIN) {
        step = INT32_MIN;
    }

    return (int32_t)step;
}

static uint32_t clamp_to_max(uint32_t value, uint32_t max_value)
{
    return (value > max_value) ? max_value : value;
}

static void clear_fade_state(fade_channel_state_t *fade)
{
    if (!fade) {
        return;
    }

    fade->start_value = 0;
    fade->delta_value = 0;
    fade->total_steps = 0;
    fade->steps_done = 0;
}

static int64_t div_round_nearest_i64(int64_t numerator, int64_t denominator)
{
    if (denominator <= 0) {
        return 0;
    }

    if (numerator >= 0) {
        return (numerator + (denominator / 2)) / denominator;
    }

    int64_t abs_num = -numerator;
    return -((abs_num + (denominator / 2)) / denominator);
}

static uint32_t calculate_interpolated_value(const dimming_channel_t *ch, const fade_channel_state_t *fade)
{
    if (!ch || !fade || fade->total_steps == 0U) {
        return 0U;
    }

    int64_t scaled = fade->delta_value * (int64_t)fade->steps_done;
    int64_t offset = div_round_nearest_i64(scaled, (int64_t)fade->total_steps);
    int64_t value = fade->start_value + offset;

    if (value < 0) {
        value = 0;
    } else if (value > (int64_t)UINT32_MAX) {
        value = (int64_t)UINT32_MAX;
    }

    return clamp_to_max((uint32_t)value, ch->max_value);
}

static float gamma_type_to_value(gamma_type_t gamma_type)
{
    switch (gamma_type) {
        case GAMMA_18:
            return 1.8f;
        case GAMMA_22:
            return 2.2f;
        case GAMMA_24:
            return 2.4f;
        default:
            return 1.0f;
    }
}

static uint32_t apply_gamma_internal(const dimming_context_t *ctx, uint8_t channel, uint32_t value)
{
    const dimming_channel_t *ch = &ctx->channels[channel];
    const gamma_channel_config_t *gamma_cfg = &ctx->gamma_configs[channel];

    value = clamp_to_max(value, ch->max_value);
    if (!gamma_cfg->gamma_enabled || gamma_cfg->gamma_type == GAMMA_NONE || ch->max_value == 0U) {
        return value;
    }

    if (gamma_cfg->gamma_type == GAMMA_CUSTOM) {
        if (!gamma_cfg->custom_table_valid) {
            return value;
        }

        uint32_t index = (value * 255U + ch->max_value / 2U) / ch->max_value;
        uint32_t mapped = gamma_cfg->custom_gamma_table[index];
        return (mapped * ch->max_value + 127U) / 255U;
    }

    float gamma = gamma_type_to_value(gamma_cfg->gamma_type);
    float normalized = (float)value / (float)ch->max_value;
    float corrected = powf(normalized, gamma) * (float)ch->max_value;

    if (corrected < 0.0f) {
        corrected = 0.0f;
    }
    if (corrected > (float)ch->max_value) {
        corrected = (float)ch->max_value;
    }

    return (uint32_t)(corrected + 0.5f);
}

static uint32_t remove_gamma_internal(const dimming_context_t *ctx, uint8_t channel, uint32_t value)
{
    const dimming_channel_t *ch = &ctx->channels[channel];
    const gamma_channel_config_t *gamma_cfg = &ctx->gamma_configs[channel];

    value = clamp_to_max(value, ch->max_value);
    if (!gamma_cfg->gamma_enabled || gamma_cfg->gamma_type == GAMMA_NONE || ch->max_value == 0U) {
        return value;
    }

    if (gamma_cfg->gamma_type == GAMMA_CUSTOM) {
        if (!gamma_cfg->custom_table_valid) {
            return value;
        }

        uint32_t target = (value * 255U + ch->max_value / 2U) / ch->max_value;
        uint32_t best_index = 0;
        int best_diff = abs((int)gamma_cfg->custom_gamma_table[0] - (int)target);

        for (uint32_t i = 1; i < 256; i++) {
            int diff = abs((int)gamma_cfg->custom_gamma_table[i] - (int)target);
            if (diff < best_diff) {
                best_diff = diff;
                best_index = i;
                if (diff == 0) {
                    break;
                }
            }
        }

        return (best_index * ch->max_value + 127U) / 255U;
    }

    float gamma = gamma_type_to_value(gamma_cfg->gamma_type);
    float normalized = (float)value / (float)ch->max_value;
    float linear = powf(normalized, 1.0f / gamma) * (float)ch->max_value;

    if (linear < 0.0f) {
        linear = 0.0f;
    }
    if (linear > (float)ch->max_value) {
        linear = (float)ch->max_value;
    }

    return (uint32_t)(linear + 0.5f);
}

static void emit_channel_value(const dimming_context_t *ctx, uint8_t channel, uint32_t linear_value)
{
    uint32_t corrected_value = apply_gamma_internal(ctx, channel, linear_value);
    ctx->driver_cb(channel, corrected_value);
}
