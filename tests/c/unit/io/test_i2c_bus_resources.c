#include "esp32_mquickjs_i2c_bus_resources.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum {
    FIXTURE_CREATE_ERROR = 401,
    FIXTURE_DELETE_ERROR = 402,
};

typedef struct {
    bool fail_lease;
    bool fail_create;
    bool omit_bus;
    bool fail_delete;
    char log[12];
    size_t log_length;
} fixture_t;

static bool fixture_acquire_lease(void *opaque)
{
    fixture_t *fixture = opaque;

    fixture->log[fixture->log_length++] = 'A';
    return !fixture->fail_lease;
}

static void fixture_release_lease(void *opaque)
{
    fixture_t *fixture = opaque;

    fixture->log[fixture->log_length++] = 'R';
}

static int fixture_create_bus(void *opaque, void **out_bus)
{
    fixture_t *fixture = opaque;

    fixture->log[fixture->log_length++] = 'C';
    if (fixture->fail_create) {
        return FIXTURE_CREATE_ERROR;
    }
    if (!fixture->omit_bus) {
        *out_bus = (void *)(uintptr_t)0x41U;
    }
    return 0;
}

static int fixture_delete_bus(void *bus, void *opaque)
{
    fixture_t *fixture = opaque;

    assert(bus == (void *)(uintptr_t)0x41U);
    fixture->log[fixture->log_length++] = 'D';
    return fixture->fail_delete ? FIXTURE_DELETE_ERROR : 0;
}

static esp32_mquickjs_i2c_bus_resource_ops_t fixture_ops(fixture_t *fixture)
{
    return (esp32_mquickjs_i2c_bus_resource_ops_t){
        .acquire_lease = fixture_acquire_lease,
        .release_lease = fixture_release_lease,
        .create_bus = fixture_create_bus,
        .delete_bus = fixture_delete_bus,
        .opaque = fixture,
    };
}

static void test_lease_failure_stops_before_bus_creation(void)
{
    fixture_t fixture = {.fail_lease = true};
    esp32_mquickjs_i2c_bus_resources_t resources = {0};
    esp32_mquickjs_i2c_bus_resource_ops_t ops = fixture_ops(&fixture);

    assert(esp32_mquickjs_i2c_bus_resources_init(&resources, &ops) ==
           ESP32_MQUICKJS_I2C_BUS_RESOURCE_UNAVAILABLE);
    assert(strcmp(fixture.log, "A") == 0);
    assert(!resources.lease_acquired);
    assert(resources.bus == NULL);
}

static void test_bus_create_failure_releases_lease(void)
{
    fixture_t fixture = {.fail_create = true};
    esp32_mquickjs_i2c_bus_resources_t resources = {0};
    esp32_mquickjs_i2c_bus_resource_ops_t ops = fixture_ops(&fixture);

    assert(esp32_mquickjs_i2c_bus_resources_init(&resources, &ops) ==
           FIXTURE_CREATE_ERROR);
    assert(strcmp(fixture.log, "ACR") == 0);
    assert(!resources.lease_acquired);
    assert(resources.bus == NULL);
}

static void test_missing_created_bus_is_rejected_and_releases_lease(void)
{
    fixture_t fixture = {.omit_bus = true};
    esp32_mquickjs_i2c_bus_resources_t resources = {0};
    esp32_mquickjs_i2c_bus_resource_ops_t ops = fixture_ops(&fixture);

    assert(esp32_mquickjs_i2c_bus_resources_init(&resources, &ops) != 0);
    assert(strcmp(fixture.log, "ACR") == 0);
    assert(!resources.lease_acquired);
    assert(resources.bus == NULL);
}

static void test_success_deletes_bus_then_releases_lease_once(void)
{
    fixture_t fixture = {0};
    esp32_mquickjs_i2c_bus_resources_t resources = {0};
    esp32_mquickjs_i2c_bus_resource_ops_t ops = fixture_ops(&fixture);

    assert(esp32_mquickjs_i2c_bus_resources_init(&resources, &ops) == 0);
    assert(resources.lease_acquired);
    assert(resources.bus == (void *)(uintptr_t)0x41U);
    assert(esp32_mquickjs_i2c_bus_resources_deinit(&resources, &ops) == 0);
    assert(strcmp(fixture.log, "ACDR") == 0);
    assert(!resources.lease_acquired);
    assert(resources.bus == NULL);
    assert(esp32_mquickjs_i2c_bus_resources_deinit(&resources, &ops) == 0);
    assert(strcmp(fixture.log, "ACDR") == 0);
}

static void test_delete_failure_retains_bus_and_lease_for_retry(void)
{
    fixture_t fixture = {.fail_delete = true};
    esp32_mquickjs_i2c_bus_resources_t resources = {0};
    esp32_mquickjs_i2c_bus_resource_ops_t ops = fixture_ops(&fixture);

    assert(esp32_mquickjs_i2c_bus_resources_init(&resources, &ops) == 0);
    assert(esp32_mquickjs_i2c_bus_resources_deinit(&resources, &ops) ==
           FIXTURE_DELETE_ERROR);
    assert(strcmp(fixture.log, "ACD") == 0);
    assert(resources.lease_acquired);
    assert(resources.bus == (void *)(uintptr_t)0x41U);

    fixture.fail_delete = false;
    assert(esp32_mquickjs_i2c_bus_resources_deinit(&resources, &ops) == 0);
    assert(strcmp(fixture.log, "ACDDR") == 0);
    assert(!resources.lease_acquired);
    assert(resources.bus == NULL);
}

int main(void)
{
    test_lease_failure_stops_before_bus_creation();
    test_bus_create_failure_releases_lease();
    test_missing_created_bus_is_rejected_and_releases_lease();
    test_success_deletes_bus_then_releases_lease_once();
    test_delete_failure_retains_bus_and_lease_for_retry();
    return 0;
}
