#include "esp32_mquickjs_event_queue_resources.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    size_t id;
} fake_resource_t;

typedef struct {
    fake_resource_t resources[4];
    size_t create_calls;
    size_t fail_at;
    size_t release_calls;
    size_t release_order[4];
} resource_probe_t;

static void *probe_create(resource_probe_t *probe)
{
    size_t call = ++probe->create_calls;

    if (call == probe->fail_at) {
        return NULL;
    }
    assert(call <= 4);
    probe->resources[call - 1].id = call;
    return &probe->resources[call - 1];
}

static void probe_release_resource(void *resource, resource_probe_t *probe)
{
    fake_resource_t *value = resource;

    assert(value != NULL);
    assert(probe->release_calls < 4);
    probe->release_order[probe->release_calls++] = value->id;
}

static void *probe_allocate(size_t bytes, void *opaque)
{
    resource_probe_t *probe = opaque;

    assert(bytes == 8);
    return probe_create(probe);
}

static void probe_release(void *allocation, void *opaque)
{
    probe_release_resource(allocation, opaque);
}

static void *probe_create_lock(void *opaque)
{
    return probe_create(opaque);
}

static void probe_delete_lock(void *lock, void *opaque)
{
    probe_release_resource(lock, opaque);
}

static void *probe_create_queue(uint32_t capacity,
                                size_t event_size,
                                void *opaque)
{
    assert(capacity == 3);
    assert(event_size == 8);
    return probe_create(opaque);
}

static void probe_delete_queue(void *queue, void *opaque)
{
    probe_release_resource(queue, opaque);
}

static const esp32_mquickjs_event_queue_resource_ops_t s_ops = {
    .allocate = probe_allocate,
    .release = probe_release,
    .create_lock = probe_create_lock,
    .delete_lock = probe_delete_lock,
    .create_queue = probe_create_queue,
    .delete_queue = probe_delete_queue,
};

static void assert_empty(
    const esp32_mquickjs_event_queue_resources_t *resources)
{
    assert(resources->drain_scratch == NULL);
    assert(resources->overflow_scratch == NULL);
    assert(resources->send_lock == NULL);
    assert(resources->events == NULL);
}

static void test_drop_oldest_failure_rolls_back_each_creation_stage(void)
{
    size_t fail_at;

    for (fail_at = 1; fail_at <= 4; ++fail_at) {
        esp32_mquickjs_event_queue_resources_t resources;
        resource_probe_t probe = {.fail_at = fail_at};
        size_t released;

        assert(!esp32_mquickjs_event_queue_resources_init(
            &resources, 8, 3, true, &s_ops, &probe));
        assert(probe.create_calls == fail_at);
        assert(probe.release_calls == fail_at - 1);
        for (released = 0; released < probe.release_calls; ++released) {
            assert(probe.release_order[released] == fail_at - released - 1);
        }
        assert_empty(&resources);
    }
}

static void test_drop_new_skips_overflow_buffer_and_releases_reverse_order(void)
{
    esp32_mquickjs_event_queue_resources_t resources;
    resource_probe_t probe = {.fail_at = SIZE_MAX};

    assert(esp32_mquickjs_event_queue_resources_init(
        &resources, 8, 3, false, &s_ops, &probe));
    assert(probe.create_calls == 3);
    assert(resources.overflow_scratch == NULL);
    esp32_mquickjs_event_queue_resources_deinit(
        &resources, &s_ops, &probe);
    assert(probe.release_calls == 3);
    assert(probe.release_order[0] == 3);
    assert(probe.release_order[1] == 2);
    assert(probe.release_order[2] == 1);
    assert_empty(&resources);
}

static void test_drop_oldest_success_releases_every_resource_once(void)
{
    esp32_mquickjs_event_queue_resources_t resources;
    resource_probe_t probe = {.fail_at = SIZE_MAX};

    assert(esp32_mquickjs_event_queue_resources_init(
        &resources, 8, 3, true, &s_ops, &probe));
    assert(probe.create_calls == 4);
    esp32_mquickjs_event_queue_resources_deinit(
        &resources, &s_ops, &probe);
    assert(probe.release_calls == 4);
    assert(probe.release_order[0] == 4);
    assert(probe.release_order[1] == 3);
    assert(probe.release_order[2] == 2);
    assert(probe.release_order[3] == 1);
    assert_empty(&resources);
}

static void test_invalid_configuration_does_not_create_resources(void)
{
    esp32_mquickjs_event_queue_resources_t resources = {0};
    resource_probe_t probe = {.fail_at = SIZE_MAX};

    assert(!esp32_mquickjs_event_queue_resources_init(
        &resources, 0, 3, true, &s_ops, &probe));
    assert(probe.create_calls == 0);
    assert(probe.release_calls == 0);
    assert_empty(&resources);
}

int main(void)
{
    test_drop_oldest_failure_rolls_back_each_creation_stage();
    test_drop_new_skips_overflow_buffer_and_releases_reverse_order();
    test_drop_oldest_success_releases_every_resource_once();
    test_invalid_configuration_does_not_create_resources();
    return 0;
}
