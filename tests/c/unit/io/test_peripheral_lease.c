#include "esp32_mquickjs_peripheral_lease.h"

#include <stdio.h>
#include <stdlib.h>

static void expect_true(bool condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        exit(1);
    }
}

static void test_duplicate_and_generation_checked_release(void)
{
    esp32_mquickjs_peripheral_lease_t first = {0};
    esp32_mquickjs_peripheral_lease_t duplicate = {0};
    esp32_mquickjs_peripheral_lease_t stale;
    esp32_mquickjs_peripheral_lease_t replacement = {0};

    esp32_mquickjs_peripheral_leases_reset();
    expect_true(esp32_mquickjs_peripheral_lease_acquire(
                    ESP32_MQUICKJS_PERIPHERAL_I2S_PORT, 0,
                    ESP32_MQUICKJS_PERIPHERAL_OWNER_I2S, &first),
                "first I2S port lease should succeed");
    expect_true(esp32_mquickjs_peripheral_lease_is_held(&first),
                "first lease should be live");
    expect_true(!esp32_mquickjs_peripheral_lease_acquire(
                    ESP32_MQUICKJS_PERIPHERAL_I2S_PORT, 0,
                    ESP32_MQUICKJS_PERIPHERAL_OWNER_I2S, &duplicate),
                "duplicate I2S port lease should fail");

    stale = first;
    esp32_mquickjs_peripheral_lease_release(&first);
    expect_true(!esp32_mquickjs_peripheral_lease_is_held(&first),
                "released lease should be cleared");
    expect_true(esp32_mquickjs_peripheral_lease_acquire(
                    ESP32_MQUICKJS_PERIPHERAL_I2S_PORT, 0,
                    ESP32_MQUICKJS_PERIPHERAL_OWNER_I2S, &replacement),
                "released port should reopen immediately");
    expect_true(replacement.generation != stale.generation,
                "reopened lease should have a fresh generation");

    esp32_mquickjs_peripheral_lease_release(&stale);
    expect_true(esp32_mquickjs_peripheral_lease_is_held(&replacement),
                "stale release must not release the replacement");
    esp32_mquickjs_peripheral_lease_release(&replacement);
}

static void test_kinds_are_independent_and_acquire_any_is_bounded(void)
{
    esp32_mquickjs_peripheral_lease_t camera = {0};
    esp32_mquickjs_peripheral_lease_t i2c = {0};
    esp32_mquickjs_peripheral_lease_t timers[3] = {{0}};
    esp32_mquickjs_peripheral_lease_t exhausted = {0};
    int i;

    esp32_mquickjs_peripheral_leases_reset();
    expect_true(esp32_mquickjs_peripheral_lease_acquire(
                    ESP32_MQUICKJS_PERIPHERAL_CAMERA, 0,
                    ESP32_MQUICKJS_PERIPHERAL_OWNER_CAMERA, &camera),
                "camera singleton lease should succeed");
    expect_true(esp32_mquickjs_peripheral_lease_acquire(
                    ESP32_MQUICKJS_PERIPHERAL_I2C_PORT, 0,
                    ESP32_MQUICKJS_PERIPHERAL_OWNER_I2C, &i2c),
                "same index in a different kind should remain independent");

    for (i = 0; i < 3; ++i) {
        expect_true(esp32_mquickjs_peripheral_lease_acquire_any(
                        ESP32_MQUICKJS_PERIPHERAL_LEDC_TIMER, 3,
                        ESP32_MQUICKJS_PERIPHERAL_OWNER_CAMERA, &timers[i]),
                    "acquire_any should select an unused resource");
        expect_true(timers[i].index == i,
                    "acquire_any should select the lowest free index");
    }
    expect_true(!esp32_mquickjs_peripheral_lease_acquire_any(
                    ESP32_MQUICKJS_PERIPHERAL_LEDC_TIMER, 3,
                    ESP32_MQUICKJS_PERIPHERAL_OWNER_LEDC, &exhausted),
                "acquire_any should fail when its bounded range is exhausted");

    for (i = 0; i < 3; ++i) {
        esp32_mquickjs_peripheral_lease_release(&timers[i]);
    }
    esp32_mquickjs_peripheral_lease_release(&i2c);
    esp32_mquickjs_peripheral_lease_release(&camera);
}

static void test_invalid_requests_fail_without_mutation(void)
{
    esp32_mquickjs_peripheral_lease_t lease = {0};

    esp32_mquickjs_peripheral_leases_reset();
    expect_true(!esp32_mquickjs_peripheral_lease_acquire(
                    ESP32_MQUICKJS_PERIPHERAL_CAMERA, -1,
                    ESP32_MQUICKJS_PERIPHERAL_OWNER_CAMERA, &lease),
                "negative index should fail");
    expect_true(!esp32_mquickjs_peripheral_lease_acquire(
                    ESP32_MQUICKJS_PERIPHERAL_CAMERA, 0,
                    ESP32_MQUICKJS_PERIPHERAL_OWNER_NONE, &lease),
                "owner NONE should fail");
    expect_true(!esp32_mquickjs_peripheral_lease_acquire_any(
                    ESP32_MQUICKJS_PERIPHERAL_LEDC_CHANNEL, 0,
                    ESP32_MQUICKJS_PERIPHERAL_OWNER_LEDC, &lease),
                "empty acquire_any range should fail");
}

int main(void)
{
    test_duplicate_and_generation_checked_release();
    test_kinds_are_independent_and_acquire_any_is_bounded();
    test_invalid_requests_fail_without_mutation();
    return 0;
}
