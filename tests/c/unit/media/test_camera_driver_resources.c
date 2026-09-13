#include "esp32_mquickjs_camera_driver_resources.h"

#include <assert.h>
#include <stddef.h>

enum {
    TEST_OK = 0,
    TEST_ERR_DEINIT = 71,
};

typedef struct {
    int deinit_result;
    int deinit_calls;
} fake_camera_driver_t;

static int fake_deinit(void *opaque)
{
    fake_camera_driver_t *fake = opaque;

    fake->deinit_calls++;
    return fake->deinit_result;
}

static esp32_mquickjs_camera_driver_resource_ops_t fake_ops(
    fake_camera_driver_t *fake)
{
    return (esp32_mquickjs_camera_driver_resource_ops_t){
        .deinit = fake_deinit,
        .opaque = fake,
    };
}

static void test_invalid_arguments_are_rejected(void)
{
    fake_camera_driver_t fake = {0};
    esp32_mquickjs_camera_driver_resource_ops_t ops = fake_ops(&fake);
    esp32_mquickjs_camera_driver_resources_t resources = {
        .initialized = true,
    };

    assert(esp32_mquickjs_camera_driver_resources_deinit(NULL, &ops) !=
           TEST_OK);
    assert(esp32_mquickjs_camera_driver_resources_deinit(&resources, NULL) !=
           TEST_OK);
    ops.deinit = NULL;
    assert(esp32_mquickjs_camera_driver_resources_deinit(&resources, &ops) !=
           TEST_OK);
    assert(resources.initialized);
    assert(fake.deinit_calls == 0);
}

static void test_uninitialized_resource_is_idempotent(void)
{
    fake_camera_driver_t fake = {0};
    esp32_mquickjs_camera_driver_resource_ops_t ops = fake_ops(&fake);
    esp32_mquickjs_camera_driver_resources_t resources = {0};

    assert(esp32_mquickjs_camera_driver_resources_deinit(&resources, &ops) ==
           TEST_OK);
    assert(!resources.initialized);
    assert(fake.deinit_calls == 0);
}

static void test_success_clears_resource_exactly_once(void)
{
    fake_camera_driver_t fake = {0};
    esp32_mquickjs_camera_driver_resource_ops_t ops = fake_ops(&fake);
    esp32_mquickjs_camera_driver_resources_t resources = {
        .initialized = true,
    };

    assert(esp32_mquickjs_camera_driver_resources_deinit(&resources, &ops) ==
           TEST_OK);
    assert(!resources.initialized);
    assert(fake.deinit_calls == 1);
    assert(esp32_mquickjs_camera_driver_resources_deinit(&resources, &ops) ==
           TEST_OK);
    assert(fake.deinit_calls == 1);
}

static void test_failure_retains_resource_for_retry(void)
{
    fake_camera_driver_t fake = {
        .deinit_result = TEST_ERR_DEINIT,
    };
    esp32_mquickjs_camera_driver_resource_ops_t ops = fake_ops(&fake);
    esp32_mquickjs_camera_driver_resources_t resources = {
        .initialized = true,
    };

    assert(esp32_mquickjs_camera_driver_resources_deinit(&resources, &ops) ==
           TEST_ERR_DEINIT);
    assert(resources.initialized);
    assert(fake.deinit_calls == 1);

    fake.deinit_result = TEST_OK;
    assert(esp32_mquickjs_camera_driver_resources_deinit(&resources, &ops) ==
           TEST_OK);
    assert(!resources.initialized);
    assert(fake.deinit_calls == 2);
}

int main(void)
{
    test_invalid_arguments_are_rejected();
    test_uninitialized_resource_is_idempotent();
    test_success_clears_resource_exactly_once();
    test_failure_retains_resource_for_retry();
    return 0;
}
