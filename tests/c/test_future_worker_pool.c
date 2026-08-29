#include "esp32_mquickjs_future_worker_pool.h"

#include <assert.h>
#include <stddef.h>

typedef struct {
    size_t worker_calls;
    size_t worker_order[4];
    size_t queue_calls;
} cleanup_probe_t;

static void probe_worker(size_t worker_index, void *opaque)
{
    cleanup_probe_t *probe = opaque;

    probe->worker_order[probe->worker_calls++] = worker_index;
}

static void probe_queue(void *opaque)
{
    cleanup_probe_t *probe = opaque;

    probe->queue_calls++;
}

static void test_partial_initialization_rolls_back_exactly_once(void)
{
    cleanup_probe_t probe = {0};

    esp32_mquickjs_future_worker_pool_cleanup_partial(
        3, probe_worker, probe_queue, &probe);
    assert(probe.worker_calls == 3);
    assert(probe.worker_order[0] == 2);
    assert(probe.worker_order[1] == 1);
    assert(probe.worker_order[2] == 0);
    assert(probe.queue_calls == 1);
}

static void test_queue_is_released_when_first_worker_fails(void)
{
    cleanup_probe_t probe = {0};

    esp32_mquickjs_future_worker_pool_cleanup_partial(
        0, probe_worker, probe_queue, &probe);
    assert(probe.worker_calls == 0);
    assert(probe.queue_calls == 1);
}

static void test_missing_cleanup_callbacks_are_safe(void)
{
    esp32_mquickjs_future_worker_pool_cleanup_partial(4, NULL, NULL, NULL);
}

int main(void)
{
    test_partial_initialization_rolls_back_exactly_once();
    test_queue_is_released_when_first_worker_fails();
    test_missing_cleanup_callbacks_are_safe();
    return 0;
}
