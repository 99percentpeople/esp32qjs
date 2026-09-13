
#include <stdatomic.h>
#include "freertos/FreeRTOS.h"
#include "esp32_mquickjs_event_queue_resources.h"
#define portENTER_CRITICAL taskENTER_CRITICAL
#define portEXIT_CRITICAL taskEXIT_CRITICAL
#define JS_CLASS_EVENT_QUEUE 1
#define JS_EXCEPTION (-1)
typedef int JSContext;
typedef intptr_t JSValue;
typedef struct {JSValue val;} JSGCRef;
typedef struct {void *event_queue_state;} esp32_mquickjs_runtime_t;
typedef struct {uint8_t slot;uint32_t generation;} esp32_mquickjs_future_token_t;
typedef struct esp32_mquickjs_event_queue esp32_mquickjs_event_queue_t;
typedef struct esp32_mquickjs_future_driver_state esp32_mquickjs_future_driver_state_t;
typedef JSValue (*esp32_mquickjs_event_queue_to_js_fn)(JSContext *,const void *,void *);
typedef void (*esp32_mquickjs_event_queue_drop_fn)(void *,void *);
typedef void (*esp32_mquickjs_event_queue_close_fn)(void *);
typedef enum {ESP32_MQUICKJS_EVENT_QUEUE_DROP_NEWEST,ESP32_MQUICKJS_EVENT_QUEUE_DROP_OLDEST} esp32_mquickjs_event_queue_overflow_t;
typedef uint32_t UBaseType_t;
typedef struct {bool dynamic;uint32_t magic,capacity,item_size;uint8_t *bytes;} StaticQueue_t;
typedef StaticQueue_t StaticSemaphore_t;
typedef StaticQueue_t *QueueHandle_t,*SemaphoreHandle_t;
typedef void *esp_timer_handle_t;
