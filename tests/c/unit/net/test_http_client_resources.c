#include "esp32_mquickjs_http_client_resources.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

enum {
    FIXTURE_CLEANUP_ERROR = 401,
};

typedef struct {
    size_t failures_remaining;
    size_t cleanup_calls;
} fixture_t;

static int fixture_cleanup(void *client, void *opaque)
{
    fixture_t *fixture = opaque;

    assert(client == (void *)(uintptr_t)0x41U);
    fixture->cleanup_calls++;
    if (fixture->failures_remaining > 0) {
        fixture->failures_remaining--;
        return FIXTURE_CLEANUP_ERROR;
    }
    return 0;
}

static void test_cleanup_failure_retains_client_for_retry(void)
{
    fixture_t fixture = {.failures_remaining = 1};
    esp32_mquickjs_http_client_resources_t resources = {
        .client = (void *)(uintptr_t)0x41U,
    };
    esp32_mquickjs_http_client_resource_ops_t ops = {
        .cleanup = fixture_cleanup,
        .opaque = &fixture,
    };

    assert(esp32_mquickjs_http_client_resources_deinit(&resources, &ops) ==
           FIXTURE_CLEANUP_ERROR);
    assert(resources.client == (void *)(uintptr_t)0x41U);
    assert(fixture.cleanup_calls == 1);

    assert(esp32_mquickjs_http_client_resources_deinit(&resources, &ops) == 0);
    assert(resources.client == NULL);
    assert(fixture.cleanup_calls == 2);
    assert(esp32_mquickjs_http_client_resources_deinit(&resources, &ops) == 0);
    assert(fixture.cleanup_calls == 2);
}

int main(void)
{
    test_cleanup_failure_retains_client_for_retry();
    return 0;
}
