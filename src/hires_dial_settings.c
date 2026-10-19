/*
 * Copyright 2026 sekigon-gonnoc
 * SPDX-License-Identifier: MIT
 */

#include <errno.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/sys/byteorder.h>

#include <zmk/dynamic_settings/store.h>
#include <zmk/hires_dial/hires_dial.h>

int hires_dial_dynamic_counts_per_revolution_changed(
    const struct dynamic_setting_descriptor *descriptor, const struct dynamic_setting_value *value,
    enum dynamic_setting_change_reason reason, const void *context) {
    (void)descriptor;
    (void)reason;

    if (!value || value->type != DYNAMIC_SETTING_TYPE_U16 || value->length != sizeof(uint16_t)) {
        return -EINVAL;
    }

    const struct device *device = context;
    if (!device) {
        return -ENODEV;
    }

    return zmk_hires_dial_set_counts_per_revolution(device, sys_get_le16(value->data));
}
