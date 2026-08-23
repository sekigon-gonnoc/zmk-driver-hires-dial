/*
 * Copyright (c) 2026 sekigon-gonnoc
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#define ZMK_HIRES_DIAL_RADIAL_CONTROLLER_REPORT_ID 0x01

struct zmk_hires_dial_radial_controller_report_body {
    uint16_t button_and_dial;
} __packed;

struct zmk_hires_dial_radial_controller_report {
    uint8_t report_id;
    struct zmk_hires_dial_radial_controller_report_body body;
} __packed;

void zmk_hires_dial_radial_controller_set_button(bool pressed);
int zmk_hires_dial_radial_controller_send(int16_t tenth_degrees);
struct zmk_hires_dial_radial_controller_report *zmk_hires_dial_radial_controller_get_report(void);

int zmk_hires_dial_radial_controller_endpoints_send(void);
int zmk_hires_dial_radial_controller_usb_send(void);
int zmk_hires_dial_radial_controller_hog_send(
    const struct zmk_hires_dial_radial_controller_report_body *body);
