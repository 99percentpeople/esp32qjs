#include "esp32_mquickjs_reaper.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct {
    size_t calls;
    size_t ready_after;
} reaper_probe_t;

static bool reap_probe(void *opaque)
{
    reaper_probe_t *probe = opaque;

    probe->calls++;
    return probe->calls >= probe->ready_after;
}

int main(void)
{
    esp32_mquickjs_reaper_registry_t registry;
    reaper_probe_t probes[ESP32_MQUICKJS_REAPER_CAPACITY + 1U] = {0};
    size_t completed;
    size_t attempted;

    esp32_mquickjs_reaper_init(&registry);
    assert(esp32_mquickjs_reaper_pending(&registry) == 0);
    assert(esp32_mquickjs_reaper_capacity(&registry) ==
           ESP32_MQUICKJS_REAPER_CAPACITY);

    probes[0].ready_after = 2;
    assert(esp32_mquickjs_reaper_register(&registry, reap_probe, &probes[0]));
    assert(esp32_mquickjs_reaper_register(&registry, reap_probe, &probes[0]));
    assert(esp32_mquickjs_reaper_pending(&registry) == 1);
    completed = esp32_mquickjs_reaper_poll(&registry, 1, &attempted);
    assert(completed == 0);
    assert(attempted == 1);
    assert(esp32_mquickjs_reaper_pending(&registry) == 1);
    completed = esp32_mquickjs_reaper_poll(&registry, 1, &attempted);
    assert(completed == 1);
    assert(attempted == 1);
    assert(esp32_mquickjs_reaper_pending(&registry) == 0);

    for (size_t index = 0; index < ESP32_MQUICKJS_REAPER_GENERAL_CAPACITY;
         ++index) {
        probes[index].ready_after = 1;
        probes[index].calls = 0;
        assert(esp32_mquickjs_reaper_register(
            &registry, reap_probe, &probes[index]));
    }
    probes[ESP32_MQUICKJS_REAPER_GENERAL_CAPACITY].ready_after = 1;
    assert(!esp32_mquickjs_reaper_register(
        &registry,
        reap_probe,
        &probes[ESP32_MQUICKJS_REAPER_GENERAL_CAPACITY]));
    probes[ESP32_MQUICKJS_REAPER_CAPACITY].ready_after = 1;
    assert(esp32_mquickjs_reaper_register_reserved(
        &registry, ESP32_MQUICKJS_REAPER_RESERVED_EVENT_QUEUE,
        reap_probe, &probes[ESP32_MQUICKJS_REAPER_CAPACITY]));
    assert(esp32_mquickjs_reaper_pending(&registry) ==
           ESP32_MQUICKJS_REAPER_CAPACITY);

    completed = esp32_mquickjs_reaper_poll(
        &registry, ESP32_MQUICKJS_REAPER_BATCH_LIMIT, &attempted);
    assert(completed == ESP32_MQUICKJS_REAPER_BATCH_LIMIT);
    assert(attempted == ESP32_MQUICKJS_REAPER_BATCH_LIMIT);
    assert(esp32_mquickjs_reaper_pending(&registry) ==
           ESP32_MQUICKJS_REAPER_CAPACITY - ESP32_MQUICKJS_REAPER_BATCH_LIMIT);

    assert(esp32_mquickjs_reaper_unregister(
        &registry,
        reap_probe,
        &probes[ESP32_MQUICKJS_REAPER_BATCH_LIMIT]));
    assert(!esp32_mquickjs_reaper_unregister(
        &registry,
        reap_probe,
        &probes[ESP32_MQUICKJS_REAPER_BATCH_LIMIT]));
    assert(esp32_mquickjs_reaper_pending(&registry) ==
           ESP32_MQUICKJS_REAPER_CAPACITY -
               ESP32_MQUICKJS_REAPER_BATCH_LIMIT - 1U);

    return 0;
}
