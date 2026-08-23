/*
 * Copyright (c) 2024 The ZMK Contributors
 * Modifications Copyright (c) 2026 sekigon-gonnoc
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>

#include <zmk/ble.h>
#include <zmk/hires_dial/radial_controller.h>
#include <zmk/hires_dial/radial_controller_hid.h>

struct hids_info {
    uint16_t version;
    uint8_t code;
    uint8_t flags;
} __packed;

struct hids_report_ref {
    uint8_t id;
    uint8_t type;
} __packed;

static struct hids_info info = {
    .flags = BIT(0) | BIT(1),
};
static struct hids_report_ref input_ref = {
    .id = ZMK_HIRES_DIAL_RADIAL_CONTROLLER_REPORT_ID,
    .type = 0x01,
};
static uint8_t ctrl_point;
static size_t input_attr_offset;

static ssize_t read_info(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
                         uint16_t len, uint16_t offset) {
    return bt_gatt_attr_read(conn, attr, buf, len, offset, &info, sizeof(info));
}

static ssize_t read_map(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
                        uint16_t len, uint16_t offset) {
    return bt_gatt_attr_read(conn, attr, buf, len, offset,
                             zmk_hires_dial_radial_controller_report_desc,
                             zmk_hires_dial_radial_controller_report_desc_size);
}

static ssize_t read_report_ref(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
                               uint16_t len, uint16_t offset) {
    return bt_gatt_attr_read(conn, attr, buf, len, offset, &input_ref, sizeof(input_ref));
}

static ssize_t read_input(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
                          uint16_t len, uint16_t offset) {
    const struct zmk_hires_dial_radial_controller_report_body *body =
        &zmk_hires_dial_radial_controller_get_report()->body;
    return bt_gatt_attr_read(conn, attr, buf, len, offset, body, sizeof(*body));
}

static ssize_t write_ctrl_point(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                const void *buf, uint16_t len, uint16_t offset, uint8_t flags) {
    if (offset + len > sizeof(ctrl_point)) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }
    memcpy((uint8_t *)&ctrl_point + offset, buf, len);
    return len;
}

BT_GATT_SERVICE_DEFINE(radial_hog, BT_GATT_PRIMARY_SERVICE(BT_UUID_HIDS),
                       BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_INFO, BT_GATT_CHRC_READ,
                                              BT_GATT_PERM_READ, read_info, NULL, NULL),
                       BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_REPORT_MAP, BT_GATT_CHRC_READ,
                                              BT_GATT_PERM_READ_ENCRYPT, read_map, NULL, NULL),
                       BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_REPORT,
                                              BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
                                              BT_GATT_PERM_READ_ENCRYPT, read_input, NULL, NULL),
                       BT_GATT_CCC(NULL, BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT),
                       BT_GATT_DESCRIPTOR(BT_UUID_HIDS_REPORT_REF, BT_GATT_PERM_READ_ENCRYPT,
                                          read_report_ref, NULL, NULL),
                       BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_CTRL_POINT,
                                              BT_GATT_CHRC_WRITE_WITHOUT_RESP, BT_GATT_PERM_WRITE,
                                              NULL, write_ctrl_point, &ctrl_point));

K_MSGQ_DEFINE(radial_msgq, sizeof(struct zmk_hires_dial_radial_controller_report_body),
              CONFIG_ZMK_HIRES_DIAL_RADIAL_CONTROLLER_BLE_QUEUE_SIZE, 4);

static struct bt_conn *destination_connection(void) {
    const bt_addr_le_t *addr = zmk_ble_active_profile_addr();
    if (!addr || !bt_addr_le_cmp(addr, BT_ADDR_LE_ANY)) {
        return NULL;
    }
    return bt_conn_lookup_addr_le(BT_ID_DEFAULT, addr);
}

static void send_work_handler(struct k_work *work) {
    struct zmk_hires_dial_radial_controller_report_body body;
    while (k_msgq_get(&radial_msgq, &body, K_NO_WAIT) == 0) {
        struct bt_conn *conn = destination_connection();
        if (!conn) {
            continue;
        }

        struct bt_gatt_notify_params params = {
            .attr = &radial_hog.attrs[input_attr_offset],
            .data = &body,
            .len = sizeof(body),
        };
        int err = bt_gatt_notify_cb(conn, &params);
        if (err == -EPERM) {
            bt_conn_set_security(conn, BT_SECURITY_L2);
        }
        bt_conn_unref(conn);
    }
}

K_WORK_DEFINE(send_work, send_work_handler);

int zmk_hires_dial_radial_controller_hog_send(
    const struct zmk_hires_dial_radial_controller_report_body *body) {
    int err = k_msgq_put(&radial_msgq, body, K_NO_WAIT);
    if (err == -ENOMSG || err == -EAGAIN) {
        struct zmk_hires_dial_radial_controller_report_body discarded;
        k_msgq_get(&radial_msgq, &discarded, K_NO_WAIT);
        err = k_msgq_put(&radial_msgq, body, K_NO_WAIT);
    }
    if (err < 0) {
        return err;
    }
    k_work_submit(&send_work);
    return 0;
}

static int init(void) {
    for (size_t i = 0; i < radial_hog.attr_count; i++) {
        if (radial_hog.attrs[i].read == read_input) {
            input_attr_offset = i - 1;
            return 0;
        }
    }
    return -ENODEV;
}

SYS_INIT(init, APPLICATION, CONFIG_ZMK_BLE_INIT_PRIORITY);
