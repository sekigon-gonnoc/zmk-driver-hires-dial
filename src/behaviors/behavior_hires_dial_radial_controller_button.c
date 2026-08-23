/*
 * Copyright (c) 2026 sekigon-gonnoc
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_hires_dial_radial_controller_button

#include <zephyr/device.h>

#include <drivers/behavior.h>
#include <zmk/behavior.h>

#include <zmk/hires_dial/radial_controller.h>

static int pressed(struct zmk_behavior_binding *binding, struct zmk_behavior_binding_event event) {
#if IS_ENABLED(CONFIG_ZMK_HIRES_DIAL_RADIAL_CONTROLLER)
    zmk_hires_dial_radial_controller_set_button(true);
    return zmk_hires_dial_radial_controller_send(0);
#else
    return ZMK_BEHAVIOR_OPAQUE;
#endif
}

static int released(struct zmk_behavior_binding *binding, struct zmk_behavior_binding_event event) {
#if IS_ENABLED(CONFIG_ZMK_HIRES_DIAL_RADIAL_CONTROLLER)
    zmk_hires_dial_radial_controller_set_button(false);
    return zmk_hires_dial_radial_controller_send(0);
#else
    return ZMK_BEHAVIOR_OPAQUE;
#endif
}

static const struct behavior_driver_api api = {
    .binding_pressed = pressed,
    .binding_released = released,
};

#define BUTTON_INST(n)                                                                             \
    BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, NULL, NULL, POST_KERNEL,                                \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &api);

DT_INST_FOREACH_STATUS_OKAY(BUTTON_INST)
