#include "esp32_mquickjs_usb_serial_future_lifecycle.h"

#include <assert.h>

static void test_pending_runtime_stop_releases_generation_ownership(void)
{
    esp32_mquickjs_usb_serial_future_lifecycle_t lifecycle = {0};
    uint32_t release;

    assert(esp32_mquickjs_usb_serial_future_lifecycle_start(&lifecycle));
    esp32_mquickjs_usb_serial_future_lifecycle_lock_stdout(&lifecycle);
    assert(!esp32_mquickjs_usb_serial_future_lifecycle_ready(&lifecycle));

    assert(esp32_mquickjs_usb_serial_future_lifecycle_cancel(&lifecycle));
    assert(esp32_mquickjs_usb_serial_future_lifecycle_ready(&lifecycle));
    assert(esp32_mquickjs_usb_serial_future_lifecycle_cancelled(&lifecycle));
    assert(!esp32_mquickjs_usb_serial_future_lifecycle_cancel(&lifecycle));

    release = esp32_mquickjs_usb_serial_future_lifecycle_take_release(
        &lifecycle);
    assert((release & ESP32_MQUICKJS_USB_SERIAL_RELEASE_SEND_LANE) != 0);
    assert((release & ESP32_MQUICKJS_USB_SERIAL_RELEASE_STDOUT) != 0);
    assert(esp32_mquickjs_usb_serial_future_lifecycle_take_release(
               &lifecycle) == 0);
}

static void test_new_runtime_can_acquire_after_old_runtime_destroy(void)
{
    esp32_mquickjs_usb_serial_future_lifecycle_t old_generation = {0};
    esp32_mquickjs_usb_serial_future_lifecycle_t new_generation = {0};

    assert(esp32_mquickjs_usb_serial_future_lifecycle_start(
        &old_generation));
    assert(esp32_mquickjs_usb_serial_future_lifecycle_cancel(
        &old_generation));
    assert((esp32_mquickjs_usb_serial_future_lifecycle_take_release(
                &old_generation) &
            ESP32_MQUICKJS_USB_SERIAL_RELEASE_SEND_LANE) != 0);

    assert(esp32_mquickjs_usb_serial_future_lifecycle_start(
        &new_generation));
    esp32_mquickjs_usb_serial_future_lifecycle_complete(&new_generation);
    assert(esp32_mquickjs_usb_serial_future_lifecycle_ready(
        &new_generation));
    assert(!esp32_mquickjs_usb_serial_future_lifecycle_cancelled(
        &new_generation));
    assert((esp32_mquickjs_usb_serial_future_lifecycle_take_release(
                &new_generation) &
            ESP32_MQUICKJS_USB_SERIAL_RELEASE_SEND_LANE) != 0);
}

static void test_failed_start_and_unstarted_destroy_own_nothing(void)
{
    esp32_mquickjs_usb_serial_future_lifecycle_t lifecycle = {0};

    esp32_mquickjs_usb_serial_future_lifecycle_complete(&lifecycle);
    assert(!esp32_mquickjs_usb_serial_future_lifecycle_start(&lifecycle));
    assert(!esp32_mquickjs_usb_serial_future_lifecycle_cancel(&lifecycle));
    assert(esp32_mquickjs_usb_serial_future_lifecycle_take_release(
               &lifecycle) == 0);
}

int main(void)
{
    test_pending_runtime_stop_releases_generation_ownership();
    test_new_runtime_can_acquire_after_old_runtime_destroy();
    test_failed_start_and_unstarted_destroy_own_nothing();
    return 0;
}
