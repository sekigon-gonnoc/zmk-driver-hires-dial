/*
 * Copyright (c) 2024 The ZMK Contributors
 * Modifications Copyright (c) 2026 sekigon-gonnoc
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/usb/usb_device.h>
#include <zephyr/usb/class/usb_hid.h>

#include <zmk/hires_dial/radial_controller.h>
#include <zmk/hires_dial/radial_controller_hid.h>
#include <zmk/usb.h>

static const struct device *hid_dev;
K_SEM_DEFINE(hid_sem, 1, 1);

static void in_ready(const struct device *dev) { k_sem_give(&hid_sem); }

static int get_report(const struct device *dev, struct usb_setup_packet *setup, int32_t *len,
                      uint8_t **data) {
    if ((setup->wValue & 0xff00) != 0x0100 ||
        (setup->wValue & 0x00ff) != ZMK_HIRES_DIAL_RADIAL_CONTROLLER_REPORT_ID) {
        return -ENOTSUP;
    }

    struct zmk_hires_dial_radial_controller_report *report =
        zmk_hires_dial_radial_controller_get_report();
    *data = (uint8_t *)report;
    *len = sizeof(*report);
    return 0;
}

static const struct hid_ops ops = {
    .int_in_ready = in_ready,
    .get_report = get_report,
};

int zmk_hires_dial_radial_controller_usb_send(void) {
    switch (zmk_usb_get_status()) {
    case USB_DC_SUSPEND:
        return usb_wakeup_request();
    case USB_DC_ERROR:
    case USB_DC_RESET:
    case USB_DC_DISCONNECTED:
    case USB_DC_UNKNOWN:
        return -ENODEV;
    default:
        break;
    }

    struct zmk_hires_dial_radial_controller_report *report =
        zmk_hires_dial_radial_controller_get_report();
    int err = k_sem_take(&hid_sem, K_MSEC(30));
    if (err < 0) {
        return err;
    }
    err = hid_int_ep_write(hid_dev, (uint8_t *)report, sizeof(*report), NULL);
    if (err < 0) {
        k_sem_give(&hid_sem);
    }
    return err;
}

static int init(void) {
    hid_dev = device_get_binding("HID_1");
    if (!hid_dev) {
        return -ENODEV;
    }
    usb_hid_register_device(hid_dev, zmk_hires_dial_radial_controller_report_desc,
                            zmk_hires_dial_radial_controller_report_desc_size, &ops);
    return usb_hid_init(hid_dev);
}

SYS_INIT(init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
