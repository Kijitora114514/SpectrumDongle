/*
 * Copyright (c) 2026 Kijitora114514
 * SPDX-License-Identifier: MIT
 */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>

#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/keymap.h>

LOG_MODULE_REGISTER(spectrum_dongle_uart,
                    CONFIG_SPECTRUM_DONGLE_UART_LOG_LEVEL);

#define UART_NODE DT_CHOSEN(spectrum_dongle_uart)
#if !defined(CONFIG_SPECTRUM_DONGLE_UART_SELF_TEST)
#define BATTERY_SLOT_COUNT 5
#define BATTERY_UNKNOWN (-1)
#define TX_BUFFER_SIZE 192
#endif

#if defined(CONFIG_SPECTRUM_DONGLE_UART_SELF_TEST)
static const uint8_t self_test_snapshot[] =
    "SD1\t0\t0\t99\t99\t-1\t-1\t-1\tUART TEST\n";
#endif

BUILD_ASSERT(DT_HAS_CHOSEN(spectrum_dongle_uart),
             "spectrum,dongle-uart chosen node is required");
BUILD_ASSERT(DT_NODE_HAS_STATUS(UART_NODE, okay),
             "Spectrum Dongle UART device must be enabled");

static const struct device *const uart_device = DEVICE_DT_GET(UART_NODE);
#if !defined(CONFIG_SPECTRUM_DONGLE_UART_SELF_TEST)
static atomic_t battery_levels[BATTERY_SLOT_COUNT];
static uint8_t tx_buffer[TX_BUFFER_SIZE];
static uint32_t tx_sequence;
#endif

static void tx_work_handler(struct k_work *work);
static void heartbeat_work_handler(struct k_work *work);

K_WORK_DELAYABLE_DEFINE(tx_work, tx_work_handler);
K_WORK_DELAYABLE_DEFINE(heartbeat_work, heartbeat_work_handler);

static void schedule_snapshot(k_timeout_t delay) {
    (void)k_work_reschedule(&tx_work, delay);
}

#if !defined(CONFIG_SPECTRUM_DONGLE_UART_SELF_TEST)
static size_t append_escaped_layer_name(uint8_t *destination, size_t capacity,
                                        const char *source) {
    size_t output = 0;
    size_t input = 0;

    while (source[input] != '\0' &&
           input < CONFIG_SPECTRUM_DONGLE_UART_LAYER_NAME_MAX_LEN) {
        const char current = source[input++];
        const char *escape = NULL;

        switch (current) {
        case '\\':
            escape = "\\\\";
            break;
        case '\t':
            escape = "\\t";
            break;
        case '\r':
            escape = "\\r";
            break;
        case '\n':
            escape = "\\n";
            break;
        default:
            break;
        }

        if (escape != NULL) {
            if (output + 2U >= capacity) {
                break;
            }
            destination[output++] = (uint8_t)escape[0];
            destination[output++] = (uint8_t)escape[1];
        } else {
            if (output + 1U >= capacity) {
                break;
            }
            destination[output++] = (uint8_t)current;
        }
    }

    return output;
}

static int build_snapshot(void) {
    const zmk_keymap_layer_index_t layer_index =
        zmk_keymap_highest_layer_active();
    const zmk_keymap_layer_id_t layer_id =
        zmk_keymap_layer_index_to_id(layer_index);
    const char *layer_name = zmk_keymap_layer_name(layer_id);
    char fallback_name[12];
    int battery[BATTERY_SLOT_COUNT];

    if (layer_name == NULL || layer_name[0] == '\0') {
        (void)snprintf(fallback_name, sizeof(fallback_name), "%u",
                       (unsigned int)layer_index);
        layer_name = fallback_name;
    }

    for (size_t i = 0; i < ARRAY_SIZE(battery); ++i) {
        battery[i] = (int)atomic_get(&battery_levels[i]);
    }

    int prefix_length = snprintf(
        (char *)tx_buffer, sizeof(tx_buffer),
        "SD1\t%u\t%u\t%d\t%d\t%d\t%d\t%d\t",
        (unsigned int)tx_sequence, (unsigned int)layer_index, battery[0],
        battery[1], battery[2], battery[3], battery[4]);
    if (prefix_length < 0 || (size_t)prefix_length >= sizeof(tx_buffer) - 1U) {
        return -ENOSPC;
    }

    size_t length = (size_t)prefix_length;
    length += append_escaped_layer_name(
        &tx_buffer[length], sizeof(tx_buffer) - length - 1U, layer_name);
    if (length + 1U > sizeof(tx_buffer)) {
        return -ENOSPC;
    }
    tx_buffer[length++] = '\n';
    return (int)length;
}
#endif

static void tx_work_handler(struct k_work *work) {
    ARG_UNUSED(work);

#if defined(CONFIG_SPECTRUM_DONGLE_UART_SELF_TEST)
    for (size_t i = 0; i < sizeof(self_test_snapshot) - 1U; ++i) {
        uart_poll_out(uart_device, self_test_snapshot[i]);
    }
    return;
#else
    const int length = build_snapshot();
    if (length < 0) {
        LOG_ERR("Failed to build UART snapshot (%d)", length);
        return;
    }

    for (int i = 0; i < length; ++i) {
        uart_poll_out(uart_device, tx_buffer[i]);
    }
    ++tx_sequence;
#endif
}

static void heartbeat_work_handler(struct k_work *work) {
    ARG_UNUSED(work);

    schedule_snapshot(K_NO_WAIT);
    (void)k_work_reschedule(
        &heartbeat_work,
        K_MSEC(CONFIG_SPECTRUM_DONGLE_UART_HEARTBEAT_MS));
}

#if !defined(CONFIG_SPECTRUM_DONGLE_UART_SELF_TEST)
static int layer_state_listener(const zmk_event_t *event) {
    ARG_UNUSED(event);
    schedule_snapshot(K_NO_WAIT);
    return ZMK_EV_EVENT_BUBBLE;
}

static int peripheral_battery_listener(const zmk_event_t *event) {
    const struct zmk_peripheral_battery_state_changed *battery_event =
        as_zmk_peripheral_battery_state_changed(event);

    if (battery_event != NULL &&
        battery_event->source < BATTERY_SLOT_COUNT) {
        atomic_set(&battery_levels[battery_event->source],
                   MIN((int)battery_event->state_of_charge, 100));
        schedule_snapshot(K_NO_WAIT);
    }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(spectrum_dongle_uart_layer, layer_state_listener);
ZMK_SUBSCRIPTION(spectrum_dongle_uart_layer, zmk_layer_state_changed);

ZMK_LISTENER(spectrum_dongle_uart_battery, peripheral_battery_listener);
ZMK_SUBSCRIPTION(spectrum_dongle_uart_battery,
                 zmk_peripheral_battery_state_changed);
#endif

static int spectrum_dongle_uart_init(void) {
    if (!device_is_ready(uart_device)) {
        LOG_ERR("UART device is not ready");
        return -ENODEV;
    }

#if !defined(CONFIG_SPECTRUM_DONGLE_UART_SELF_TEST)
    for (size_t i = 0; i < ARRAY_SIZE(battery_levels); ++i) {
        atomic_set(&battery_levels[i], BATTERY_UNKNOWN);
    }
#endif

    schedule_snapshot(
        K_MSEC(CONFIG_SPECTRUM_DONGLE_UART_START_DELAY_MS));
    (void)k_work_reschedule(
        &heartbeat_work,
        K_MSEC(CONFIG_SPECTRUM_DONGLE_UART_START_DELAY_MS +
               CONFIG_SPECTRUM_DONGLE_UART_HEARTBEAT_MS));

    LOG_INF("Status UART ready on XIAO D6/P1.11%s",
            IS_ENABLED(CONFIG_SPECTRUM_DONGLE_UART_SELF_TEST)
                ? " (self-test)"
                : "");
    return 0;
}

SYS_INIT(spectrum_dongle_uart_init, APPLICATION,
         CONFIG_APPLICATION_INIT_PRIORITY);

