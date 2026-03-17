#ifndef LIGHT_BULB_H
#define LIGHT_BULB_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "dimming_lib.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct light_bulb *light_bulb_handle_t;

typedef struct {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
} light_bulb_color_t;

typedef struct {
    uint32_t warm;
    uint32_t cool;
} light_bulb_cct_t;

typedef struct {
    uint32_t fade_duration_ms;
    uint32_t hold_duration_ms;
    gamma_type_t gamma_type;
    bool gamma_enabled;
} light_bulb_scene_t;

typedef enum {
    LIGHT_BULB_EFFECT_NONE = 0,
    LIGHT_BULB_EFFECT_BREATH,
    LIGHT_BULB_EFFECT_PULSE,
    LIGHT_BULB_EFFECT_RAINBOW,
    LIGHT_BULB_EFFECT_FIRE,
    LIGHT_BULB_EFFECT_STROBE,
    LIGHT_BULB_EFFECT_COLOR_WIPE,
    LIGHT_BULB_EFFECT_THEATER_CHASE,
    LIGHT_BULB_EFFECT_SCANNER,
    LIGHT_BULB_EFFECT_LARSON_SCANNER,
    LIGHT_BULB_EFFECT_LIGHTNING,
    LIGHT_BULB_EFFECT_OCEAN_WAVE,
    LIGHT_BULB_EFFECT_AURORA,
    LIGHT_BULB_EFFECT_FOREST_BREEZE,
} light_bulb_effect_t;

typedef struct {
    light_bulb_effect_t type;
    uint32_t speed_ms;
    uint32_t intensity;
    uint32_t color_count;
    const light_bulb_color_t *colors;
} light_bulb_effect_config_t;

typedef bool (*light_bulb_driver_init_cb_t)(void *user_ctx);
typedef void (*light_bulb_driver_deinit_cb_t)(void *user_ctx);
typedef bool (*light_bulb_driver_set_channel_cb_t)(void *user_ctx,
                                                    uint8_t channel,
                                                    uint32_t value,
                                                    uint32_t max_value);

typedef struct {
    void *user_ctx;
    uint8_t channel_count;
    light_bulb_driver_init_cb_t init;
    light_bulb_driver_deinit_cb_t deinit;
    light_bulb_driver_set_channel_cb_t set_channel;
} light_bulb_driver_t;

typedef struct {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    uint8_t warm;
    uint8_t cool;
} light_bulb_channel_map_t;

typedef struct {
    uint32_t dimming_timer_period_ms;
    uint32_t max_value;
    light_bulb_scene_t scene;
    light_bulb_driver_t driver;
    light_bulb_channel_map_t map;
} light_bulb_config_t;

#define LIGHT_BULB_CHANNEL_INVALID 0xFFU

/* 预设颜色定义 */
#define LIGHT_BULB_COLOR_RED        {255, 0, 0}
#define LIGHT_BULB_COLOR_GREEN      {0, 255, 0}
#define LIGHT_BULB_COLOR_BLUE       {0, 0, 255}
#define LIGHT_BULB_COLOR_WHITE      {255, 255, 255}
#define LIGHT_BULB_COLOR_WARM_WHITE {255, 200, 150}
#define LIGHT_BULB_COLOR_COOL_WHITE {200, 220, 255}
#define LIGHT_BULB_COLOR_YELLOW     {255, 255, 0}
#define LIGHT_BULB_COLOR_CYAN       {0, 255, 255}
#define LIGHT_BULB_COLOR_MAGENTA    {255, 0, 255}
#define LIGHT_BULB_COLOR_ORANGE     {255, 165, 0}
#define LIGHT_BULB_COLOR_PURPLE     {128, 0, 128}
#define LIGHT_BULB_COLOR_PINK       {255, 192, 203}
#define LIGHT_BULB_COLOR_AMBER      {255, 191, 0}
#define LIGHT_BULB_COLOR_OFF        {0, 0, 0}

/* 预设场景定义 */
#define LIGHT_BULB_SCENE_RELAX      {2000, 5000, GAMMA_22, true}
#define LIGHT_BULB_SCENE_READING    {1000, 0, GAMMA_22, true}
#define LIGHT_BULB_SCENE_NIGHT      {3000, 10000, GAMMA_18, true}
#define LIGHT_BULB_SCENE_PARTY      {500, 200, GAMMA_24, true}
#define LIGHT_BULB_SCENE_MOVIE      {1500, 3000, GAMMA_22, true}
#define LIGHT_BULB_SCENE_SUNRISE    {60000, 0, GAMMA_22, true}
#define LIGHT_BULB_SCENE_SUNSET     {45000, 0, GAMMA_22, true}

void light_bulb_get_default_config(light_bulb_config_t *config);

light_bulb_handle_t light_bulb_create(const light_bulb_config_t *config);
void light_bulb_destroy(light_bulb_handle_t handle);

bool light_bulb_apply_scene(light_bulb_handle_t handle, const light_bulb_scene_t *scene);
bool light_bulb_transition_rgb(light_bulb_handle_t handle, uint8_t red, uint8_t green, uint8_t blue);
bool light_bulb_transition_color(light_bulb_handle_t handle, light_bulb_color_t color);
bool light_bulb_transition_cct(light_bulb_handle_t handle, uint32_t warm, uint32_t cool);
bool light_bulb_transition_cct_pair(light_bulb_handle_t handle, light_bulb_cct_t cct);
bool light_bulb_transition_cw(light_bulb_handle_t handle, uint32_t cool_value);
bool light_bulb_transition_ww(light_bulb_handle_t handle, uint32_t warm_value);
bool light_bulb_transition_cct_percent(light_bulb_handle_t handle, uint8_t cool_percent, uint32_t brightness);
bool light_bulb_is_fading(light_bulb_handle_t handle);
void light_bulb_wait_fade_done(light_bulb_handle_t handle);
void light_bulb_stop(light_bulb_handle_t handle);

/* 颜色循环和动画效果 */
void light_bulb_run_color_cycle(light_bulb_handle_t handle,
                                const light_bulb_color_t *colors,
                                size_t color_count,
                                uint32_t loop_count);

void light_bulb_run_rainbow_effect(light_bulb_handle_t handle, uint32_t duration_ms, uint32_t loop_count);
void light_bulb_run_breath_effect(light_bulb_handle_t handle, light_bulb_color_t color, uint32_t period_ms, uint32_t loop_count);
void light_bulb_run_pulse_effect(light_bulb_handle_t handle, light_bulb_color_t color, uint32_t period_ms, uint32_t loop_count);
void light_bulb_run_strobe_effect(light_bulb_handle_t handle, light_bulb_color_t color, uint32_t on_ms, uint32_t off_ms, uint32_t loop_count);
void light_bulb_run_fire_effect(light_bulb_handle_t handle, uint32_t intensity, uint32_t duration_ms);
void light_bulb_run_color_wipe(light_bulb_handle_t handle, const light_bulb_color_t *colors, size_t color_count, uint32_t wipe_duration_ms, uint32_t loop_count);
void light_bulb_run_theater_chase(light_bulb_handle_t handle, light_bulb_color_t color1, light_bulb_color_t color2, uint32_t speed_ms, uint32_t loop_count);
void light_bulb_run_scanner_effect(light_bulb_handle_t handle, light_bulb_color_t color, uint32_t speed_ms, uint32_t loop_count);
void light_bulb_run_larson_scanner(light_bulb_handle_t handle, light_bulb_color_t color, uint32_t speed_ms, uint32_t loop_count);
void light_bulb_run_lightning_effect(light_bulb_handle_t handle, uint32_t intensity, uint32_t duration_ms);
void light_bulb_run_ocean_wave_effect(light_bulb_handle_t handle, uint32_t period_ms, uint32_t loop_count);
void light_bulb_run_aurora_effect(light_bulb_handle_t handle, uint32_t period_ms, uint32_t loop_count);
void light_bulb_run_forest_breeze_effect(light_bulb_handle_t handle, uint32_t period_ms, uint32_t loop_count);

/* 预设场景快捷方式 */
bool light_bulb_apply_relax_scene(light_bulb_handle_t handle);
bool light_bulb_apply_reading_scene(light_bulb_handle_t handle);
bool light_bulb_apply_night_scene(light_bulb_handle_t handle);
bool light_bulb_apply_party_scene(light_bulb_handle_t handle);
bool light_bulb_apply_movie_scene(light_bulb_handle_t handle);
bool light_bulb_sunrise_simulation(light_bulb_handle_t handle);
bool light_bulb_sunset_simulation(light_bulb_handle_t handle);

/* 颜色混合和转换 */
light_bulb_color_t light_bulb_color_mix(light_bulb_color_t color1, light_bulb_color_t color2, uint8_t ratio);
light_bulb_color_t light_bulb_color_brightness(light_bulb_color_t color, uint8_t brightness_percent);
light_bulb_color_t light_bulb_color_temperature_to_rgb(uint32_t kelvin, uint8_t brightness);
light_bulb_cct_t light_bulb_color_temperature_to_cct(uint32_t kelvin, uint32_t brightness);

/* 状态查询 */
bool light_bulb_get_current_rgb(light_bulb_handle_t handle, uint8_t *red, uint8_t *green, uint8_t *blue);
bool light_bulb_get_current_cct(light_bulb_handle_t handle, uint32_t *warm, uint32_t *cool);
light_bulb_color_t light_bulb_get_current_color(light_bulb_handle_t handle);
light_bulb_cct_t light_bulb_get_current_cct_pair(light_bulb_handle_t handle);

/* 效果控制 */
bool light_bulb_start_effect(light_bulb_handle_t handle, const light_bulb_effect_config_t *config);
void light_bulb_stop_effect(light_bulb_handle_t handle);
bool light_bulb_is_effect_running(light_bulb_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif /* LIGHT_BULB_H */
