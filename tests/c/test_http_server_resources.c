#include "esp32_mquickjs_http_server_resources.h"

#include <assert.h>
#include <stddef.h>

enum {
    TEST_OK = 0,
    TEST_ERR_STOP = 83,
};

typedef struct {
    int stop_result;
    int stop_calls;
    void *last_handle;
} fake_http_server_t;

static int fake_stop(void *handle, void *opaque)
{
    fake_http_server_t *fake = opaque;

    fake->stop_calls++;
    fake->last_handle = handle;
    return fake->stop_result;
}

static esp32_mquickjs_http_server_resource_ops_t fake_ops(
    fake_http_server_t *fake)
{
    return (esp32_mquickjs_http_server_resource_ops_t){
        .stop = fake_stop,
        .opaque = fake,
    };
}

static void test_invalid_arguments_are_rejected(void)
{
    int native_server = 1;
    fake_http_server_t fake = {0};
    esp32_mquickjs_http_server_resource_ops_t ops = fake_ops(&fake);
    esp32_mquickjs_http_server_resources_t resources = {
        .handle = &native_server,
    };

    assert(esp32_mquickjs_http_server_resources_stop(NULL, &ops) != TEST_OK);
    assert(esp32_mquickjs_http_server_resources_stop(&resources, NULL) != TEST_OK);
    ops.stop = NULL;
    assert(esp32_mquickjs_http_server_resources_stop(&resources, &ops) != TEST_OK);
    assert(resources.handle == &native_server);
    assert(fake.stop_calls == 0);
}

static void test_empty_resource_is_idempotent(void)
{
    fake_http_server_t fake = {0};
    esp32_mquickjs_http_server_resource_ops_t ops = fake_ops(&fake);
    esp32_mquickjs_http_server_resources_t resources = {0};

    assert(esp32_mquickjs_http_server_resources_stop(&resources, &ops) == TEST_OK);
    assert(resources.handle == NULL);
    assert(fake.stop_calls == 0);
}

static void test_success_clears_handle_exactly_once(void)
{
    int native_server = 1;
    fake_http_server_t fake = {0};
    esp32_mquickjs_http_server_resource_ops_t ops = fake_ops(&fake);
    esp32_mquickjs_http_server_resources_t resources = {
        .handle = &native_server,
    };

    assert(esp32_mquickjs_http_server_resources_stop(&resources, &ops) == TEST_OK);
    assert(resources.handle == NULL);
    assert(fake.stop_calls == 1);
    assert(fake.last_handle == &native_server);
    assert(esp32_mquickjs_http_server_resources_stop(&resources, &ops) == TEST_OK);
    assert(fake.stop_calls == 1);
}

static void test_failure_retains_handle_for_retry(void)
{
    int native_server = 1;
    fake_http_server_t fake = {
        .stop_result = TEST_ERR_STOP,
    };
    esp32_mquickjs_http_server_resource_ops_t ops = fake_ops(&fake);
    esp32_mquickjs_http_server_resources_t resources = {
        .handle = &native_server,
    };

    assert(esp32_mquickjs_http_server_resources_stop(&resources, &ops) ==
           TEST_ERR_STOP);
    assert(resources.handle == &native_server);
    assert(fake.stop_calls == 1);

    fake.stop_result = TEST_OK;
    assert(esp32_mquickjs_http_server_resources_stop(&resources, &ops) == TEST_OK);
    assert(resources.handle == NULL);
    assert(fake.stop_calls == 2);
}

int main(void)
{
    test_invalid_arguments_are_rejected();
    test_empty_resource_is_idempotent();
    test_success_clears_handle_exactly_once();
    test_failure_retains_handle_for_retry();
    return 0;
}
