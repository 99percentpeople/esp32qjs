#include "esp32_mquickjs_uart_port_resources.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum {
    FIXTURE_LOCK_ERROR = 201,
    FIXTURE_DRIVER_ERROR = 202,
    FIXTURE_DELETE_DRIVER_ERROR = 203,
};

typedef struct {
    bool fail_lock;
    bool fail_driver;
    bool omit_driver_events;
    size_t delete_driver_failures_remaining;
    size_t create_lock_calls;
    size_t install_driver_calls;
    size_t delete_driver_calls;
    size_t delete_lock_calls;
    char log[8];
    size_t log_length;
} fixture_t;

static int fixture_create_lock(void *opaque, void **out_lock)
{
    fixture_t *fixture = opaque;

    fixture->log[fixture->log_length++] = 'L';
    fixture->create_lock_calls++;
    if (fixture->fail_lock) {
        return FIXTURE_LOCK_ERROR;
    }
    *out_lock = (void *)(uintptr_t)0x11U;
    return 0;
}

static int fixture_install_driver(void *opaque, void **out_driver_events)
{
    fixture_t *fixture = opaque;

    fixture->log[fixture->log_length++] = 'I';
    fixture->install_driver_calls++;
    if (fixture->fail_driver) {
        return FIXTURE_DRIVER_ERROR;
    }
    if (!fixture->omit_driver_events) {
        *out_driver_events = (void *)(uintptr_t)0x22U;
    }
    return 0;
}

static int fixture_delete_driver(void *opaque)
{
    fixture_t *fixture = opaque;

    fixture->log[fixture->log_length++] = 'D';
    fixture->delete_driver_calls++;
    if (fixture->delete_driver_failures_remaining > 0) {
        fixture->delete_driver_failures_remaining--;
        return FIXTURE_DELETE_DRIVER_ERROR;
    }
    return 0;
}

static void fixture_delete_lock(void *lock, void *opaque)
{
    fixture_t *fixture = opaque;

    assert(lock == (void *)(uintptr_t)0x11U);
    fixture->log[fixture->log_length++] = 'l';
    fixture->delete_lock_calls++;
}

static esp32_mquickjs_uart_port_resource_ops_t fixture_ops(
    fixture_t *fixture)
{
    return (esp32_mquickjs_uart_port_resource_ops_t){
        .create_watch_lock = fixture_create_lock,
        .install_driver = fixture_install_driver,
        .delete_driver = fixture_delete_driver,
        .delete_watch_lock = fixture_delete_lock,
        .opaque = fixture,
    };
}

static void test_lock_failure_stops_before_driver_install(void)
{
    fixture_t fixture = {.fail_lock = true};
    esp32_mquickjs_uart_port_resources_t resources = {0};
    esp32_mquickjs_uart_port_resource_ops_t ops = fixture_ops(&fixture);

    assert(esp32_mquickjs_uart_port_resources_init(&resources, &ops) ==
           FIXTURE_LOCK_ERROR);
    assert(fixture.create_lock_calls == 1);
    assert(fixture.install_driver_calls == 0);
    assert(fixture.delete_driver_calls == 0);
    assert(fixture.delete_lock_calls == 0);
    assert(resources.watch_lock == NULL);
    assert(resources.driver_events == NULL);
    assert(!resources.driver_installed);
}

static void test_driver_failure_deletes_the_lock(void)
{
    fixture_t fixture = {.fail_driver = true};
    esp32_mquickjs_uart_port_resources_t resources = {0};
    esp32_mquickjs_uart_port_resource_ops_t ops = fixture_ops(&fixture);

    assert(esp32_mquickjs_uart_port_resources_init(&resources, &ops) ==
           FIXTURE_DRIVER_ERROR);
    assert(fixture.create_lock_calls == 1);
    assert(fixture.install_driver_calls == 1);
    assert(fixture.delete_driver_calls == 0);
    assert(fixture.delete_lock_calls == 1);
    assert(strcmp(fixture.log, "LIl") == 0);
    assert(resources.watch_lock == NULL);
    assert(resources.driver_events == NULL);
    assert(!resources.driver_installed);
}

static void test_missing_event_queue_rolls_back_the_installed_driver(void)
{
    fixture_t fixture = {.omit_driver_events = true};
    esp32_mquickjs_uart_port_resources_t resources = {0};
    esp32_mquickjs_uart_port_resource_ops_t ops = fixture_ops(&fixture);

    assert(esp32_mquickjs_uart_port_resources_init(&resources, &ops) != 0);
    assert(fixture.delete_driver_calls == 1);
    assert(fixture.delete_lock_calls == 1);
    assert(strcmp(fixture.log, "LIDl") == 0);
    assert(resources.watch_lock == NULL);
    assert(resources.driver_events == NULL);
    assert(!resources.driver_installed);
}

static void test_success_deinit_is_reverse_and_idempotent(void)
{
    fixture_t fixture = {0};
    esp32_mquickjs_uart_port_resources_t resources = {0};
    esp32_mquickjs_uart_port_resource_ops_t ops = fixture_ops(&fixture);

    assert(esp32_mquickjs_uart_port_resources_init(&resources, &ops) == 0);
    assert(resources.watch_lock == (void *)(uintptr_t)0x11U);
    assert(resources.driver_events == (void *)(uintptr_t)0x22U);
    assert(resources.driver_installed);
    assert(esp32_mquickjs_uart_port_resources_deinit(&resources, &ops) == 0);
    assert(strcmp(fixture.log, "LIDl") == 0);
    assert(fixture.delete_driver_calls == 1);
    assert(fixture.delete_lock_calls == 1);
    assert(resources.watch_lock == NULL);
    assert(resources.driver_events == NULL);
    assert(!resources.driver_installed);
    assert(esp32_mquickjs_uart_port_resources_deinit(&resources, &ops) == 0);
    assert(fixture.delete_driver_calls == 1);
    assert(fixture.delete_lock_calls == 1);
}

static void test_delete_driver_failure_retains_exact_suffix_for_retry(void)
{
    fixture_t fixture = {.delete_driver_failures_remaining = 1};
    esp32_mquickjs_uart_port_resources_t resources = {0};
    esp32_mquickjs_uart_port_resource_ops_t ops = fixture_ops(&fixture);

    assert(esp32_mquickjs_uart_port_resources_init(&resources, &ops) == 0);
    assert(esp32_mquickjs_uart_port_resources_deinit(&resources, &ops) ==
           FIXTURE_DELETE_DRIVER_ERROR);
    assert(strcmp(fixture.log, "LID") == 0);
    assert(fixture.delete_driver_calls == 1);
    assert(fixture.delete_lock_calls == 0);
    assert(resources.watch_lock == (void *)(uintptr_t)0x11U);
    assert(resources.driver_events == (void *)(uintptr_t)0x22U);
    assert(resources.driver_installed);

    assert(esp32_mquickjs_uart_port_resources_deinit(&resources, &ops) == 0);
    assert(strcmp(fixture.log, "LIDDl") == 0);
    assert(fixture.delete_driver_calls == 2);
    assert(fixture.delete_lock_calls == 1);
    assert(resources.watch_lock == NULL);
    assert(resources.driver_events == NULL);
    assert(!resources.driver_installed);
}

int main(void)
{
    test_lock_failure_stops_before_driver_install();
    test_driver_failure_deletes_the_lock();
    test_missing_event_queue_rolls_back_the_installed_driver();
    test_success_deinit_is_reverse_and_idempotent();
    test_delete_driver_failure_retains_exact_suffix_for_retry();
    return 0;
}
