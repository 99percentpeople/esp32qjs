#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool started;
    bool completed;
    bool cancelled;
    bool stdout_locked;
} esp32_mquickjs_usb_serial_future_lifecycle_t;

enum {
    ESP32_MQUICKJS_USB_SERIAL_RELEASE_SEND_LANE = 1U << 0,
    ESP32_MQUICKJS_USB_SERIAL_RELEASE_STDOUT = 1U << 1,
};

bool esp32_mquickjs_usb_serial_future_lifecycle_start(
    esp32_mquickjs_usb_serial_future_lifecycle_t *lifecycle);

void esp32_mquickjs_usb_serial_future_lifecycle_lock_stdout(
    esp32_mquickjs_usb_serial_future_lifecycle_t *lifecycle);

void esp32_mquickjs_usb_serial_future_lifecycle_complete(
    esp32_mquickjs_usb_serial_future_lifecycle_t *lifecycle);

bool esp32_mquickjs_usb_serial_future_lifecycle_cancel(
    esp32_mquickjs_usb_serial_future_lifecycle_t *lifecycle);

bool esp32_mquickjs_usb_serial_future_lifecycle_ready(
    const esp32_mquickjs_usb_serial_future_lifecycle_t *lifecycle);

bool esp32_mquickjs_usb_serial_future_lifecycle_cancelled(
    const esp32_mquickjs_usb_serial_future_lifecycle_t *lifecycle);

uint32_t esp32_mquickjs_usb_serial_future_lifecycle_take_release(
    esp32_mquickjs_usb_serial_future_lifecycle_t *lifecycle);
