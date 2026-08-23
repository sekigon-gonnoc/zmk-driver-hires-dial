/*
 * Copyright (c) 2026 sekigon-gonnoc
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/sys/byteorder.h>

#include <zmk/hires_dial/radial_controller.h>
#include <zmk/hires_dial/radial_controller_hid.h>

/* Minimal Windows Radial Controller top-level collection: Button 1 + 15-bit Dial. */
const uint8_t zmk_hires_dial_radial_controller_report_desc[] = {
    0x05, 0x01, /* Usage Page (Generic Desktop) */
    0x09, 0x0e, /* Usage (System Multi-Axis Controller) */
    0xa1, 0x01, /* Collection (Application) */
    0x85, ZMK_HIRES_DIAL_RADIAL_CONTROLLER_REPORT_ID,
    0x05, 0x0d, /* Usage Page (Digitizers) */
    0x09, 0x21, /* Usage (Puck) */
    0xa1, 0x00, /* Collection (Physical) */
    0x05, 0x09, /* Usage Page (Button) */
    0x09, 0x01, /* Usage (Button 1) */
    0x95, 0x01, /* Report Count (1) */
    0x75, 0x01, /* Report Size (1) */
    0x15, 0x00, /* Logical Minimum (0) */
    0x25, 0x01, /* Logical Maximum (1) */
    0x81, 0x02, /* Input (Data, Variable, Absolute) */
    0x05, 0x01, /* Usage Page (Generic Desktop) */
    0x09, 0x37, /* Usage (Dial) */
    0x95, 0x01, /* Report Count (1) */
    0x75, 0x0f, /* Report Size (15) */
    0x55, 0x0f, /* Unit Exponent (-1) */
    0x65, 0x14, /* Unit (Degrees, English Rotation) */
    0x36, 0xf0,
    0xf1, /* Physical Minimum (-3600) */
    0x46, 0x10,
    0x0e, /* Physical Maximum (3600) */
    0x16, 0xf0,
    0xf1, /* Logical Minimum (-3600) */
    0x26, 0x10,
    0x0e,       /* Logical Maximum (3600) */
    0x81, 0x06, /* Input (Data, Variable, Relative) */
    0xc0,       /* End Collection */
    0xc0,       /* End Collection */
};

const size_t zmk_hires_dial_radial_controller_report_desc_size =
    sizeof(zmk_hires_dial_radial_controller_report_desc);

static bool button_pressed;
static struct zmk_hires_dial_radial_controller_report report = {
    .report_id = ZMK_HIRES_DIAL_RADIAL_CONTROLLER_REPORT_ID,
};

void zmk_hires_dial_radial_controller_set_button(bool pressed) { button_pressed = pressed; }

struct zmk_hires_dial_radial_controller_report *zmk_hires_dial_radial_controller_get_report(void) {
    return &report;
}

int zmk_hires_dial_radial_controller_send(int16_t tenth_degrees) {
    uint16_t packed = (((uint16_t)tenth_degrees & 0x7fffU) << 1) | (button_pressed ? 1U : 0U);
    report.body.button_and_dial = sys_cpu_to_le16(packed);
    return zmk_hires_dial_radial_controller_endpoints_send();
}
