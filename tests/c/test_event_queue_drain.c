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

int main(void)
{
    test_drain_drops_owned_payloads_without_allocating();
    test_drain_without_drop_still_consumes_events();
    return 0;
}
