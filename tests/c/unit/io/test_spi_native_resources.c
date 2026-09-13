#include "esp32_mquickjs_spi_native_resources.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

enum {
    TEST_OK = 0,
    TEST_ERR_REMOVE = 81,
    TEST_ERR_FREE = 82,
};

typedef struct {
    int remove_result;
    int free_result;
    int remove_calls;
    int free_calls;
    void *last_device;
    int last_host_id;
} fake_spi_t;

static int fake_remove(void *device, void *opaque)
{
    fake_spi_t *fake = opaque;

    fake->remove_calls++;
    fake->last_device = device;
    return fake->remove_result;
}

static int fake_free(int host_id, void *opaque)
{
    fake_spi_t *fake = opaque;

    fake->free_calls++;
    fake->last_host_id = host_id;
    return fake->free_result;
}

static void test_invalid_arguments_are_rejected(void)
{
    fake_spi_t fake = {0};
    esp32_mquickjs_spi_device_resource_ops_t device_ops = {
        .remove = fake_remove,
        .opaque = &fake,
    };
    esp32_mquickjs_spi_bus_resource_ops_t bus_ops = {
        .free = fake_free,
        .opaque = &fake,
    };
    esp32_mquickjs_spi_device_resources_t device = {
        .handle = (void *)(uintptr_t)0x11,
    };
    esp32_mquickjs_spi_bus_resources_t bus = {
        .initialized = true,
        .host_id = 2,
    };

    assert(esp32_mquickjs_spi_device_resources_deinit(NULL, &device_ops) !=
           TEST_OK);
    assert(esp32_mquickjs_spi_device_resources_deinit(&device, NULL) !=
           TEST_OK);
    device_ops.remove = NULL;
    assert(esp32_mquickjs_spi_device_resources_deinit(&device, &device_ops) !=
           TEST_OK);
    assert(esp32_mquickjs_spi_bus_resources_deinit(NULL, &bus_ops) != TEST_OK);
    assert(esp32_mquickjs_spi_bus_resources_deinit(&bus, NULL) != TEST_OK);
    bus_ops.free = NULL;
    assert(esp32_mquickjs_spi_bus_resources_deinit(&bus, &bus_ops) != TEST_OK);
    assert(device.handle != NULL);
    assert(bus.initialized);
    assert(fake.remove_calls == 0);
    assert(fake.free_calls == 0);
}

static void test_empty_resources_are_idempotent(void)
{
    fake_spi_t fake = {0};
    esp32_mquickjs_spi_device_resource_ops_t device_ops = {
        .remove = fake_remove,
        .opaque = &fake,
    };
    esp32_mquickjs_spi_bus_resource_ops_t bus_ops = {
        .free = fake_free,
        .opaque = &fake,
    };
    esp32_mquickjs_spi_device_resources_t device = {0};
    esp32_mquickjs_spi_bus_resources_t bus = {
        .host_id = 2,
    };

    assert(esp32_mquickjs_spi_device_resources_deinit(&device, &device_ops) ==
           TEST_OK);
    assert(esp32_mquickjs_spi_bus_resources_deinit(&bus, &bus_ops) == TEST_OK);
    assert(fake.remove_calls == 0);
    assert(fake.free_calls == 0);
}

static void test_device_remove_failure_retains_handle_for_retry(void)
{
    fake_spi_t fake = {
        .remove_result = TEST_ERR_REMOVE,
    };
    esp32_mquickjs_spi_device_resource_ops_t ops = {
        .remove = fake_remove,
        .opaque = &fake,
    };
    esp32_mquickjs_spi_device_resources_t resources = {
        .handle = (void *)(uintptr_t)0x21,
    };

    assert(esp32_mquickjs_spi_device_resources_deinit(&resources, &ops) ==
           TEST_ERR_REMOVE);
    assert(resources.handle == (void *)(uintptr_t)0x21);
    assert(fake.last_device == resources.handle);
    fake.remove_result = TEST_OK;
    assert(esp32_mquickjs_spi_device_resources_deinit(&resources, &ops) ==
           TEST_OK);
    assert(resources.handle == NULL);
    assert(fake.remove_calls == 2);
    assert(esp32_mquickjs_spi_device_resources_deinit(&resources, &ops) ==
           TEST_OK);
    assert(fake.remove_calls == 2);
}

static void test_bus_free_failure_retains_initialized_state_for_retry(void)
{
    fake_spi_t fake = {
        .free_result = TEST_ERR_FREE,
    };
    esp32_mquickjs_spi_bus_resource_ops_t ops = {
        .free = fake_free,
        .opaque = &fake,
    };
    esp32_mquickjs_spi_bus_resources_t resources = {
        .initialized = true,
        .host_id = 3,
    };

    assert(esp32_mquickjs_spi_bus_resources_deinit(&resources, &ops) ==
           TEST_ERR_FREE);
    assert(resources.initialized);
    assert(fake.last_host_id == 3);
    fake.free_result = TEST_OK;
    assert(esp32_mquickjs_spi_bus_resources_deinit(&resources, &ops) ==
           TEST_OK);
    assert(!resources.initialized);
    assert(fake.free_calls == 2);
    assert(esp32_mquickjs_spi_bus_resources_deinit(&resources, &ops) ==
           TEST_OK);
    assert(fake.free_calls == 2);
}

int main(void)
{
    test_invalid_arguments_are_rejected();
    test_empty_resources_are_idempotent();
    test_device_remove_failure_retains_handle_for_retry();
    test_bus_free_failure_retains_initialized_state_for_retry();
    return 0;
}
