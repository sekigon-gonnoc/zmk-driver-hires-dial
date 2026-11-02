/* SPDX-License-Identifier: MIT */

#pragma once

#include <drivers/behavior.h>
#include <stdbool.h>
#include <stdint.h>

enum zmk_hires_dial_dynamic_mode {
    ZMK_HIRES_DIAL_DYNAMIC_MODE_STATIC = 0,
    ZMK_HIRES_DIAL_DYNAMIC_MODE_BINDING = 1,
    ZMK_HIRES_DIAL_DYNAMIC_MODE_ENCODER = 2,
};

enum zmk_hires_dial_dynamic_direction {
    ZMK_HIRES_DIAL_DYNAMIC_DIRECTION_CW,
    ZMK_HIRES_DIAL_DYNAMIC_DIRECTION_CCW,
    ZMK_HIRES_DIAL_DYNAMIC_DIRECTION_COUNT,
};

enum zmk_hires_dial_dynamic_action {
    ZMK_HIRES_DIAL_DYNAMIC_ACTION_TRANSPARENT = 0,
    ZMK_HIRES_DIAL_DYNAMIC_ACTION_NONE = 1,
    ZMK_HIRES_DIAL_DYNAMIC_ACTION_BEHAVIOR = 2,
};

enum zmk_hires_dial_dynamic_binding_kind {
    ZMK_HIRES_DIAL_DYNAMIC_BINDING_STATIC,
    ZMK_HIRES_DIAL_DYNAMIC_BINDING_BEHAVIOR,
    ZMK_HIRES_DIAL_DYNAMIC_BINDING_ENCODER,
};

static inline enum zmk_hires_dial_dynamic_binding_kind
zmk_hires_dial_dynamic_binding_kind_from_mode(uint8_t mode) {
    switch (mode) {
    case ZMK_HIRES_DIAL_DYNAMIC_MODE_BINDING:
        return ZMK_HIRES_DIAL_DYNAMIC_BINDING_BEHAVIOR;
    case ZMK_HIRES_DIAL_DYNAMIC_MODE_ENCODER:
        return ZMK_HIRES_DIAL_DYNAMIC_BINDING_ENCODER;
    default:
        return ZMK_HIRES_DIAL_DYNAMIC_BINDING_STATIC;
    }
}

/* Called by the dynamic encoder adapter for high-resolution dial sources. */
struct zmk_behavior_binding *zmk_hires_dial_dynamic_sensor_binding_lookup(
    uint8_t sensor_index, uint8_t layer_id, struct zmk_behavior_binding *static_binding);

/* Called by the high-resolution dial encoder behavior for direction overrides. */
bool zmk_hires_dial_dynamic_encoder_bindings_lookup(
    uint8_t sensor_index, uint8_t layer_id, struct zmk_behavior_binding *cw, uint8_t *cw_action,
    struct zmk_behavior_binding *ccw, uint8_t *ccw_action);
