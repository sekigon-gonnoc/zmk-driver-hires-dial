#define DT_DRV_COMPAT sekigon_remote_hires_dial

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/init.h>

static int remote_hires_dial_sample_fetch(const struct device *dev, enum sensor_channel chan) {
    ARG_UNUSED(dev);
    ARG_UNUSED(chan);
    return -ENOTSUP;
}

static int remote_hires_dial_channel_get(const struct device *dev, enum sensor_channel chan,
                                         struct sensor_value *value) {
    ARG_UNUSED(dev);
    ARG_UNUSED(chan);
    ARG_UNUSED(value);
    return -ENOTSUP;
}

static int remote_hires_dial_trigger_set(const struct device *dev,
                                         const struct sensor_trigger *trigger,
                                         sensor_trigger_handler_t handler) {
    ARG_UNUSED(dev);
    ARG_UNUSED(trigger);
    ARG_UNUSED(handler);
    return 0;
}

static const struct sensor_driver_api remote_hires_dial_api = {
    .sample_fetch = remote_hires_dial_sample_fetch,
    .channel_get = remote_hires_dial_channel_get,
    .trigger_set = remote_hires_dial_trigger_set,
};

#define REMOTE_HIRES_DIAL_INIT(n)                                                               \
    DEVICE_DT_INST_DEFINE(n, NULL, NULL, NULL, NULL, POST_KERNEL, CONFIG_SENSOR_INIT_PRIORITY, \
                          &remote_hires_dial_api);

DT_INST_FOREACH_STATUS_OKAY(REMOTE_HIRES_DIAL_INIT)
