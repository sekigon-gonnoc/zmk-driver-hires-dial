/*
 * Copyright 2024 Google LLC
 * Modifications Copyright 2026 sekigon-gonnoc
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT sekigon_hires_dial

#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/pm/device.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(zmk_hires_dial, CONFIG_ZMK_HIRES_DIAL_LOG_LEVEL);

#define PAT912X_PRODUCT_ID1 0x00
#define PAT912X_MOTION_STATUS 0x02
#define PAT912X_DELTA_X_LO 0x03
#define PAT912X_OPERATION_MODE 0x05
#define PAT912X_CONFIGURATION 0x06
#define PAT912X_RES_X 0x0d
#define PAT912X_RES_Y 0x0e
#define PAT912X_DELTA_XY_HI 0x12

#define PRODUCT_ID_PAT9125EL 0x3191
#define CONFIGURATION_CLEAR 0x17
#define CONFIGURATION_PD_ENH BIT(3)
#define MOTION_STATUS_MOTION BIT(7)
#define OPERATION_MODE_SLEEP_1_EN BIT(4)
#define OPERATION_MODE_SLEEP_12_EN (BIT(4) | BIT(3))
#define RES_SCALING_FACTOR 5
#define RES_MAX (UINT8_MAX * RES_SCALING_FACTOR)
#define RESET_DELAY_MS 2

struct pat912x_hires_dial_config {
    struct i2c_dt_spec i2c;
    struct gpio_dt_spec motion_gpio;
    struct gpio_dt_spec power_gpio;
    uint16_t res_cpi;
    uint16_t motion_read_interval_ms;
    uint32_t counts_per_revolution;
    bool invert;
    bool sleep1_enable;
    bool sleep2_enable;
};

struct pat912x_hires_dial_data {
    const struct device *dev;
    struct k_work_delayable motion_work;
    struct gpio_callback motion_cb;
    struct k_spinlock lock;
    int32_t pending_counts;
    int32_t sample_counts;
    int64_t cumulative_counts;
    sensor_trigger_handler_t trigger_handler;
    const struct sensor_trigger *trigger;
};

static inline int32_t sign_extend_12(uint32_t value) { return (int32_t)(value << 20) >> 20; }

static int pat912x_set_resolution(const struct device *dev) {
    const struct pat912x_hires_dial_config *cfg = dev->config;
    // Disable x motion by setting x resolution to 0, and set y resolution to the configured value.
    int err = i2c_reg_write_byte_dt(&cfg->i2c, PAT912X_RES_X, 0);
    if (err < 0) {
        return err;
    }

    return i2c_reg_write_byte_dt(&cfg->i2c, PAT912X_RES_Y, cfg->res_cpi / RES_SCALING_FACTOR);
}

static int pat912x_configure(const struct device *dev) {
    const struct pat912x_hires_dial_config *cfg = dev->config;
    uint8_t id[2];
    int err = i2c_burst_read_dt(&cfg->i2c, PAT912X_PRODUCT_ID1, id, sizeof(id));
    if (err < 0) {
        return err;
    }

    if (sys_get_be16(id) != PRODUCT_ID_PAT9125EL) {
        LOG_ERR("Invalid product id: %04x", sys_get_be16(id));
        return -ENOTSUP;
    }

    k_sleep(K_MSEC(RESET_DELAY_MS));
    err = i2c_reg_write_byte_dt(&cfg->i2c, PAT912X_CONFIGURATION, CONFIGURATION_CLEAR);
    if (err < 0) {
        return err;
    }

    err = pat912x_set_resolution(dev);
    if (err < 0) {
        return err;
    }

    uint8_t sleep_bits = 0;
    if (cfg->sleep1_enable) {
        sleep_bits |= OPERATION_MODE_SLEEP_1_EN;
    }
    if (cfg->sleep2_enable) {
        sleep_bits |= OPERATION_MODE_SLEEP_12_EN;
    }

    return i2c_reg_update_byte_dt(&cfg->i2c, PAT912X_OPERATION_MODE, OPERATION_MODE_SLEEP_12_EN,
                                  sleep_bits);
}

static int pat912x_read_motion(const struct device *dev, int32_t *y) {
    const struct pat912x_hires_dial_config *cfg = dev->config;
    uint8_t status;
    int err = i2c_reg_read_byte_dt(&cfg->i2c, PAT912X_MOTION_STATUS, &status);
    if (err < 0) {
        return err;
    }
    if (!(status & MOTION_STATUS_MOTION)) {
        LOG_DBG("Motion status inactive: 0x%02x", status);
        return 0;
    }

    uint8_t xy[2];
    err = i2c_burst_read_dt(&cfg->i2c, PAT912X_DELTA_X_LO, xy, sizeof(xy));
    if (err < 0) {
        return err;
    }

    uint8_t high;
    err = i2c_reg_read_byte_dt(&cfg->i2c, PAT912X_DELTA_XY_HI, &high);
    if (err < 0) {
        return err;
    }

    int32_t value = sign_extend_12(xy[1] | ((high << 8) & 0xf00));
    *y = cfg->invert ? -value : value;
    LOG_DBG("Motion status=0x%02x delta_y=%d", status, *y);
    return 1;
}

static int pat912x_interrupt_configure(const struct device *dev, gpio_flags_t flags) {
    const struct pat912x_hires_dial_config *cfg = dev->config;

    return gpio_pin_interrupt_configure_dt(&cfg->motion_gpio, flags);
}

static int pat912x_interrupt_enable(const struct device *dev) {
    return pat912x_interrupt_configure(dev, GPIO_INT_LEVEL_LOW);
}

static int pat912x_interrupt_disable(const struct device *dev) {
    return pat912x_interrupt_configure(dev, GPIO_INT_DISABLE);
}

static void pat912x_motion_work_handler(struct k_work *work) {
    struct pat912x_hires_dial_data *data =
        CONTAINER_OF(work, struct pat912x_hires_dial_data, motion_work.work);
    const struct device *dev = data->dev;
    const struct pat912x_hires_dial_config *cfg = dev->config;

    int motion_active = gpio_pin_get_dt(&cfg->motion_gpio);
    if (motion_active < 0) {
        LOG_ERR("Motion GPIO read failed: %d", motion_active);
        k_work_reschedule(&data->motion_work, K_MSEC(cfg->motion_read_interval_ms));
        return;
    }
    if (motion_active == 0) {
        LOG_DBG("MOTION pin inactive; enabling level interrupt");
        int err = pat912x_interrupt_enable(dev);
        if (err < 0) {
            LOG_ERR("Motion interrupt enable failed: %d", err);
        }
        return;
    }

    int32_t y = 0;
    int err = pat912x_read_motion(dev, &y);
    if (err < 0) {
        LOG_ERR("Motion read failed: %d", err);
    }

    sensor_trigger_handler_t handler = NULL;
    const struct sensor_trigger *trigger = NULL;
    int64_t cumulative_counts = 0;

    k_spinlock_key_t key = k_spin_lock(&data->lock);
    if (err > 0) {
        data->pending_counts += y;
        data->cumulative_counts += y;
        cumulative_counts = data->cumulative_counts;
    }
    if (data->pending_counts != 0 && data->trigger_handler) {
        handler = data->trigger_handler;
        trigger = data->trigger;
    }
    k_spin_unlock(&data->lock, key);

    if (err > 0) {
        LOG_INF("DIAL_COUNT delta=%d cumulative=%lld", y, (long long)cumulative_counts);
    }

    if (handler) {
        LOG_DBG("Notifying ZMK sensor handler; pending_counts=%d", data->pending_counts);
        handler(dev, trigger);
    }

    /* Keep the level interrupt disabled while waiting. On the next run, read again only if
     * MOTION is still physically low; otherwise re-enable the level interrupt. */
    k_work_reschedule(&data->motion_work, K_MSEC(cfg->motion_read_interval_ms));
}

static void pat912x_motion_gpio_handler(const struct device *gpio_dev, struct gpio_callback *cb,
                                        uint32_t pins) {
    struct pat912x_hires_dial_data *data =
        CONTAINER_OF(cb, struct pat912x_hires_dial_data, motion_cb);

    ARG_UNUSED(gpio_dev);
    ARG_UNUSED(pins);

    LOG_DBG("MOTION level interrupt");

    int err = pat912x_interrupt_disable(data->dev);
    if (err < 0) {
        LOG_ERR("Motion interrupt disable failed: %d", err);
        return;
    }
    k_work_reschedule(&data->motion_work, K_NO_WAIT);
}

static int pat912x_sample_fetch(const struct device *dev, enum sensor_channel chan) {
    struct pat912x_hires_dial_data *data = dev->data;
    if (chan != SENSOR_CHAN_ALL && chan != SENSOR_CHAN_ROTATION) {
        return -ENOTSUP;
    }

    k_spinlock_key_t key = k_spin_lock(&data->lock);
    data->sample_counts = data->pending_counts;
    data->pending_counts = 0;
    k_spin_unlock(&data->lock, key);
    LOG_DBG("Sample fetched: counts=%d", data->sample_counts);
    return 0;
}

static int pat912x_channel_get(const struct device *dev, enum sensor_channel chan,
                               struct sensor_value *value) {
    const struct pat912x_hires_dial_config *cfg = dev->config;
    struct pat912x_hires_dial_data *data = dev->data;
    if (chan != SENSOR_CHAN_ROTATION) {
        return -ENOTSUP;
    }

    int64_t microdegrees =
        ((int64_t)data->sample_counts * 360000000LL) / cfg->counts_per_revolution;
    value->val1 = microdegrees / 1000000LL;
    value->val2 = microdegrees % 1000000LL;
    LOG_DBG("Channel rotation: counts=%d value=(%d, %d)", data->sample_counts, value->val1,
            value->val2);
    return 0;
}

static int pat912x_trigger_set(const struct device *dev, const struct sensor_trigger *trigger,
                               sensor_trigger_handler_t handler) {
    struct pat912x_hires_dial_data *data = dev->data;
    if (trigger->type != SENSOR_TRIG_DATA_READY || trigger->chan != SENSOR_CHAN_ROTATION) {
        return -ENOTSUP;
    }

    k_spinlock_key_t key = k_spin_lock(&data->lock);
    data->trigger = trigger;
    data->trigger_handler = handler;
    k_spin_unlock(&data->lock, key);
    LOG_INF("ZMK sensor trigger %s", handler ? "registered" : "cleared");
    return 0;
}

static const struct sensor_driver_api pat912x_api = {
    .sample_fetch = pat912x_sample_fetch,
    .channel_get = pat912x_channel_get,
    .trigger_set = pat912x_trigger_set,
};

static int pat912x_init(const struct device *dev) {
    const struct pat912x_hires_dial_config *cfg = dev->config;
    struct pat912x_hires_dial_data *data = dev->data;
    int err;

    LOG_INF("Initializing PAT912x hires dial on %s", cfg->i2c.bus->name);

    if (cfg->power_gpio.port) {
        if (!gpio_is_ready_dt(&cfg->power_gpio)) {
            return -ENODEV;
        }
        err = gpio_pin_configure_dt(&cfg->power_gpio, GPIO_OUTPUT_INACTIVE);
        if (err < 0) {
            return err;
        }
        k_sleep(K_MSEC(10));
        gpio_pin_set_dt(&cfg->power_gpio, 1);
        k_sleep(K_MSEC(500));
    }

    if (!i2c_is_ready_dt(&cfg->i2c) || !gpio_is_ready_dt(&cfg->motion_gpio)) {
        return -ENODEV;
    }

    data->dev = dev;
    k_work_init_delayable(&data->motion_work, pat912x_motion_work_handler);

    err = gpio_pin_configure_dt(&cfg->motion_gpio, GPIO_INPUT);
    if (err < 0) {
        return err;
    }
    gpio_init_callback(&data->motion_cb, pat912x_motion_gpio_handler, BIT(cfg->motion_gpio.pin));

    err = gpio_add_callback_dt(&cfg->motion_gpio, &data->motion_cb);
    if (err < 0) {
        return err;
    }

    err = pat912x_configure(dev);
    if (err < 0) {
        return err;
    }

    err = pat912x_interrupt_enable(dev);
    if (err < 0) {
        return err;
    }
    LOG_INF("PAT912x hires dial ready; MOTION pin=%u interval=%u ms", cfg->motion_gpio.pin,
            cfg->motion_read_interval_ms);
    return 0;
}

#ifdef CONFIG_PM_DEVICE
static int pat912x_pm_action(const struct device *dev, enum pm_device_action action) {
    const struct pat912x_hires_dial_config *cfg = dev->config;
    uint8_t value;
    switch (action) {
    case PM_DEVICE_ACTION_SUSPEND:
        value = CONFIGURATION_PD_ENH;
        break;
    case PM_DEVICE_ACTION_RESUME:
        value = 0;
        break;
    default:
        return -ENOTSUP;
    }
    return i2c_reg_update_byte_dt(&cfg->i2c, PAT912X_CONFIGURATION, CONFIGURATION_PD_ENH, value);
}
#endif

#define PAT912X_HIRES_DIAL_INIT(n)                                                                 \
    BUILD_ASSERT(DT_INST_PROP(n, counts_per_revolution) > 0,                                       \
                 "counts-per-revolution must be positive");                                        \
    BUILD_ASSERT(DT_INST_PROP(n, motion_read_interval_ms) > 0,                                     \
                 "motion-read-interval-ms must be positive");                                      \
    BUILD_ASSERT(DT_INST_PROP(n, res_cpi) <= RES_MAX, "invalid res-cpi");                          \
    static const struct pat912x_hires_dial_config pat912x_config_##n = {                           \
        .i2c = I2C_DT_SPEC_INST_GET(n),                                                            \
        .motion_gpio = GPIO_DT_SPEC_INST_GET(n, motion_gpios),                                     \
        .power_gpio = GPIO_DT_SPEC_INST_GET_OR(n, power_gpios, {0}),                               \
        .res_cpi = DT_INST_PROP(n, res_cpi),                                                       \
        .counts_per_revolution = DT_INST_PROP(n, counts_per_revolution),                           \
        .motion_read_interval_ms = DT_INST_PROP(n, motion_read_interval_ms),                       \
        .invert = DT_INST_PROP(n, invert),                                                         \
        .sleep1_enable = DT_INST_PROP(n, sleep1_enable),                                           \
        .sleep2_enable = DT_INST_PROP(n, sleep2_enable),                                           \
    };                                                                                             \
    static struct pat912x_hires_dial_data pat912x_data_##n;                                        \
    PM_DEVICE_DT_INST_DEFINE(n, pat912x_pm_action);                                                \
    DEVICE_DT_INST_DEFINE(n, pat912x_init, PM_DEVICE_DT_INST_GET(n), &pat912x_data_##n,            \
                          &pat912x_config_##n, POST_KERNEL, CONFIG_SENSOR_INIT_PRIORITY,           \
                          &pat912x_api);

DT_INST_FOREACH_STATUS_OKAY(PAT912X_HIRES_DIAL_INIT)
