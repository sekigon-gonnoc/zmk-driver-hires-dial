/*
 * Copyright (c) 2026 sekigon-gonnoc
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_hires_dial_radial_controller

#include <zephyr/device.h>
#include <zephyr/logging/log.h>

#include <drivers/behavior.h>
#include <zmk/behavior.h>
#include <zmk/keymap.h>
#include <zmk/sensors.h>
#include <zmk/virtual_key_position.h>

#include <zmk/hires_dial/conversion.h>
#include <zmk/hires_dial/radial_controller.h>

LOG_MODULE_DECLARE(zmk_hires_dial, CONFIG_ZMK_HIRES_DIAL_LOG_LEVEL);

struct radial_config {
    uint32_t counts_per_revolution;
};

struct radial_data {
    int32_t accepted[ZMK_KEYMAP_SENSORS_LEN][ZMK_KEYMAP_LAYERS_LEN];
    int64_t remainder[ZMK_KEYMAP_SENSORS_LEN][ZMK_KEYMAP_LAYERS_LEN];
};

static int accept_data(struct zmk_behavior_binding *binding,
                       struct zmk_behavior_binding_event event,
                       const struct zmk_sensor_config *sensor_config, size_t channel_data_size,
                       const struct zmk_sensor_channel_data *channel_data) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    const struct radial_config *cfg = dev->config;
    struct radial_data *data = dev->data;
    int sensor = ZMK_SENSOR_POSITION_FROM_VIRTUAL_KEY_POSITION(event.position);
    struct sensor_value value = channel_data[0].value;
    data->accepted[sensor][event.layer] =
        zmk_hires_dial_value_to_counts(&value, cfg->counts_per_revolution);
    return 0;
}

static int process(struct zmk_behavior_binding *binding, struct zmk_behavior_binding_event event,
                   enum behavior_sensor_binding_process_mode mode) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    struct radial_data *data = dev->data;
    int sensor = ZMK_SENSOR_POSITION_FROM_VIRTUAL_KEY_POSITION(event.position);
    int32_t counts = data->accepted[sensor][event.layer];
    data->accepted[sensor][event.layer] = 0;

    if (mode != BEHAVIOR_SENSOR_BINDING_PROCESS_MODE_TRIGGER || counts == 0) {
        return ZMK_BEHAVIOR_TRANSPARENT;
    }

#if !IS_ENABLED(CONFIG_ZMK_HIRES_DIAL_RADIAL_CONTROLLER)
    return ZMK_BEHAVIOR_OPAQUE;
#else
    const struct radial_config *cfg = dev->config;
    if (binding->param1 == 0 || binding->param2 == 0) {
        LOG_ERR("Radial Controller scale must be positive: %u/%u", binding->param2,
                binding->param1);
        return -EINVAL;
    }

    int64_t divisor = (int64_t)cfg->counts_per_revolution * binding->param1;
    data->remainder[sensor][event.layer] += (int64_t)counts * 3600 * binding->param2;
    int32_t tenth_degrees = data->remainder[sensor][event.layer] / divisor;
    data->remainder[sensor][event.layer] -= (int64_t)tenth_degrees * divisor;

    while (tenth_degrees != 0) {
        int16_t report_value = CLAMP(tenth_degrees, -3600, 3600);
        int err = zmk_hires_dial_radial_controller_send(report_value);
        if (err < 0) {
            return err;
        }
        tenth_degrees -= report_value;
    }
    return ZMK_BEHAVIOR_OPAQUE;
#endif
}

static const struct behavior_driver_api api = {
    .sensor_binding_accept_data = accept_data,
    .sensor_binding_process = process,
};

#define RADIAL_INST(n)                                                                             \
    static const struct radial_config config_##n = {                                               \
        .counts_per_revolution = DT_PROP(DT_INST_PHANDLE(n, sensor), counts_per_revolution),       \
    };                                                                                             \
    static struct radial_data data_##n;                                                            \
    BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, &data_##n, &config_##n, POST_KERNEL,                    \
                            CONFIG_ZMK_HIRES_DIAL_BEHAVIOR_INIT_PRIORITY, &api);

DT_INST_FOREACH_STATUS_OKAY(RADIAL_INST)
