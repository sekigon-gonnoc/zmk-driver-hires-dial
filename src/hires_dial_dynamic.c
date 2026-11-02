/*
 * Copyright 2026 sekigon-gonnoc
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT sekigon_hires_dial_source

#include <errno.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/iterable_sections.h>
#include <zephyr/sys/util.h>

#include <drivers/behavior.h>
#include <zmk/behavior.h>
#include <zmk/dynamic_encoder/encoder.h>
#include <zmk/dynamic_settings/store.h>
#include <zmk/hires_dial/dynamic.h>
#include <zmk/keymap.h>
#include <zmk/sensors.h>

#define DIAL_RECORD_SIZE 24
#define DIAL_RECORD_VERSION 1
#define DIAL_RECORD_MODE_OFFSET 1
#define DIAL_RECORD_PAYLOAD_OFFSET 2
#define DIAL_SCROLL_RECORD_USED_SIZE 12
#define DIAL_DIRECTION_SIZE 11

struct dial_direction {
    uint8_t action;
    struct zmk_behavior_binding binding;
};

struct dial_slot {
    uint8_t record[DIAL_RECORD_SIZE];
    struct zmk_behavior_binding binding;
    struct dial_direction cw;
    struct dial_direction ccw;
    bool overridden;
    bool resolved;
};

struct dial_source {
    uint8_t sensor_index;
    const struct device *device;
};

#define DIAL_SOURCE(node_id)                                                                       \
    {.sensor_index = DT_PROP(node_id, sensor_index), .device = DEVICE_DT_GET(node_id)},

#define DIAL_SOURCE_COUNT DT_NUM_INST_STATUS_OKAY(sekigon_hires_dial_source)
#if DIAL_SOURCE_COUNT > 0
static const struct dial_source dial_sources[] = {
    DT_FOREACH_STATUS_OKAY(sekigon_hires_dial_source, DIAL_SOURCE)};
#else
static const struct dial_source dial_sources[1] = {{0}};
#endif

static struct dial_slot dial_slots[ZMK_KEYMAP_LAYERS_LEN];
K_MUTEX_DEFINE(dial_mutex);

dynamic_setting_record_on_change_fn hires_dial_dynamic_record_changed;

static int dial_source_init(const struct device *dev) {
    ARG_UNUSED(dev);
    return 0;
}

#define DIAL_SOURCE_DEVICE(node_id)                                                                \
    DEVICE_DT_DEFINE(node_id, dial_source_init, NULL, NULL, NULL, POST_KERNEL, 99, NULL);

#define DIAL_SOURCE_ASSERT(node_id)                                                                \
    BUILD_ASSERT(DT_PROP(node_id, sensor_index) == 0,                                              \
                 "high-resolution dial dynamic source must use sensor-index 0");                   \
    BUILD_ASSERT(DT_SAME_NODE(DT_PHANDLE(node_id, sensor),                                         \
                              ZMK_KEYMAP_SENSORS_BY_IDX(DT_PROP(node_id, sensor_index))),          \
                 "dial sensor phandle and sensor-index disagree");

DT_FOREACH_STATUS_OKAY(sekigon_hires_dial_source, DIAL_SOURCE_ASSERT)
DT_FOREACH_STATUS_OKAY(sekigon_hires_dial_source, DIAL_SOURCE_DEVICE)

#if DT_NODE_EXISTS(DT_NODELABEL(hires_dial_encoder))
static struct zmk_behavior_binding dial_encoder_binding = {
    .behavior_dev = DEVICE_DT_NAME(DT_NODELABEL(hires_dial_encoder)),
};
#endif

static uint16_t behavior_identity(const char *name) {
    uint32_t hash = 2166136261u;
    for (const uint8_t *p = (const uint8_t *)name; *p; p++) {
        hash = (hash ^ *p) * 16777619u;
    }
    hash ^= hash >> 16;
    return (uint16_t)hash;
}

static const struct device *resolve_behavior(uint16_t identity) {
    const struct device *match = NULL;
    STRUCT_SECTION_FOREACH(zmk_behavior_ref, ref) {
        if (!ref->device) {
            continue;
        }
        bool identity_matches = behavior_identity(ref->device->name) == identity;
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
        if (ref->metadata.display_name &&
            behavior_identity(ref->metadata.display_name) == identity) {
            identity_matches = true;
        }
#endif
        if (!identity_matches) {
            continue;
        }
        if (match && match != ref->device) {
            return NULL;
        }
        match = ref->device;
    }
    if (!match || !device_is_ready(match)) {
        return NULL;
    }
    return match;
}

static int resolve_behavior_binding(const uint8_t *record, struct zmk_behavior_binding *binding) {
    const uint16_t identity = sys_get_le16(record);
    if (!identity) {
        return -EINVAL;
    }
    const struct device *behavior = resolve_behavior(identity);
    if (!behavior) {
        return -ENOENT;
    }
    *binding = (struct zmk_behavior_binding){
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_LOCAL_IDS_IN_BINDINGS)
        .local_id = zmk_behavior_get_local_id(behavior->name),
#endif
        .behavior_dev = behavior->name,
        .param1 = sys_get_le32(record + 2),
        .param2 = sys_get_le32(record + 6),
    };
    int err = zmk_behavior_validate_binding(binding);
    return (err == -ENOTSUP || err == -ENODEV) ? 0 : err;
}

static int resolve_record(struct dial_slot *slot) {
    const uint8_t *record = slot->record;
    if (record[0] != DIAL_RECORD_VERSION) {
        return -EINVAL;
    }
    memset(&slot->binding, 0, sizeof(slot->binding));
    memset(&slot->cw, 0, sizeof(slot->cw));
    memset(&slot->ccw, 0, sizeof(slot->ccw));

    const uint8_t mode = record[DIAL_RECORD_MODE_OFFSET];
    if (mode == ZMK_HIRES_DIAL_DYNAMIC_MODE_STATIC) {
        for (size_t i = DIAL_RECORD_PAYLOAD_OFFSET; i < DIAL_RECORD_SIZE; i++) {
            if (record[i]) {
                return -EINVAL;
            }
        }
        slot->resolved = true;
        return 0;
    }
    if (mode == ZMK_HIRES_DIAL_DYNAMIC_MODE_BINDING) {
        for (size_t i = DIAL_SCROLL_RECORD_USED_SIZE; i < DIAL_RECORD_SIZE; i++) {
            if (record[i]) {
                return -EINVAL;
            }
        }
        int err = resolve_behavior_binding(record + DIAL_RECORD_PAYLOAD_OFFSET, &slot->binding);
        if (!err) {
            slot->resolved = true;
        }
        return err;
    }
    if (mode != ZMK_HIRES_DIAL_DYNAMIC_MODE_ENCODER) {
        return -EINVAL;
    }

    for (int direction = 0; direction < ZMK_HIRES_DIAL_DYNAMIC_DIRECTION_COUNT; direction++) {
        const uint8_t *direction_record =
            record + DIAL_RECORD_PAYLOAD_OFFSET + (size_t)direction * DIAL_DIRECTION_SIZE;
        const uint8_t *binding_data = direction_record + 1;
        const uint8_t action = direction_record[0];
        struct dial_direction *target =
            direction == ZMK_HIRES_DIAL_DYNAMIC_DIRECTION_CW ? &slot->cw : &slot->ccw;
        if (action > ZMK_HIRES_DIAL_DYNAMIC_ACTION_BEHAVIOR) {
            return -EINVAL;
        }
        target->action = action;
        if (action != ZMK_HIRES_DIAL_DYNAMIC_ACTION_BEHAVIOR) {
            if (sys_get_le16(binding_data) || sys_get_le32(binding_data + 2) ||
                sys_get_le32(binding_data + 6)) {
                return -EINVAL;
            }
            continue;
        }
        int err = resolve_behavior_binding(binding_data, &target->binding);
        if (err) {
            return err;
        }
    }
    slot->resolved = true;
    return 0;
}

static bool slot_layer_for_group(const struct dynamic_setting_group_descriptor *group,
                                 const void *context, uint8_t *layer_id) {
    const struct device *source_device = context ? context : group->context;
    bool is_dial_source = false;
    for (size_t i = 0; i < DIAL_SOURCE_COUNT; i++) {
        if (dial_sources[i].device == source_device && dial_sources[i].sensor_index == 0) {
            is_dial_source = true;
            break;
        }
    }
    if (!is_dial_source) {
        return false;
    }

    uint8_t layer = 0;
    for (size_t i = 0; i < dynamic_setting_group_count; i++) {
        const struct dynamic_setting_group_descriptor *candidate = &dynamic_setting_groups[i];
        if (candidate == group) {
            *layer_id = layer;
            return layer < ZMK_KEYMAP_LAYERS_LEN;
        }
        if (candidate->context == source_device &&
            candidate->record_on_change == hires_dial_dynamic_record_changed) {
            layer++;
        }
    }
    return false;
}

int hires_dial_dynamic_record_changed(const struct dynamic_setting_group_descriptor *group,
                                      const uint8_t *record, size_t length,
                                      enum dynamic_setting_change_reason reason,
                                      const void *context) {
    uint8_t layer_id;
    if (!record || length != DIAL_RECORD_SIZE || record[0] != DIAL_RECORD_VERSION ||
        !slot_layer_for_group(group, context, &layer_id)) {
        return -EINVAL;
    }
    struct dial_slot staged = {0};
    memcpy(staged.record, record, length);
    if (reason != DYNAMIC_SETTING_CHANGE_BOOT_LOAD) {
        int err = resolve_record(&staged);
        if (err) {
            return err;
        }
    }

    k_mutex_lock(&dial_mutex, K_FOREVER);
    struct dial_slot *slot = &dial_slots[layer_id];
    memcpy(slot->record, staged.record, sizeof(slot->record));
    slot->binding = staged.binding;
    slot->cw = staged.cw;
    slot->ccw = staged.ccw;
    slot->overridden = record[DIAL_RECORD_MODE_OFFSET] != ZMK_HIRES_DIAL_DYNAMIC_MODE_STATIC;
    slot->resolved = staged.resolved;
    k_mutex_unlock(&dial_mutex);
    return 0;
}

struct zmk_behavior_binding *
zmk_hires_dial_dynamic_sensor_binding_lookup(uint8_t sensor_index, uint8_t layer_id,
                                             struct zmk_behavior_binding *static_binding) {
    if (sensor_index != 0 || layer_id >= ZMK_KEYMAP_LAYERS_LEN) {
        return NULL;
    }
    bool is_dial_sensor = false;
    for (size_t i = 0; i < DIAL_SOURCE_COUNT; i++) {
        if (dial_sources[i].sensor_index == sensor_index) {
            is_dial_sensor = true;
            break;
        }
    }
    if (!is_dial_sensor) {
        return NULL;
    }

    k_mutex_lock(&dial_mutex, K_FOREVER);
    struct dial_slot *slot = &dial_slots[layer_id];
    if (!slot->overridden || (!slot->resolved && resolve_record(slot) != 0)) {
        k_mutex_unlock(&dial_mutex);
        return static_binding;
    }
    const uint8_t mode = slot->record[DIAL_RECORD_MODE_OFFSET];
    struct zmk_behavior_binding *binding = static_binding;
    if (mode == ZMK_HIRES_DIAL_DYNAMIC_MODE_BINDING) {
        binding = &slot->binding;
    }
#if DT_NODE_EXISTS(DT_NODELABEL(hires_dial_encoder))
    else if (mode == ZMK_HIRES_DIAL_DYNAMIC_MODE_ENCODER) {
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_LOCAL_IDS_IN_BINDINGS)
        dial_encoder_binding.local_id =
            zmk_behavior_get_local_id(dial_encoder_binding.behavior_dev);
#endif
        binding = &dial_encoder_binding;
    }
#endif
    k_mutex_unlock(&dial_mutex);
    return binding;
}

bool zmk_hires_dial_dynamic_encoder_bindings_lookup(uint8_t sensor_index, uint8_t layer_id,
                                                    struct zmk_behavior_binding *cw,
                                                    uint8_t *cw_action,
                                                    struct zmk_behavior_binding *ccw,
                                                    uint8_t *ccw_action) {
    if (sensor_index != 0 || !cw || !cw_action || !ccw || !ccw_action ||
        layer_id >= ZMK_KEYMAP_LAYERS_LEN) {
        return false;
    }
    k_mutex_lock(&dial_mutex, K_FOREVER);
    struct dial_slot *slot = &dial_slots[layer_id];
    const bool found = slot->overridden && (slot->resolved || resolve_record(slot) == 0) &&
                       slot->record[DIAL_RECORD_MODE_OFFSET] == ZMK_HIRES_DIAL_DYNAMIC_MODE_ENCODER;
    if (found) {
        *cw = slot->cw.binding;
        *cw_action = slot->cw.action;
        *ccw = slot->ccw.binding;
        *ccw_action = slot->ccw.action;
    }
    k_mutex_unlock(&dial_mutex);
    return found;
}

static struct zmk_behavior_binding *
dial_binding_provider(uint8_t sensor_index, uint8_t layer_id,
                      struct zmk_behavior_binding *static_binding) {
    return zmk_hires_dial_dynamic_sensor_binding_lookup(sensor_index, layer_id, static_binding);
}

DYNAMIC_ENCODER_BINDING_PROVIDER_DEFINE(hires_dial_binding_provider, dial_binding_provider);
