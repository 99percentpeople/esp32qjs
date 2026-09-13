
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <pthread.h>
#include <stdatomic.h>
#include "esp32_mquickjs_event_queue_resources.h"
#include "esp32_mquickjs_event_queue_drain.h"
typedef pthread_mutex_t portMUX_TYPE;
typedef int JSContext,JSValue;
typedef struct {JSValue val;} JSGCRef;
typedef struct {void *event_queue_state;} esp32_mquickjs_runtime_t;
typedef struct {uint8_t slot;uint32_t generation;} esp32_mquickjs_future_token_t;
typedef struct esp32_mquickjs_event_queue esp32_mquickjs_event_queue_t;
typedef JSValue (*esp32_mquickjs_event_queue_to_js_fn)(JSContext *,const void *,void *);
typedef void (*esp32_mquickjs_event_queue_drop_fn)(void *,void *);
typedef void (*esp32_mquickjs_event_queue_close_fn)(void *);
typedef enum {ESP32_MQUICKJS_EVENT_QUEUE_DROP_NEWEST,ESP32_MQUICKJS_EVENT_QUEUE_DROP_OLDEST} esp32_mquickjs_event_queue_overflow_t;
