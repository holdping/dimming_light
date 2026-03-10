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
    uint32_t fade_duration_ms;
    uint32_t hold_duration_ms;
    gamma_type_t gamma_type;
    bool gamma_enabled;
} light_bulb_scene_t;

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
    uint32_t dimming_timer_period_ms;
    uint32_t max_value;
    light_bulb_scene_t scene;
    light_bulb_driver_t driver;
} light_bulb_config_t;

void light_bulb_get_default_config(light_bulb_config_t *config);

light_bulb_handle_t light_bulb_create(const light_bulb_config_t *config);
void light_bulb_destroy(light_bulb_handle_t handle);

bool light_bulb_apply_scene(light_bulb_handle_t handle, const light_bulb_scene_t *scene);
bool light_bulb_transition_rgb(light_bulb_handle_t handle, uint8_t red, uint8_t green, uint8_t blue);
bool light_bulb_transition_color(light_bulb_handle_t handle, light_bulb_color_t color);
bool light_bulb_is_fading(light_bulb_handle_t handle);
void light_bulb_wait_fade_done(light_bulb_handle_t handle);
void light_bulb_stop(light_bulb_handle_t handle);

void light_bulb_run_color_cycle(light_bulb_handle_t handle,
                                const light_bulb_color_t *colors,
                                size_t color_count,
                                uint32_t loop_count);

#ifdef __cplusplus
}
#endif

#endif /* LIGHT_BULB_H */
