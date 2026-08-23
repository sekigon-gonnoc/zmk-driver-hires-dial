/*
 * Copyright (c) 2026 sekigon-gonnoc
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdint.h>
#include <zephyr/drivers/sensor.h>

#define ZMK_HIRES_DIAL_MICRODEGREES_PER_REVOLUTION 360000000LL

static inline int32_t zmk_hires_dial_value_to_counts(const struct sensor_value *value,
                                                     uint32_t counts_per_revolution) {
    int64_t microdegrees = (int64_t)value->val1 * 1000000LL + value->val2;
    int64_t scaled = microdegrees * counts_per_revolution;

    if (scaled >= 0) {
        scaled += ZMK_HIRES_DIAL_MICRODEGREES_PER_REVOLUTION / 2;
    } else {
        scaled -= ZMK_HIRES_DIAL_MICRODEGREES_PER_REVOLUTION / 2;
    }

    return (int32_t)(scaled / ZMK_HIRES_DIAL_MICRODEGREES_PER_REVOLUTION);
}
