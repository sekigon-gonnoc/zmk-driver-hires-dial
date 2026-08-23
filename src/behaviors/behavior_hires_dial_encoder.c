/*
 * Copyright (c) 2026 sekigon-gonnoc
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_hires_dial_encoder

#include <zephyr/device.h>
#include <zephyr/logging/log.h>

#include <drivers/behavior.h>
#include <zmk/behavior.h>
#include <zmk/behavior_queue.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/keymap.h>
#include <zmk/sensors.h>
#include <zmk/virtual_key_position.h>

#include <zmk/hires_dial/conversion.h>

LOG_MODULE_DECLARE(zmk_hires_dial, CONFIG_ZMK_HIRES_DIAL_LOG_LEVEL);

struct encoder_config {
    struct zmk_behavior_binding cw;
    struct zmk_behavior_binding ccw;
    uint32_t counts_per_revolution;
    uint32_t tap_ms;
};

struct encoder_data {
    int32_t accepted[ZMK_KEYMAP_SENSORS_LEN][ZMK_KEYMAP_LAYERS_LEN];
    int64_t remainder[ZMK_KEYMAP_SENSORS_LEN][ZMK_KEYMAP_LAYERS_LEN];
};

static int accept_data(struct zmk_behavior_binding *binding,
                       struct zmk_behavior_binding_event event,
                       const struct zmk_sensor_config *sensor_config, size_t channel_data_size,
                       const struct zmk_sensor_channel_data *channel_data) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    const struct encoder_config *cfg = dev->config;
    struct encoder_data *data = dev->data;
    int sensor = ZMK_SENSOR_POSITION_FROM_VIRTUAL_KEY_POSITION(event.position);
    struct sensor_value value = channel_data[0].value;
    data->accepted[sensor][event.layer] =
        zmk_hires_dial_value_to_counts(&value, cfg->counts_per_revolution);
    LOG_DBG("Encoder accept: layer=%u sensor=%d value=(%d, %d) counts=%d", event.layer, sensor,
            value.val1, value.val2, data->accepted[sensor][event.layer]);
    return 0;
}

static int process(struct zmk_behavior_binding *binding, struct zmk_behavior_binding_event event,
                   enum behavior_sensor_binding_process_mode mode) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    const struct encoder_config *cfg = dev->config;
    struct encoder_data *data = dev->data;
    int sensor = ZMK_SENSOR_POSITION_FROM_VIRTUAL_KEY_POSITION(event.position);
    int32_t counts = data->accepted[sensor][event.layer];
    data->accepted[sensor][event.layer] = 0;

    if (mode != BEHAVIOR_SENSOR_BINDING_PROCESS_MODE_TRIGGER || counts == 0) {
        return ZMK_BEHAVIOR_TRANSPARENT;
    }

    const struct zmk_sensor_config *sensor_cfg = zmk_sensors_get_config_at_index(sensor);
    data->remainder[sensor][event.layer] += (int64_t)counts * sensor_cfg->triggers_per_rotation;
    int32_t triggers = data->remainder[sensor][event.layer] / cfg->counts_per_revolution;
    data->remainder[sensor][event.layer] -= (int64_t)triggers * cfg->counts_per_revolution;

    LOG_DBG("Encoder process: layer=%u mode=%d counts=%d remainder=%lld triggers=%d", event.layer,
            mode, counts, data->remainder[sensor][event.layer], triggers);

    if (triggers == 0) {
        return ZMK_BEHAVIOR_OPAQUE;
    }

    struct zmk_behavior_binding output = triggers > 0 ? cfg->cw : cfg->ccw;
    output.param1 = triggers > 0 ? binding->param1 : binding->param2;
    int count = triggers > 0 ? triggers : -triggers;

#if IS_ENABLED(CONFIG_ZMK_SPLIT)
    event.source = ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL;
#endif
    for (int i = 0; i < count; i++) {
        int err = zmk_behavior_queue_add(&event, output, true, cfg->tap_ms);
        if (err < 0) {
            LOG_ERR("Failed to queue encoder press: %d", err);
            return err;
        }
        err = zmk_behavior_queue_add(&event, output, false, 0);
        if (err < 0) {
            LOG_ERR("Failed to queue encoder release: %d", err);
            return err;
        }
    }
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api api = {
    .sensor_binding_accept_data = accept_data,
    .sensor_binding_process = process,
};

#define ENCODER_INST(n)                                                                            \
    static const struct encoder_config config_##n = {                                              \
        .cw = {.behavior_dev = DEVICE_DT_NAME(DT_INST_PHANDLE_BY_IDX(n, bindings, 0))},            \
        .ccw = {.behavior_dev = DEVICE_DT_NAME(DT_INST_PHANDLE_BY_IDX(n, bindings, 1))},           \
        .counts_per_revolution = DT_PROP(DT_INST_PHANDLE(n, sensor), counts_per_revolution),       \
        .tap_ms = DT_INST_PROP(n, tap_ms),                                                         \
    };                                                                                             \
    static struct encoder_data data_##n;                                                           \
    BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, &data_##n, &config_##n, POST_KERNEL,                    \
                            CONFIG_ZMK_HIRES_DIAL_BEHAVIOR_INIT_PRIORITY, &api);

DT_INST_FOREACH_STATUS_OKAY(ENCODER_INST)
