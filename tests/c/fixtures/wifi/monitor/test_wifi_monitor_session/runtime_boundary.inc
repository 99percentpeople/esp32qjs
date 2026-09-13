
typedef uintptr_t TaskHandle_t;
static TaskHandle_t current_task=1;
static TaskHandle_t xTaskGetCurrentTaskHandle(void) { return current_task; }
typedef bool (*esp32_mquickjs_async_poller_t)(JSContext *,esp32_mquickjs_runtime_t *,void *);
typedef bool (*esp32_mquickjs_reap_fn)(void *);
static esp32_mquickjs_async_poller_t registered_poller;
static esp32_mquickjs_reaper_registry_t registry;
static bool fail_poller;
static unsigned notifications;
static void esp32_mquickjs_notify_activity(esp32_mquickjs_runtime_t *runtime) { assert(runtime);++notifications; }
static bool esp32_mquickjs_register_async_poller(esp32_mquickjs_runtime_t *runtime,esp32_mquickjs_async_poller_t poller,void *opaque) {
    assert(runtime && poller && !opaque);if(fail_poller)return false;registered_poller=poller;return true;
}
static bool esp32_mquickjs_register_reaper(esp32_mquickjs_runtime_t *runtime,esp32_mquickjs_reap_fn reap,void *opaque) {
    assert(runtime);return esp32_mquickjs_reaper_register(&registry,reap,opaque);
}
static void *heap_caps_malloc(size_t size,int caps) { return heap_caps_calloc(1,size,caps); }
#define ESP32_MQUICKJS_MEMORY_DEFAULT 0
#define ESP32_MQUICKJS_MEMORY_BUDGET_POOL 2
#define ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL 1
#define esp32_mquickjs_memory_payload_free heap_caps_free
#define esp32_mquickjs_memory_wireless_alloc(owner,size,policy,role) heap_caps_malloc(size,policy)
static bool esp32_mquickjs_memory_wireless_retire(void *p) { return p != NULL; }
