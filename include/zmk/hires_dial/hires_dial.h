/*
 * Copyright 2026 sekigon-gonnoc
 * SPDX-License-Identifier: MIT
 */

#ifndef ZMK_HIRES_DIAL_HIRES_DIAL_H
#define ZMK_HIRES_DIAL_HIRES_DIAL_H

#include <stdint.h>

struct device;

/* Update the calibration used to convert PAT912x counts into rotation. */
int zmk_hires_dial_set_counts_per_revolution(const struct device *dev,
                                             uint32_t counts_per_revolution);

#endif /* ZMK_HIRES_DIAL_HIRES_DIAL_H */
