#include "esp32_mquickjs_ble_runtime_resources.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

enum {
    FIXTURE_STOP_ERROR = 301,
    FIXTURE_DEINIT_ERROR = 302,
};

typedef struct {
    size_t stop_failures_remaining;
    size_t deinit_failures_remaining;
    size_t stop_calls;
    size_t deinit_calls;
    char log[8];
    size_t log_length;
} fixture_t;

static int fixture_stop(void *opaque)
{
    fixture_t *fixture = opaque;

    fixture->log[fixture->log_length++] = 'S';
    fixture->stop_calls++;
    if (fixture->stop_failures_remaining > 0) {
        fixture->stop_failures_remaining--;
        return FIXTURE_STOP_ERROR;
    }
    return 0;
}

static int fixture_deinit(void *opaque)
{
    fixture_t *fixture = opaque;

    fixture->log[fixture->log_length++] = 'D';
    fixture->deinit_calls++;
    if (fixture->deinit_failures_remaining > 0) {
        fixture->deinit_failures_remaining--;
        return FIXTURE_DEINIT_ERROR;
    }
    return 0;
}

static esp32_mquickjs_ble_runtime_resource_ops_t fixture_ops(
    fixture_t *fixture)
{
    return (esp32_mquickjs_ble_runtime_resource_ops_t){
        .stop_host = fixture_stop,
        .deinit_port = fixture_deinit,
        .opaque = fixture,
    };
}

static void test_stop_failure_retains_host_and_port_for_retry(void)
{
    fixture_t fixture = {.stop_failures_remaining = 1};
    esp32_mquickjs_ble_runtime_resources_t resources = {
        .port_initialized = true,
        .host_started = true,
    };
    esp32_mquickjs_ble_runtime_resource_ops_t ops = fixture_ops(&fixture);

    assert(esp32_mquickjs_ble_runtime_resources_deinit(&resources, &ops) ==
           FIXTURE_STOP_ERROR);
    assert(strcmp(fixture.log, "S") == 0);
    assert(fixture.stop_calls == 1);
    assert(fixture.deinit_calls == 0);
    assert(resources.host_started);
    assert(resources.port_initialized);

    assert(esp32_mquickjs_ble_runtime_resources_deinit(&resources, &ops) == 0);
    assert(strcmp(fixture.log, "SSD") == 0);
    assert(fixture.stop_calls == 2);
    assert(fixture.deinit_calls == 1);
    assert(!resources.host_started);
    assert(!resources.port_initialized);
}

static void test_deinit_failure_retains_only_initialized_suffix(void)
{
    fixture_t fixture = {.deinit_failures_remaining = 1};
    esp32_mquickjs_ble_runtime_resources_t resources = {
        .port_initialized = true,
        .host_started = true,
    };
    esp32_mquickjs_ble_runtime_resource_ops_t ops = fixture_ops(&fixture);

    assert(esp32_mquickjs_ble_runtime_resources_deinit(&resources, &ops) ==
           FIXTURE_DEINIT_ERROR);
    assert(strcmp(fixture.log, "SD") == 0);
    assert(!resources.host_started);
    assert(resources.port_initialized);

    assert(esp32_mquickjs_ble_runtime_resources_deinit(&resources, &ops) == 0);
    assert(strcmp(fixture.log, "SDD") == 0);
    assert(fixture.stop_calls == 1);
    assert(fixture.deinit_calls == 2);
    assert(!resources.host_started);
    assert(!resources.port_initialized);
}

static void test_initialized_without_started_host_only_deinitializes(void)
{
    fixture_t fixture = {0};
    esp32_mquickjs_ble_runtime_resources_t resources = {
        .port_initialized = true,
    };
    esp32_mquickjs_ble_runtime_resource_ops_t ops = fixture_ops(&fixture);

    assert(esp32_mquickjs_ble_runtime_resources_deinit(&resources, &ops) == 0);
    assert(strcmp(fixture.log, "D") == 0);
    assert(fixture.stop_calls == 0);
    assert(fixture.deinit_calls == 1);
    assert(!resources.host_started);
    assert(!resources.port_initialized);
    assert(esp32_mquickjs_ble_runtime_resources_deinit(&resources, &ops) == 0);
    assert(fixture.deinit_calls == 1);
}

int main(void)
{
    test_stop_failure_retains_host_and_port_for_retry();
    test_deinit_failure_retains_only_initialized_suffix();
    test_initialized_without_started_host_only_deinitializes();
    return 0;
}
