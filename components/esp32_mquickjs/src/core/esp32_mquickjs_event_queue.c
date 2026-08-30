#include "esp32_mquickjs_event_queue.h"

#include "esp32_mquickjs_core.h"
#include "esp32_mquickjs_event_queue_drain.h"
#include "esp32_mquickjs_event_queue_resources.h"
#include "esp32_mquickjs_options.h"
#include "esp32_mquickjs_reaper.h"

#include <stdatomic.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

struct esp32_mquickjs_event_queue {
    esp32_mquickjs_runtime_t *runtime;
    esp32_mquickjs_event_queue_resources_t resources;
    size_t event_size;
    uint32_t capacity;
    esp32_mquickjs_event_queue_overflow_t overflow;
    esp32_mquickjs_event_queue_to_js_fn to_js;
    esp32_mquickjs_event_queue_drop_fn drop;
    esp32_mquickjs_event_queue_close_fn close;
    void *opaque;
    portMUX_TYPE lock;
    esp32_mquickjs_future_token_t receiver;
    uint32_t dropped;
    bool receiver_registered;
    bool dispose_requested;
    bool destroying;
    bool reaper_registered;
    bool reaper_hold;
    bool orphaned;
    uint32_t native_retain_count;
    _Atomic bool closed;
    struct esp32_mquickjs_event_queue *next;
};

typedef struct {
    esp32_mquickjs_event_queue_t *head;
    esp32_mquickjs_event_queue_t *orphans;
    bool reaper_registered;
} esp32_mquickjs_event_queue_runtime_t;

struct esp32_mquickjs_future_driver_state {
    esp32_mquickjs_event_queue_t *queue;
    JSContext *ctx;
    JSGCRef queue_ref;
    esp32_mquickjs_runtime_t *runtime;
    esp32_mquickjs_future_token_t token;
    esp_timer_handle_t timer;
    void *event;
    uint32_t timeout_ms;
    bool received;
    _Atomic bool timed_out;
    _Atomic bool completed;
    bool queue_retained;
    bool native_queue_retained;
};

static void *event_queue_resource_allocate(size_t bytes, void *opaque)
{
    (void)opaque;
    return heap_caps_malloc(bytes, MALLOC_CAP_8BIT);
}

static void event_queue_resource_release(void *allocation, void *opaque)
{
    (void)opaque;
    heap_caps_free(allocation);
}

static void *event_queue_resource_create_lock(void *opaque)
{
    (void)opaque;
    return xSemaphoreCreateMutex();
}

static void event_queue_resource_delete_lock(void *lock, void *opaque)
{
    (void)opaque;
    vSemaphoreDelete((SemaphoreHandle_t)lock);
}

static void *event_queue_resource_create_queue(uint32_t capacity,
                                               size_t event_size,
                                               void *opaque)
{
    (void)opaque;
    return xQueueCreate(capacity, event_size);
}

static void event_queue_resource_delete_queue(void *queue, void *opaque)
{
    (void)opaque;
    vQueueDelete((QueueHandle_t)queue);
}

static const esp32_mquickjs_event_queue_resource_ops_t
    s_event_queue_resource_ops = {
        .allocate = event_queue_resource_allocate,
        .release = event_queue_resource_release,
        .create_lock = event_queue_resource_create_lock,
        .delete_lock = event_queue_resource_delete_lock,
        .create_queue = event_queue_resource_create_queue,
        .delete_queue = event_queue_resource_delete_queue,
    };

static esp32_mquickjs_event_queue_t *event_queue_from_value(JSContext *ctx,
                                                             JSValue value)
{
    if (JS_GetClassID(ctx, value) == JS_CLASS_EVENT_QUEUE) {
        return JS_GetOpaque(ctx, value);
    }
    if (JS_GetClassID(ctx, value) >= 0) {
        JSValue nested = JS_GetPropertyStr(ctx, value, "_eventQueue");

        if (!JS_IsException(nested) &&
            JS_GetClassID(ctx, nested) == JS_CLASS_EVENT_QUEUE) {
            return JS_GetOpaque(ctx, nested);
        }
    }
    return NULL;
}

static esp32_mquickjs_event_queue_runtime_t *event_queue_runtime(
    esp32_mquickjs_runtime_t *runtime)
{
    return runtime != NULL ? runtime->event_queue_state : NULL;
}

static void event_queue_register(esp32_mquickjs_event_queue_t *queue)
{
    esp32_mquickjs_event_queue_runtime_t *state;

    if (queue == NULL || (state = event_queue_runtime(queue->runtime)) == NULL) {
        return;
    }
    queue->next = state->head;
    state->head = queue;
}

static void event_queue_unregister(esp32_mquickjs_event_queue_t *queue)
{
    esp32_mquickjs_event_queue_runtime_t *state;
    esp32_mquickjs_event_queue_t **cursor;

    if (queue == NULL || (state = event_queue_runtime(queue->runtime)) == NULL) {
        return;
    }
    cursor = &state->head;
    while (*cursor != NULL) {
        if (*cursor == queue) {
            *cursor = queue->next;
            queue->next = NULL;
            return;
        }
        cursor = &(*cursor)->next;
    }
}

static void event_queue_move_to_orphans(esp32_mquickjs_event_queue_t *queue)
{
    esp32_mquickjs_event_queue_runtime_t *state;

    if (queue == NULL || queue->orphaned ||
        (state = event_queue_runtime(queue->runtime)) == NULL) {
        return;
    }
    event_queue_unregister(queue);
    queue->next = state->orphans;
    state->orphans = queue;
    queue->orphaned = true;
}

static void event_queue_destroy_native(esp32_mquickjs_event_queue_t *queue)
{
    if (queue == NULL) {
        return;
    }
    esp32_mquickjs_event_queue_resources_deinit(
        &queue->resources, &s_event_queue_resource_ops, NULL);
    heap_caps_free(queue);
}

static bool event_queue_take_destroy_ownership_locked(
    esp32_mquickjs_event_queue_t *queue)
{
    if (queue->dispose_requested && !queue->receiver_registered &&
        queue->native_retain_count == 0 && !queue->destroying) {
        queue->destroying = true;
        return true;
    }
    return false;
}

static bool event_queue_drain_receive(void *source, void *event);
static void event_queue_wake_receiver(esp32_mquickjs_event_queue_t *queue);

static bool event_queue_reap(void *opaque)
{
    esp32_mquickjs_event_queue_t *queue = opaque;
    esp32_mquickjs_event_queue_close_fn close;
    void *close_opaque;
    bool destroy = false;

    if (queue == NULL || queue->resources.send_lock == NULL ||
        xSemaphoreTake(
            (SemaphoreHandle_t)queue->resources.send_lock,
            pdMS_TO_TICKS(ESP32_MQUICKJS_REAPER_RETRY_MS)) != pdTRUE) {
        return false;
    }
    portENTER_CRITICAL(&queue->lock);
    close = queue->close;
    close_opaque = queue->opaque;
    queue->close = NULL;
    portEXIT_CRITICAL(&queue->lock);
    xSemaphoreGive((SemaphoreHandle_t)queue->resources.send_lock);

    if (close != NULL) {
        close(close_opaque);
    }

    if (queue->resources.events == NULL ||
        queue->resources.drain_scratch == NULL ||
        xSemaphoreTake(
            (SemaphoreHandle_t)queue->resources.send_lock,
            pdMS_TO_TICKS(ESP32_MQUICKJS_REAPER_RETRY_MS)) != pdTRUE) {
        return false;
    }
    (void)esp32_mquickjs_event_queue_drain(
        queue->resources.events, queue->resources.drain_scratch,
        event_queue_drain_receive, queue->drop, queue->opaque);
    xSemaphoreGive((SemaphoreHandle_t)queue->resources.send_lock);

    portENTER_CRITICAL(&queue->lock);
    queue->reaper_registered = false;
    if (queue->reaper_hold) {
        queue->reaper_hold = false;
        queue->native_retain_count--;
    }
    destroy = event_queue_take_destroy_ownership_locked(queue);
    portEXIT_CRITICAL(&queue->lock);
    if (destroy) {
        event_queue_destroy_native(queue);
    }
    return true;
}

static bool event_queue_runtime_reap(void *opaque)
{
    esp32_mquickjs_event_queue_runtime_t *state = opaque;
    esp32_mquickjs_event_queue_t *queue;
    esp32_mquickjs_event_queue_t *next;
    esp32_mquickjs_event_queue_t **cursor;
    size_t attempted = 0;
    bool pending = false;

    if (state == NULL) {
        return true;
    }
    for (queue = state->head;
         queue != NULL && attempted < ESP32_MQUICKJS_REAPER_BATCH_LIMIT;
         queue = next) {
        next = queue->next;
        if (!queue->reaper_registered) {
            continue;
        }
        attempted++;
        (void)event_queue_reap(queue);
    }

    cursor = &state->orphans;
    while (*cursor != NULL &&
           attempted < ESP32_MQUICKJS_REAPER_BATCH_LIMIT) {
        queue = *cursor;
        next = queue->next;
        attempted++;
        if (event_queue_reap(queue)) {
            *cursor = next;
        } else {
            cursor = &queue->next;
        }
    }

    for (queue = state->head; queue != NULL; queue = queue->next) {
        if (queue->reaper_registered) {
            pending = true;
            break;
        }
    }
    if (state->orphans != NULL) {
        pending = true;
    }
    if (!pending) {
        state->reaper_registered = false;
    }
    return !pending;
}

static void event_queue_request_reap(esp32_mquickjs_event_queue_t *queue,
                                     bool dispose)
{
    esp32_mquickjs_event_queue_runtime_t *state;
    bool register_reaper = false;

    if (queue == NULL ||
        (state = event_queue_runtime(queue->runtime)) == NULL) {
        return;
    }
    if (dispose) {
        event_queue_move_to_orphans(queue);
    }
    atomic_store_explicit(&queue->closed, true, memory_order_release);
    portENTER_CRITICAL(&queue->lock);
    if (dispose) {
        queue->dispose_requested = true;
    }
    if (!queue->reaper_registered && !queue->destroying) {
        if (!queue->reaper_hold && queue->native_retain_count < UINT32_MAX) {
            queue->native_retain_count++;
            queue->reaper_hold = true;
        }
        if (queue->reaper_hold) {
            queue->reaper_registered = true;
        }
    }
    if (queue->reaper_registered && !state->reaper_registered) {
        state->reaper_registered = true;
        register_reaper = true;
    }
    portEXIT_CRITICAL(&queue->lock);

    if (register_reaper && !esp32_mquickjs_register_reserved_reaper(
            queue->runtime, ESP32_MQUICKJS_REAPER_RESERVED_EVENT_QUEUE,
            event_queue_runtime_reap, state)) {
        state->reaper_registered = false;
    }
    event_queue_wake_receiver(queue);
}

static bool event_queue_drain_receive(void *source, void *event)
{
    return xQueueReceive((QueueHandle_t)source, event, 0) == pdTRUE;
}

static bool event_queue_enqueue_send(void *destination, const void *event)
{
    return xQueueSend((QueueHandle_t)destination, event, 0) == pdTRUE;
}

static bool event_queue_enqueue_send_from_isr(void *destination,
                                              const void *event,
                                              int *task_woken)
{
    BaseType_t higher_priority_woken = pdFALSE;
    bool sent = xQueueSendFromISR(
                    (QueueHandle_t)destination, event,
                    &higher_priority_woken) == pdTRUE;

    if (task_woken != NULL && higher_priority_woken == pdTRUE) {
        *task_woken = 1;
    }
    return sent;
}

esp32_mquickjs_event_queue_t *esp32_mquickjs_event_queue_from_value(
    JSContext *ctx,
    JSValue value)
{
    return event_queue_from_value(ctx, value);
}

bool esp32_mquickjs_event_queue_retain(esp32_mquickjs_event_queue_t *queue)
{
    bool retained = false;

    if (queue == NULL) {
        return false;
    }
    portENTER_CRITICAL(&queue->lock);
    if (!queue->dispose_requested && !queue->destroying &&
        queue->native_retain_count < UINT32_MAX) {
        queue->native_retain_count++;
        retained = true;
    }
    portEXIT_CRITICAL(&queue->lock);
    return retained;
}

void esp32_mquickjs_event_queue_release(esp32_mquickjs_event_queue_t *queue)
{
    bool destroy = false;

    if (queue == NULL) {
        return;
    }
    portENTER_CRITICAL(&queue->lock);
    if (queue->native_retain_count > 0) {
        queue->native_retain_count--;
        destroy = event_queue_take_destroy_ownership_locked(queue);
    }
    portEXIT_CRITICAL(&queue->lock);
    if (destroy) {
        event_queue_destroy_native(queue);
    }
}

static void event_queue_wake_receiver(esp32_mquickjs_event_queue_t *queue)
{
    esp32_mquickjs_future_token_t token = {0};
    bool registered;

    portENTER_CRITICAL(&queue->lock);
    registered = queue->receiver_registered;
    token = queue->receiver;
    portEXIT_CRITICAL(&queue->lock);
    if (registered) {
        (void)esp32_mquickjs_future_wake(queue->runtime, token);
    }
    esp32_mquickjs_notify_activity(queue->runtime);
}

static bool event_queue_register_receiver(esp32_mquickjs_event_queue_t *queue,
                                          esp32_mquickjs_future_token_t token)
{
    bool registered = false;

    portENTER_CRITICAL(&queue->lock);
    if (!queue->receiver_registered) {
        queue->receiver_registered = true;
        queue->receiver = token;
        registered = true;
    }
    portEXIT_CRITICAL(&queue->lock);
    return registered;
}

static void event_queue_clear_receiver(esp32_mquickjs_event_queue_t *queue,
                                       esp32_mquickjs_future_token_t token)
{
    portENTER_CRITICAL(&queue->lock);
    if (queue->receiver_registered &&
        queue->receiver.slot == token.slot &&
        queue->receiver.generation == token.generation) {
        queue->receiver_registered = false;
        memset(&queue->receiver, 0, sizeof(queue->receiver));
    }
    portEXIT_CRITICAL(&queue->lock);
}

bool esp32_mquickjs_event_queue_send(esp32_mquickjs_event_queue_t *queue,
                                     const void *event)
{
    bool sent = false;
    uint32_t dropped = 0;

    if (queue == NULL || event == NULL || queue->resources.events == NULL ||
        queue->resources.send_lock == NULL ||
        atomic_load_explicit(&queue->closed, memory_order_acquire)) {
        return false;
    }
    if (xSemaphoreTake(
            (SemaphoreHandle_t)queue->resources.send_lock,
            portMAX_DELAY) != pdTRUE) {
        return false;
    }
    if (atomic_load_explicit(&queue->closed, memory_order_acquire)) {
        xSemaphoreGive((SemaphoreHandle_t)queue->resources.send_lock);
        return false;
    }
    sent = esp32_mquickjs_event_queue_enqueue(
        queue->resources.events, event, queue->resources.overflow_scratch,
        queue->overflow == ESP32_MQUICKJS_EVENT_QUEUE_DROP_OLDEST,
        event_queue_enqueue_send, event_queue_drain_receive, queue->drop,
        queue->opaque, &dropped);
    if (dropped > 0) {
        portENTER_CRITICAL(&queue->lock);
        queue->dropped += dropped;
        portEXIT_CRITICAL(&queue->lock);
    }
    if (!sent) {
        xSemaphoreGive((SemaphoreHandle_t)queue->resources.send_lock);
        return false;
    }
    xSemaphoreGive((SemaphoreHandle_t)queue->resources.send_lock);
    event_queue_wake_receiver(queue);
    return true;
}

bool esp32_mquickjs_event_queue_try_send_from_callback(
    esp32_mquickjs_event_queue_t *queue,
    const void *event)
{
    uint32_t dropped = 0;

    if (queue == NULL || event == NULL || queue->resources.events == NULL ||
        queue->overflow != ESP32_MQUICKJS_EVENT_QUEUE_DROP_NEWEST ||
        atomic_load_explicit(&queue->closed, memory_order_acquire)) {
        return false;
    }
    if (!esp32_mquickjs_event_queue_enqueue_from_callback(
            queue->resources.events, event, event_queue_enqueue_send,
            &dropped)) {
        if (dropped > 0) {
            portENTER_CRITICAL(&queue->lock);
            queue->dropped += dropped;
            portEXIT_CRITICAL(&queue->lock);
        }
        return false;
    }
    event_queue_wake_receiver(queue);
    return true;
}

bool esp32_mquickjs_event_queue_send_from_isr(esp32_mquickjs_event_queue_t *queue,
                                              const void *event,
                                              int *task_woken)
{
    int higher_priority_woken = 0;
    uint32_t dropped = 0;
    esp32_mquickjs_future_token_t token = {0};
    bool registered;

    if (queue == NULL || event == NULL || queue->resources.events == NULL ||
        atomic_load_explicit(&queue->closed, memory_order_acquire)) {
        return false;
    }
    if (!esp32_mquickjs_event_queue_enqueue_from_isr(
            queue->resources.events, event,
            event_queue_enqueue_send_from_isr,
            &higher_priority_woken, &dropped)) {
        portENTER_CRITICAL_ISR(&queue->lock);
        queue->dropped += dropped;
        portEXIT_CRITICAL_ISR(&queue->lock);
        if (task_woken != NULL && higher_priority_woken != 0) {
            *task_woken = 1;
        }
        return false;
    }
    portENTER_CRITICAL_ISR(&queue->lock);
    registered = queue->receiver_registered;
    token = queue->receiver;
    portEXIT_CRITICAL_ISR(&queue->lock);
    if (registered) {
        int future_woken = 0;
        (void)esp32_mquickjs_future_wake_from_isr(queue->runtime, token, &future_woken);
        if (future_woken != 0) {
            higher_priority_woken = 1;
        }
    }
    if (task_woken != NULL && higher_priority_woken != 0) {
        *task_woken = 1;
    }
    return true;
}

bool esp32_mquickjs_event_queue_close(esp32_mquickjs_event_queue_t *queue)
{
    esp32_mquickjs_event_queue_close_fn close;
    void *opaque;

    if (queue == NULL || queue->resources.send_lock == NULL ||
        xSemaphoreTake((SemaphoreHandle_t)queue->resources.send_lock,
                       portMAX_DELAY) != pdTRUE) {
        return false;
    }
    portENTER_CRITICAL(&queue->lock);
    if (atomic_load_explicit(&queue->closed, memory_order_acquire)) {
        portEXIT_CRITICAL(&queue->lock);
        xSemaphoreGive((SemaphoreHandle_t)queue->resources.send_lock);
        return false;
    }
    atomic_store_explicit(&queue->closed, true, memory_order_release);
    close = queue->close;
    opaque = queue->opaque;
    queue->close = NULL;
    portEXIT_CRITICAL(&queue->lock);
    xSemaphoreGive((SemaphoreHandle_t)queue->resources.send_lock);
    if (close != NULL) {
        close(opaque);
    }
    event_queue_wake_receiver(queue);
    return true;
}

void esp32_mquickjs_event_queue_request_close(
    esp32_mquickjs_event_queue_t *queue)
{
    event_queue_request_reap(queue, false);
}

bool esp32_mquickjs_event_queue_dispose(JSContext *ctx, JSValue value)
{
    esp32_mquickjs_event_queue_t *queue;
    bool destroy;

    if (ctx == NULL || JS_GetClassID(ctx, value) != JS_CLASS_EVENT_QUEUE ||
        (queue = JS_GetOpaque(ctx, value)) == NULL) {
        return false;
    }
    JS_SetOpaque(ctx, value, NULL);
    event_queue_unregister(queue);
    (void)esp32_mquickjs_event_queue_close(queue);
    (void)esp32_mquickjs_event_queue_discard_all(queue);
    portENTER_CRITICAL(&queue->lock);
    queue->dispose_requested = true;
    destroy = event_queue_take_destroy_ownership_locked(queue);
    portEXIT_CRITICAL(&queue->lock);
    if (destroy) {
        event_queue_destroy_native(queue);
    }
    return true;
}

size_t esp32_mquickjs_event_queue_discard_all(
    esp32_mquickjs_event_queue_t *queue)
{
    size_t discarded;

    if (queue == NULL || queue->resources.events == NULL ||
        queue->resources.drain_scratch == NULL ||
        queue->resources.send_lock == NULL ||
        xSemaphoreTake((SemaphoreHandle_t)queue->resources.send_lock,
                       portMAX_DELAY) != pdTRUE) {
        return 0;
    }
    discarded = esp32_mquickjs_event_queue_drain(
        queue->resources.events, queue->resources.drain_scratch,
        event_queue_drain_receive,
        queue->drop, queue->opaque);
    xSemaphoreGive((SemaphoreHandle_t)queue->resources.send_lock);
    return discarded;
}

bool esp32_mquickjs_event_queue_is_closed(const esp32_mquickjs_event_queue_t *queue)
{
    return queue == NULL || atomic_load_explicit(
                                &queue->closed, memory_order_acquire);
}

bool esp32_mquickjs_event_queue_get_stats(
    esp32_mquickjs_event_queue_t *queue,
    esp32_mquickjs_event_queue_stats_t *stats)
{
    if (queue == NULL || stats == NULL ||
        queue->resources.events == NULL) {
        return false;
    }
    memset(stats, 0, sizeof(*stats));
    stats->queued = (uint32_t)uxQueueMessagesWaiting(
        (QueueHandle_t)queue->resources.events);
    stats->capacity = queue->capacity;
    portENTER_CRITICAL(&queue->lock);
    stats->open = !atomic_load_explicit(&queue->closed,
                                        memory_order_acquire);
    stats->dropped = queue->dropped;
    stats->receiver_pending = queue->receiver_registered;
    portEXIT_CRITICAL(&queue->lock);
    return true;
}

uint32_t esp32_mquickjs_event_queue_dropped(esp32_mquickjs_event_queue_t *queue)
{
    uint32_t dropped = 0;

    if (queue == NULL) {
        return 0;
    }
    portENTER_CRITICAL(&queue->lock);
    dropped = queue->dropped;
    portEXIT_CRITICAL(&queue->lock);
    return dropped;
}

static void event_queue_timer_cb(void *arg)
{
    esp32_mquickjs_future_driver_state_t *state = arg;

    if (state == NULL || atomic_load_explicit(
                             &state->completed, memory_order_acquire)) {
        return;
    }
    atomic_store_explicit(&state->timed_out, true, memory_order_release);
    (void)esp32_mquickjs_future_wake(state->runtime, state->token);
}

static bool event_queue_future_prepare(JSContext *ctx,
                                       JSGCRef *this_ref,
                                       int argc,
                                       JSGCRef *argv,
                                       esp32_mquickjs_future_driver_state_t **out_state)
{
    esp32_mquickjs_event_queue_t *queue = event_queue_from_value(ctx, this_ref->val);
    esp32_mquickjs_future_driver_state_t *state;
    uint32_t timeout_ms = 0;

    if (queue == NULL || out_state == NULL || argc > 1 ||
        (argc == 1 && !esp32_mquickjs_value_to_bounded_u32(
            ctx, argv[0].val, 0, INT32_MAX, &timeout_ms))) {
        JS_ThrowTypeError(ctx, "EventQueue.receive(timeoutMs?) expects a non-negative timeout");
        return false;
    }
    state = heap_caps_calloc(1, sizeof(*state), MALLOC_CAP_8BIT);
    if (state == NULL) {
        JS_ThrowOutOfMemory(ctx);
        return false;
    }
    state->event = heap_caps_malloc(queue->event_size, MALLOC_CAP_8BIT);
    if (state->event == NULL) {
        heap_caps_free(state);
        JS_ThrowOutOfMemory(ctx);
        return false;
    }
    if (!esp32_mquickjs_event_queue_retain(queue)) {
        heap_caps_free(state->event);
        heap_caps_free(state);
        JS_ThrowInternalError(ctx, "EventQueue.receive() lost its queue");
        return false;
    }
    state->queue = queue;
    state->native_queue_retained = true;
    state->ctx = ctx;
    *JS_AddGCRef(ctx, &state->queue_ref) = this_ref->val;
    state->queue_retained = true;
    state->timeout_ms = argc == 1 ? timeout_ms : UINT32_MAX;
    atomic_init(&state->timed_out, false);
    atomic_init(&state->completed, false);
    *out_state = state;
    return true;
}

static bool event_queue_future_start(JSContext *ctx,
                                     esp32_mquickjs_runtime_t *runtime,
                                     esp32_mquickjs_future_token_t token,
                                     esp32_mquickjs_future_driver_state_t *state)
{
    esp_timer_create_args_t timer_args = {
        .callback = event_queue_timer_cb,
        .arg = state,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "mqjs_event_queue",
        .skip_unhandled_events = true,
    };

    if (state == NULL || state->queue == NULL) {
        JS_ThrowInternalError(ctx, "EventQueue.receive() lost its queue");
        return false;
    }
    state->runtime = runtime;
    state->token = token;
    if (!event_queue_register_receiver(state->queue, token)) {
        JS_ThrowInternalError(ctx, "EventQueue.receive() already has a pending receiver");
        return false;
    }
    if (uxQueueMessagesWaiting(
            (QueueHandle_t)state->queue->resources.events) > 0 ||
        atomic_load_explicit(&state->queue->closed, memory_order_acquire)) {
        (void)esp32_mquickjs_future_wake(runtime, token);
        return true;
    }
    if (state->timeout_ms != UINT32_MAX) {
        if (state->timeout_ms == 0) {
            atomic_store_explicit(&state->timed_out, true,
                                  memory_order_release);
            (void)esp32_mquickjs_future_wake(runtime, token);
        } else if (esp_timer_create(&timer_args, &state->timer) != ESP_OK ||
                   esp_timer_start_once(state->timer,
                                        (uint64_t)state->timeout_ms * 1000ULL) != ESP_OK) {
            event_queue_clear_receiver(state->queue, token);
            if (state->timer != NULL) {
                esp_timer_delete(state->timer);
                state->timer = NULL;
            }
            JS_ThrowInternalError(ctx, "EventQueue.receive() failed to start timeout timer");
            return false;
        }
    }
    return true;
}

static esp32_mquickjs_future_poll_t event_queue_future_poll(
    esp32_mquickjs_future_driver_state_t *state)
{
    if (state == NULL || atomic_load_explicit(
                             &state->completed, memory_order_acquire)) {
        return ESP32_MQUICKJS_FUTURE_READY;
    }
    if (xQueueReceive((QueueHandle_t)state->queue->resources.events,
                      state->event, 0) == pdTRUE) {
        state->received = true;
        atomic_store_explicit(&state->completed, true, memory_order_release);
    } else if (atomic_load_explicit(&state->timed_out, memory_order_acquire) ||
               atomic_load_explicit(&state->queue->closed,
                                    memory_order_acquire)) {
        atomic_store_explicit(&state->completed, true, memory_order_release);
    }
    if (atomic_load_explicit(&state->completed, memory_order_acquire)) {
        event_queue_clear_receiver(state->queue, state->token);
        return ESP32_MQUICKJS_FUTURE_READY;
    }
    return ESP32_MQUICKJS_FUTURE_PENDING;
}

static JSValue event_queue_future_finish(JSContext *ctx,
                                         esp32_mquickjs_future_driver_state_t *state)
{
    if (state == NULL || !state->received) {
        return JS_NULL;
    }
    return state->queue->to_js(ctx, state->event, state->queue->opaque);
}

static esp32_mquickjs_cancel_result_t event_queue_future_cancel(
    esp32_mquickjs_future_driver_state_t *state)
{
    if (state == NULL || atomic_load_explicit(
                             &state->completed, memory_order_acquire)) {
        return ESP32_MQUICKJS_CANCEL_REJECTED;
    }
    atomic_store_explicit(&state->completed, true, memory_order_release);
    event_queue_clear_receiver(state->queue, state->token);
    return ESP32_MQUICKJS_CANCELLED;
}

static void event_queue_future_destroy(esp32_mquickjs_future_driver_state_t *state)
{
    esp32_mquickjs_event_queue_t *queue;
    bool release_queue;

    if (state == NULL) {
        return;
    }
    queue = state->queue;
    release_queue = state->native_queue_retained;
    if (state->timer != NULL) {
        (void)esp_timer_stop(state->timer);
        esp_timer_delete(state->timer);
    }
    if (state->queue_retained) {
        JS_DeleteGCRef(state->ctx, &state->queue_ref);
        state->queue_retained = false;
    }
    heap_caps_free(state->event);
    heap_caps_free(state);
    if (release_queue) {
        esp32_mquickjs_event_queue_release(queue);
    }
}

static const esp32_mquickjs_future_driver_t s_event_queue_future_driver = {
    .capture = event_queue_future_prepare,
    .start = event_queue_future_start,
    .poll = event_queue_future_poll,
    .finish = event_queue_future_finish,
    .cancel = event_queue_future_cancel,
    .destroy = event_queue_future_destroy,
};

bool esp32_mquickjs_event_queue_register_receive_alias(
    JSContext *ctx,
    esp32_mquickjs_runtime_t *runtime,
    JSValue function)
{
    return esp32_mquickjs_future_register_driver(
        ctx, runtime, function, &s_event_queue_future_driver);
}

bool esp32_mquickjs_init_event_queue_runtime(JSContext *ctx,
                                              esp32_mquickjs_runtime_t *runtime)
{
    esp32_mquickjs_event_queue_runtime_t *state;
    JSGCRef object_ref;
    JSGCRef receive_ref;
    JSValue *object;
    JSValue *receive;
    bool result;

    if (runtime == NULL || runtime->event_queue_state != NULL) {
        return false;
    }
    state = heap_caps_calloc(1, sizeof(*state), MALLOC_CAP_8BIT);
    if (state == NULL) {
        return false;
    }
    runtime->event_queue_state = state;

    object = JS_PushGCRef(ctx, &object_ref);
    receive = JS_PushGCRef(ctx, &receive_ref);

    *object = JS_NewObjectClassUser(ctx, JS_CLASS_EVENT_QUEUE);
    *receive = JS_IsException(*object)
        ? JS_EXCEPTION
        : JS_GetPropertyStr(ctx, *object, "receive");
    result = !JS_IsException(*object) && !JS_IsException(*receive) &&
             esp32_mquickjs_event_queue_register_receive_alias(
                 ctx, runtime, *receive);
    if (!result && !JS_IsException(*object) && !JS_IsException(*receive)) {
        JS_ThrowInternalError(ctx, "failed to register EventQueue Future driver");
    }
    JS_PopGCRef(ctx, &receive_ref);
    JS_PopGCRef(ctx, &object_ref);
    if (!result) {
        esp32_mquickjs_deinit_event_queue_runtime(runtime);
    }
    return result;
}

void esp32_mquickjs_deinit_event_queue_runtime(esp32_mquickjs_runtime_t *runtime)
{
    esp32_mquickjs_event_queue_runtime_t *state = event_queue_runtime(runtime);
    esp32_mquickjs_event_queue_t *queue;
    esp32_mquickjs_event_queue_t *next;

    if (state == NULL) {
        return;
    }
    if (state->reaper_registered) {
        (void)esp32_mquickjs_unregister_reaper(
            runtime, event_queue_runtime_reap, state);
        state->reaper_registered = false;
    }
    while ((queue = state->head) != NULL) {
        state->head = queue->next;
        queue->next = state->orphans;
        state->orphans = queue;
        queue->orphaned = true;
        atomic_store_explicit(&queue->closed, true, memory_order_release);
        portENTER_CRITICAL(&queue->lock);
        queue->dispose_requested = true;
        if (!queue->reaper_hold) {
            queue->native_retain_count++;
            queue->reaper_hold = true;
        }
        queue->reaper_registered = true;
        portEXIT_CRITICAL(&queue->lock);
    }
    for (queue = state->orphans; queue != NULL; queue = next) {
        next = queue->next;
        while (!event_queue_reap(queue)) {
            vTaskDelay(1);
        }
    }
    state->orphans = NULL;
    heap_caps_free(state);
    runtime->event_queue_state = NULL;
}

bool esp32_mquickjs_get_event_queue_status(
    esp32_mquickjs_runtime_t *runtime,
    esp32_mquickjs_event_queue_status_t *status)
{
    esp32_mquickjs_event_queue_runtime_t *state = event_queue_runtime(runtime);
    esp32_mquickjs_event_queue_t *queue;

    if (status == NULL) {
        return false;
    }
    memset(status, 0, sizeof(*status));
    if (state == NULL) {
        return true;
    }
    for (queue = state->head; queue != NULL; queue = queue->next) {
        uint32_t dropped;
        bool closed;

        portENTER_CRITICAL(&queue->lock);
        dropped = queue->dropped;
        closed = atomic_load_explicit(&queue->closed, memory_order_acquire);
        portEXIT_CRITICAL(&queue->lock);
        status->dropped += dropped;
        if (!closed) {
            status->open++;
        }
    }
    return true;
}

JSValue esp32_mquickjs_event_queue_new(JSContext *ctx,
                                       esp32_mquickjs_runtime_t *runtime,
                                       size_t event_size,
                                       uint32_t capacity,
                                       esp32_mquickjs_event_queue_overflow_t overflow,
                                       esp32_mquickjs_event_queue_to_js_fn to_js,
                                       esp32_mquickjs_event_queue_drop_fn drop,
                                       esp32_mquickjs_event_queue_close_fn close,
                                       void *opaque)
{
    esp32_mquickjs_event_queue_t *queue;
    JSValue object;

    if (ctx == NULL || runtime == NULL || event_size == 0 || capacity == 0 || to_js == NULL) {
        return JS_ThrowInternalError(ctx, "invalid native EventQueue configuration");
    }
    queue = heap_caps_calloc(1, sizeof(*queue), MALLOC_CAP_8BIT);
    if (queue == NULL) {
        return JS_ThrowOutOfMemory(ctx);
    }
    if (!esp32_mquickjs_event_queue_resources_init(
            &queue->resources, event_size, capacity,
            overflow == ESP32_MQUICKJS_EVENT_QUEUE_DROP_OLDEST,
            &s_event_queue_resource_ops, NULL)) {
        heap_caps_free(queue);
        return JS_ThrowOutOfMemory(ctx);
    }
    queue->runtime = runtime;
    queue->event_size = event_size;
    queue->capacity = capacity;
    queue->overflow = overflow;
    queue->to_js = to_js;
    queue->drop = drop;
    queue->close = close;
    queue->opaque = opaque;
    queue->lock = (portMUX_TYPE)portMUX_INITIALIZER_UNLOCKED;
    atomic_init(&queue->closed, false);
    object = JS_NewObjectClassUser(ctx, JS_CLASS_EVENT_QUEUE);
    if (JS_IsException(object)) {
        esp32_mquickjs_event_queue_resources_deinit(
            &queue->resources, &s_event_queue_resource_ops, NULL);
        heap_caps_free(queue);
        return object;
    }
    JS_SetOpaque(ctx, object, queue);
    event_queue_register(queue);
    return object;
}

JSValue js_event_queue_constructor(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_ThrowTypeError(ctx, "EventQueue cannot be constructed directly");
}

void js_event_queue_finalizer(JSContext *ctx, void *opaque)
{
    esp32_mquickjs_event_queue_t *queue = opaque;

    (void)ctx;
    if (queue == NULL) {
        return;
    }
    event_queue_request_reap(queue, true);
}

JSValue js_event_queue_receive(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    JSGCRef receive_ref;
    JSValue *receive = JS_PushGCRef(ctx, &receive_ref);
    JSValue result;

    if (this_val == NULL || event_queue_from_value(ctx, *this_val) == NULL) {
        JS_PopGCRef(ctx, &receive_ref);
        return JS_ThrowTypeError(ctx, "EventQueue.receive() called on an incompatible receiver");
    }
    *receive = JS_GetPropertyStr(ctx, *this_val, "receive");
    result = JS_IsException(*receive)
        ? JS_EXCEPTION
        : esp32_mquickjs_future_call_and_wait(ctx,
                                               esp32_mquickjs_get_active_runtime(),
                                               *receive,
                                               *this_val,
                                               argc,
                                               argv);
    JS_PopGCRef(ctx, &receive_ref);
    return result;
}

JSValue js_event_queue_stats(JSContext *ctx, JSValue *this_val,
                             int argc, JSValue *argv)
{
    esp32_mquickjs_event_queue_t *queue;
    esp32_mquickjs_event_queue_stats_t stats;
    JSGCRef object_ref;
    JSValue *object;

    (void)argc;
    (void)argv;
    if (this_val == NULL ||
        (queue = event_queue_from_value(ctx, *this_val)) == NULL) {
        return JS_ThrowTypeError(
            ctx, "EventQueue.stats() called on an incompatible receiver");
    }
    if (!esp32_mquickjs_event_queue_get_stats(queue, &stats)) {
        return JS_ThrowInternalError(ctx, "EventQueue stats are unavailable");
    }
    object = JS_PushGCRef(ctx, &object_ref);
    *object = JS_NewObject(ctx);
    if (JS_IsException(*object) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "open",
                                         JS_NewBool(stats.open)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "queued",
                                         JS_NewUint32(ctx, stats.queued)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "capacity",
                                         JS_NewUint32(ctx, stats.capacity)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "dropped",
                                         JS_NewUint32(ctx, stats.dropped)) ||
        !esp32_mquickjs_set_property_ref(ctx, object, "receiverPending",
                                         JS_NewBool(stats.receiver_pending))) {
        JS_PopGCRef(ctx, &object_ref);
        return JS_EXCEPTION;
    }
    return JS_PopGCRef(ctx, &object_ref);
}

JSValue js_event_queue_close(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    esp32_mquickjs_event_queue_t *queue;

    (void)argc;
    (void)argv;
    if (this_val == NULL || (queue = event_queue_from_value(ctx, *this_val)) == NULL) {
        return JS_ThrowTypeError(ctx, "EventQueue.close() called on an incompatible receiver");
    }
    return JS_NewBool(esp32_mquickjs_event_queue_close(queue));
}
