
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
typedef void *TaskHandle_t;
typedef struct { void *task_handle; } esp32_mquickjs_async_state_t;
typedef struct {
    uint64_t deadline_us, scoped_deadline_us;
    uint16_t native_wait_depth;
    bool (*cooperate)(void *);
    void *cooperate_opaque;
    esp32_mquickjs_async_state_t *async;
} esp32_mquickjs_runtime_t;
typedef struct {uint64_t saved_deadline_us,started_us;bool active;} esp32_mquickjs_native_wait_t;
typedef struct {
    esp32_mquickjs_runtime_t engine;
    TaskHandle_t task;
    bool watchdog_registered, js_watchdog_registered, stop_requested;
    void *js_watchdog_user;
    uint64_t last_outer_heartbeat_us;
} esp32qjs_runtime_t;
static TaskHandle_t current;
static uint64_t now;
static bool due;
static unsigned task_feeds, foreign_feeds, js_feeds;
static TaskHandle_t xTaskGetCurrentTaskHandle(void) {return current;}
static int64_t esp_timer_get_time(void) {return now;}
static esp32_mquickjs_async_state_t *esp32_mquickjs_async_state(esp32_mquickjs_runtime_t *r) {return r?r->async:NULL;}
static bool runtime_control_due(esp32qjs_runtime_t *r) {(void)r;return due;}
static int esp_task_wdt_reset(void) {
    if (current!=(void *)1) foreign_feeds++;
    else task_feeds++;
    return 0;
}
static int esp_task_wdt_reset_user(void *user) {assert(user==(void *)3);js_feeds++;return 0;}
static void runtime_feed_js_watchdog(esp32qjs_runtime_t *runtime);
