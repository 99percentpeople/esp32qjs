#include "esp32_mquickjs_websocket_client_resources.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum {
    FIXTURE_STOP_ERROR = 501,
    FIXTURE_UNREGISTER_ERROR = 502,
    FIXTURE_QUIESCE_ERROR = 503,
    FIXTURE_DESTROY_ERROR = 504,
};

typedef struct {
    size_t stop_failures_remaining;
    size_t unregister_failures_remaining;
    size_t quiesce_failures_remaining;
    size_t destroy_failures_remaining;
    size_t stop_calls;
    size_t unregister_calls;
    size_t quiesce_calls;
    size_t destroy_calls;
    char log[16];
    size_t log_length;
} fixture_t;

static int fixture_stop(void *client, void *opaque)
{
    fixture_t *fixture = opaque;

    assert(client == (void *)(uintptr_t)0x51U);
    fixture->log[fixture->log_length++] = 'S';
    fixture->stop_calls++;
    if (fixture->stop_failures_remaining > 0) {
        fixture->stop_failures_remaining--;
        return FIXTURE_STOP_ERROR;
    }
    return 0;
}

static int fixture_unregister(void *client, void *opaque)
{
    fixture_t *fixture = opaque;

    assert(client == (void *)(uintptr_t)0x51U);
    fixture->log[fixture->log_length++] = 'U';
    fixture->unregister_calls++;
    if (fixture->unregister_failures_remaining > 0) {
        fixture->unregister_failures_remaining--;
        return FIXTURE_UNREGISTER_ERROR;
    }
    return 0;
}

static int fixture_destroy(void *client, void *opaque)
{
    fixture_t *fixture = opaque;

    assert(client == (void *)(uintptr_t)0x51U);
    fixture->log[fixture->log_length++] = 'D';
    fixture->destroy_calls++;
    if (fixture->destroy_failures_remaining > 0) {
        fixture->destroy_failures_remaining--;
        return FIXTURE_DESTROY_ERROR;
    }
    return 0;
}

static int fixture_quiesce(void *client, void *opaque)
{
    fixture_t *fixture = opaque;

    assert(client == (void *)(uintptr_t)0x51U);
    fixture->log[fixture->log_length++] = 'Q';
    fixture->quiesce_calls++;
    if (fixture->quiesce_failures_remaining > 0) {
        fixture->quiesce_failures_remaining--;
        return FIXTURE_QUIESCE_ERROR;
    }
    return 0;
}

static esp32_mquickjs_websocket_client_resource_ops_t fixture_ops(
    fixture_t *fixture)
{
    return (esp32_mquickjs_websocket_client_resource_ops_t){
        .stop = fixture_stop,
        .unregister_events = fixture_unregister,
        .quiesce = fixture_quiesce,
        .destroy = fixture_destroy,
        .opaque = fixture,
    };
}

static esp32_mquickjs_websocket_client_resources_t active_resources(void)
{
    return (esp32_mquickjs_websocket_client_resources_t){
        .client = (void *)(uintptr_t)0x51U,
        .started = true,
        .events_registered = true,
    };
}

static void test_stop_failure_retains_full_suffix(void)
{
    fixture_t fixture = {.stop_failures_remaining = 1};
    esp32_mquickjs_websocket_client_resources_t resources = active_resources();
    esp32_mquickjs_websocket_client_resource_ops_t ops = fixture_ops(&fixture);

    assert(esp32_mquickjs_websocket_client_resources_deinit(
               &resources, &ops) == FIXTURE_STOP_ERROR);
    assert(strcmp(fixture.log, "S") == 0);
    assert(resources.client != NULL);
    assert(resources.started);
    assert(resources.events_registered);
    assert(fixture.unregister_calls == 0);
    assert(fixture.destroy_calls == 0);

    assert(esp32_mquickjs_websocket_client_resources_deinit(
               &resources, &ops) == 0);
    assert(strcmp(fixture.log, "SSUQD") == 0);
    assert(resources.client == NULL);
    assert(!resources.started);
    assert(!resources.events_registered);
}

static void test_unregister_failure_retains_registered_suffix(void)
{
    fixture_t fixture = {.unregister_failures_remaining = 1};
    esp32_mquickjs_websocket_client_resources_t resources = active_resources();
    esp32_mquickjs_websocket_client_resource_ops_t ops = fixture_ops(&fixture);

    assert(esp32_mquickjs_websocket_client_resources_deinit(
               &resources, &ops) == FIXTURE_UNREGISTER_ERROR);
    assert(strcmp(fixture.log, "SU") == 0);
    assert(resources.client != NULL);
    assert(!resources.started);
    assert(resources.events_registered);
    assert(fixture.destroy_calls == 0);

    assert(esp32_mquickjs_websocket_client_resources_deinit(
               &resources, &ops) == 0);
    assert(strcmp(fixture.log, "SUUQD") == 0);
    assert(fixture.stop_calls == 1);
    assert(resources.client == NULL);
}

static void test_destroy_failure_retains_only_client_suffix(void)
{
    fixture_t fixture = {.destroy_failures_remaining = 1};
    esp32_mquickjs_websocket_client_resources_t resources = active_resources();
    esp32_mquickjs_websocket_client_resource_ops_t ops = fixture_ops(&fixture);

    assert(esp32_mquickjs_websocket_client_resources_deinit(
               &resources, &ops) == FIXTURE_DESTROY_ERROR);
    assert(strcmp(fixture.log, "SUQD") == 0);
    assert(resources.client != NULL);
    assert(!resources.started);
    assert(!resources.events_registered);

    assert(esp32_mquickjs_websocket_client_resources_deinit(
               &resources, &ops) == 0);
    assert(strcmp(fixture.log, "SUQDQD") == 0);
    assert(fixture.stop_calls == 1);
    assert(fixture.unregister_calls == 1);
    assert(resources.client == NULL);
}

static void test_quiesce_failure_blocks_destroy_until_retry(void)
{
    fixture_t fixture = {.quiesce_failures_remaining = 1};
    esp32_mquickjs_websocket_client_resources_t resources = active_resources();
    esp32_mquickjs_websocket_client_resource_ops_t ops = fixture_ops(&fixture);

    assert(esp32_mquickjs_websocket_client_resources_deinit(
               &resources, &ops) == FIXTURE_QUIESCE_ERROR);
    assert(strcmp(fixture.log, "SUQ") == 0);
    assert(resources.client != NULL);
    assert(!resources.started);
    assert(!resources.events_registered);
    assert(fixture.destroy_calls == 0);

    assert(esp32_mquickjs_websocket_client_resources_deinit(
               &resources, &ops) == 0);
    assert(strcmp(fixture.log, "SUQQD") == 0);
    assert(resources.client == NULL);
}

int main(void)
{
    test_stop_failure_retains_full_suffix();
    test_unregister_failure_retains_registered_suffix();
    test_destroy_failure_retains_only_client_suffix();
    test_quiesce_failure_blocks_destroy_until_retry();
    return 0;
}
