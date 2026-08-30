#include "esp32_mquickjs_event_queue_drain.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char *payload;
} owned_event_t;

typedef struct {
    owned_event_t events[2];
    size_t count;
    size_t next;
    size_t live_payloads;
    size_t drop_calls;
    bool allocations_allowed;
} fake_queue_t;

typedef struct {
    owned_event_t events[2];
    size_t count;
    size_t capacity;
    size_t live_payloads;
    size_t drop_calls;
    size_t send_calls;
    bool fail_retry;
} fake_enqueue_queue_t;

typedef struct {
    int events[2];
    size_t count;
    size_t send_calls;
    bool wake_on_send;
} fake_isr_queue_t;

typedef struct {
    int events[2];
    size_t count;
    size_t send_calls;
} fake_callback_queue_t;

static bool fake_receive(void *source, void *event)
{
    fake_queue_t *queue = source;

    if (queue->next >= queue->count) {
        return false;
    }
    memcpy(event, &queue->events[queue->next], sizeof(owned_event_t));
    queue->next++;
    return true;
}

static void fake_drop(void *event, void *opaque)
{
    owned_event_t *owned = event;
    fake_queue_t *queue = opaque;

    free(owned->payload);
    owned->payload = NULL;
    queue->live_payloads--;
    queue->drop_calls++;
}

static char *fake_allocate_payload(fake_queue_t *queue, const char *text)
{
    size_t length;
    char *payload;

    assert(queue->allocations_allowed);
    length = strlen(text) + 1;
    payload = malloc(length);
    assert(payload != NULL);
    memcpy(payload, text, length);
    queue->live_payloads++;
    return payload;
}

static char *fake_enqueue_allocate_payload(fake_enqueue_queue_t *queue,
                                           const char *text)
{
    size_t length = strlen(text) + 1;
    char *payload = malloc(length);

    assert(payload != NULL);
    memcpy(payload, text, length);
    queue->live_payloads++;
    return payload;
}

static bool fake_enqueue_send(void *destination, const void *event)
{
    fake_enqueue_queue_t *queue = destination;

    queue->send_calls++;
    if (queue->count >= queue->capacity ||
        (queue->fail_retry && queue->send_calls > 1)) {
        return false;
    }
    memcpy(&queue->events[queue->count], event, sizeof(owned_event_t));
    queue->count++;
    return true;
}

static bool fake_enqueue_receive(void *source, void *event)
{
    fake_enqueue_queue_t *queue = source;

    if (queue->count == 0) {
        return false;
    }
    memcpy(event, &queue->events[0], sizeof(owned_event_t));
    queue->count--;
    if (queue->count > 0) {
        memmove(&queue->events[0], &queue->events[1],
                queue->count * sizeof(owned_event_t));
    }
    return true;
}

static void fake_enqueue_drop(void *event, void *opaque)
{
    owned_event_t *owned = event;
    fake_enqueue_queue_t *queue = opaque;

    free(owned->payload);
    owned->payload = NULL;
    queue->live_payloads--;
    queue->drop_calls++;
}

static bool fake_isr_send(void *destination,
                          const void *event,
                          int *task_woken)
{
    fake_isr_queue_t *queue = destination;

    queue->send_calls++;
    if (queue->count >= 2) {
        return false;
    }
    queue->events[queue->count++] = *(const int *)event;
    if (task_woken != NULL && queue->wake_on_send) {
        *task_woken = 1;
    }
    return true;
}

static bool fake_callback_try_send(void *destination, const void *event)
{
    fake_callback_queue_t *queue = destination;

    queue->send_calls++;
    if (queue->count >= 2) {
        return false;
    }
    queue->events[queue->count++] = *(const int *)event;
    return true;
}

static void test_drain_drops_owned_payloads_without_allocating(void)
{
    fake_queue_t queue = {
        .count = 2,
        .allocations_allowed = true,
    };
    owned_event_t scratch;
    size_t drained;

    queue.events[0].payload = fake_allocate_payload(&queue, "message");
    queue.events[1].payload = fake_allocate_payload(&queue, "close reason");

    /* Model finalization under memory pressure: draining cannot allocate. */
    queue.allocations_allowed = false;
    drained = esp32_mquickjs_event_queue_drain(
        &queue, &scratch, fake_receive, fake_drop, &queue);

    assert(drained == 2);
    assert(queue.drop_calls == 2);
    assert(queue.live_payloads == 0);
}

static void test_drain_without_drop_still_consumes_events(void)
{
    fake_queue_t queue = {
        .count = 2,
        .allocations_allowed = false,
    };
    owned_event_t scratch;

    assert(esp32_mquickjs_event_queue_drain(
               &queue, &scratch, fake_receive, NULL, NULL) == 2);
    assert(queue.next == queue.count);
}

static void test_drop_oldest_releases_once_and_accepts_incoming(void)
{
    fake_enqueue_queue_t queue = {.capacity = 1};
    owned_event_t incoming;
    owned_event_t scratch;
    uint32_t dropped = 0;

    queue.events[0].payload =
        fake_enqueue_allocate_payload(&queue, "oldest");
    queue.count = 1;
    incoming.payload = fake_enqueue_allocate_payload(&queue, "incoming");

    assert(esp32_mquickjs_event_queue_enqueue(
        &queue, &incoming, &scratch, true, fake_enqueue_send,
        fake_enqueue_receive, fake_enqueue_drop, &queue, &dropped));
    assert(dropped == 1);
    assert(queue.drop_calls == 1);
    assert(queue.count == 1);
    assert(strcmp(queue.events[0].payload, "incoming") == 0);

    assert(esp32_mquickjs_event_queue_drain(
        &queue, &scratch, fake_enqueue_receive, fake_enqueue_drop,
        &queue) == 1);
    assert(queue.live_payloads == 0);
    assert(queue.drop_calls == 2);
}

static void test_retry_failure_accounts_for_both_events_without_double_free(void)
{
    fake_enqueue_queue_t queue = {.capacity = 1, .fail_retry = true};
    owned_event_t incoming;
    owned_event_t scratch;
    uint32_t dropped = 0;

    queue.events[0].payload =
        fake_enqueue_allocate_payload(&queue, "oldest");
    queue.count = 1;
    incoming.payload = fake_enqueue_allocate_payload(&queue, "incoming");

    assert(!esp32_mquickjs_event_queue_enqueue(
        &queue, &incoming, &scratch, true, fake_enqueue_send,
        fake_enqueue_receive, fake_enqueue_drop, &queue, &dropped));
    assert(dropped == 2);
    assert(queue.drop_calls == 1);
    assert(queue.live_payloads == 1);
    assert(queue.count == 0);

    /* A rejected incoming event remains owned by its caller. */
    fake_enqueue_drop(&incoming, &queue);
    assert(queue.live_payloads == 0);
    assert(queue.drop_calls == 2);
}

static void test_drop_new_rejects_without_consuming_caller_ownership(void)
{
    fake_enqueue_queue_t queue = {.capacity = 1};
    owned_event_t incoming;
    owned_event_t scratch;
    uint32_t dropped = 0;

    queue.events[0].payload = fake_enqueue_allocate_payload(&queue, "queued");
    queue.count = 1;
    incoming.payload = fake_enqueue_allocate_payload(&queue, "incoming");

    assert(!esp32_mquickjs_event_queue_enqueue(
        &queue, &incoming, &scratch, false, fake_enqueue_send,
        fake_enqueue_receive, fake_enqueue_drop, &queue, &dropped));
    assert(dropped == 1);
    assert(queue.drop_calls == 0);
    assert(queue.count == 1);

    fake_enqueue_drop(&incoming, &queue);
    assert(esp32_mquickjs_event_queue_drain(
        &queue, &scratch, fake_enqueue_receive, fake_enqueue_drop,
        &queue) == 1);
    assert(queue.live_payloads == 0);
}

static void test_isr_producer_saturation_counts_every_drop(void)
{
    fake_isr_queue_t queue = {.wake_on_send = true};
    uint32_t dropped = 0;
    int task_woken = 0;
    int event;
    int accepted = 0;

    for (event = 0; event < 100; ++event) {
        if (esp32_mquickjs_event_queue_enqueue_from_isr(
                &queue, &event, fake_isr_send, &task_woken, &dropped)) {
            accepted++;
        }
    }
    assert(accepted == 2);
    assert(queue.count == 2);
    assert(queue.events[0] == 0 && queue.events[1] == 1);
    assert(queue.send_calls == 100);
    assert(dropped == 98);
    assert(task_woken == 1);

    assert(!esp32_mquickjs_event_queue_enqueue_from_isr(
        NULL, &event, fake_isr_send, &task_woken, &dropped));
    assert(dropped == 98);
}

static void test_callback_producer_never_retries_and_counts_every_drop(void)
{
    fake_callback_queue_t queue = {0};
    uint32_t dropped = 0;
    int event;
    int accepted = 0;

    for (event = 0; event < 100; ++event) {
        if (esp32_mquickjs_event_queue_enqueue_from_callback(
                &queue, &event, fake_callback_try_send, &dropped)) {
            accepted++;
        }
    }
    assert(accepted == 2);
    assert(queue.count == 2);
    assert(queue.events[0] == 0 && queue.events[1] == 1);
    assert(queue.send_calls == 100);
    assert(dropped == 98);

    assert(!esp32_mquickjs_event_queue_enqueue_from_callback(
        NULL, &event, fake_callback_try_send, &dropped));
    assert(dropped == 98);
}

int main(void)
{
    test_drain_drops_owned_payloads_without_allocating();
    test_drain_without_drop_still_consumes_events();
    test_drop_oldest_releases_once_and_accepts_incoming();
    test_retry_failure_accounts_for_both_events_without_double_free();
    test_drop_new_rejects_without_consuming_caller_ownership();
    test_isr_producer_saturation_counts_every_drop();
    test_callback_producer_never_retries_and_counts_every_drop();
    return 0;
}
