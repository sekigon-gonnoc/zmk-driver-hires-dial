/*
 * Copyright (c) 2026 sekigon-gonnoc
 * SPDX-License-Identifier: MIT
 */

#include <errno.h>

#include <zmk/endpoints.h>
#include <zmk/hires_dial/radial_controller.h>

int zmk_hires_dial_radial_controller_endpoints_send(void) {
    struct zmk_endpoint_instance endpoint = zmk_endpoints_selected();

    switch (endpoint.transport) {
#if IS_ENABLED(CONFIG_ZMK_USB)
    case ZMK_TRANSPORT_USB:
        return zmk_hires_dial_radial_controller_usb_send();
#endif
#if IS_ENABLED(CONFIG_ZMK_BLE)
    case ZMK_TRANSPORT_BLE:
        return zmk_hires_dial_radial_controller_hog_send(
            &zmk_hires_dial_radial_controller_get_report()->body);
#endif
    default:
        return -ENOTSUP;
    }
}
