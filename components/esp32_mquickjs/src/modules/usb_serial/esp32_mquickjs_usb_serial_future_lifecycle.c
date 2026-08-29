#include "esp32_mquickjs_usb_serial_future_lifecycle.h"

#include <stddef.h>

bool esp32_mquickjs_usb_serial_future_lifecycle_start(
    esp32_mquickjs_usb_serial_future_lifecycle_t *lifecycle)
{
    if (lifecycle == NULL || lifecycle->started || lifecycle->completed ||
        lifecycle->cancelled) {
        return false;
    }
    lifecycle->started = true;
    return true;
}

void esp32_mquickjs_usb_serial_future_lifecycle_lock_stdout(
    esp32_mquickjs_usb_serial_future_lifecycle_t *lifecycle)
{
    if (lifecycle != NULL && lifecycle->started &&
        !lifecycle->completed) {
        lifecycle->stdout_locked = true;
    }
}

void esp32_mquickjs_usb_serial_future_lifecycle_complete(
    esp32_mquickjs_usb_serial_future_lifecycle_t *lifecycle)
{
    if (lifecycle != NULL) {
        lifecycle->completed = true;
    }
}

bool esp32_mquickjs_usb_serial_future_lifecycle_cancel(
    esp32_mquickjs_usb_serial_future_lifecycle_t *lifecycle)
{
    if (lifecycle == NULL || lifecycle->completed || lifecycle->cancelled) {
        return false;
    }
    lifecycle->cancelled = true;
    lifecycle->completed = true;
    return true;
}

bool esp32_mquickjs_usb_serial_future_lifecycle_ready(
    const esp32_mquickjs_usb_serial_future_lifecycle_t *lifecycle)
{
    return lifecycle != NULL && lifecycle->completed;
}

bool esp32_mquickjs_usb_serial_future_lifecycle_cancelled(
    const esp32_mquickjs_usb_serial_future_lifecycle_t *lifecycle)
{
    return lifecycle != NULL && lifecycle->cancelled;
}

uint32_t esp32_mquickjs_usb_serial_future_lifecycle_take_release(
    esp32_mquickjs_usb_serial_future_lifecycle_t *lifecycle)
{
    uint32_t release = 0;

    if (lifecycle == NULL) {
        return 0;
    }
    if (lifecycle->started) {
        release |= ESP32_MQUICKJS_USB_SERIAL_RELEASE_SEND_LANE;
        lifecycle->started = false;
    }
    if (lifecycle->stdout_locked) {
        release |= ESP32_MQUICKJS_USB_SERIAL_RELEASE_STDOUT;
        lifecycle->stdout_locked = false;
    }
    return release;
}
