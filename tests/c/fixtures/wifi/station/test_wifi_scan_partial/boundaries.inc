typedef enum { WIFI_FUTURE_SCAN, WIFI_FUTURE_CONNECT, WIFI_FUTURE_DISCONNECT } wifi_future_kind_t;
typedef int esp32_mquickjs_future_poll_t;
#define ESP32_MQUICKJS_FUTURE_READY 1
#define ESP32_MQUICKJS_FUTURE_PENDING 0
#define ESP32_MQUICKJS_WIFI_CONNECT_EVENT_KIND_DISCONNECTED 1
#define ESP32_MQUICKJS_WIFI_CONNECT_EVENT_KIND_SUCCESS 2
#define ESP32_MQUICKJS_WIFI_CONNECT_EVENT_KIND_TIMEOUT 3
#define ESP_ERR_INVALID_STATE 259
#define ESP_ERR_TIMEOUT 263
#define ESP_FAIL -1
#define pdTRUE 1
typedef struct { unsigned generation,status; } esp32_mquickjs_wifi_scan_event_t;
typedef struct {
    unsigned generation,kind;int reason;uint64_t completed_us;
    esp32_mquickjs_wifi_link_snapshot_t link;
} esp32_mquickjs_wifi_connect_event_t;
typedef struct { void *scan_queue,*connect_queue; } esp32_mquickjs_wifi_state_t;
typedef struct {
    wifi_future_kind_t kind;
    bool started,completed,cancel_requested;
    unsigned generation,scan_status,connect_kind;int connect_reason;
    uint64_t connect_start_us,connect_completed_us;
    esp32_mquickjs_wifi_link_snapshot_t connect_link;
    uint16_t scan_max_records;
} esp32_mquickjs_future_driver_state_t;
static esp32_mquickjs_wifi_state_t test_wifi={.scan_queue=(void *)1};
static bool terminal_queued;
static unsigned terminal_status,terminal_generation;
static int stop_error,stop_calls,cancel_calls;
static esp32_mquickjs_wifi_state_t *esp32_mquickjs_wifi_state(void) { return &test_wifi; }
static int xQueueReceive(void *q,void *out,int wait) {
    assert(q==(void *)1 && !wait);
    if(!terminal_queued)return 0;
    *(esp32_mquickjs_wifi_scan_event_t *)out=(esp32_mquickjs_wifi_scan_event_t){.generation=terminal_generation,.status=terminal_status};
    terminal_queued=false;if(terminal_generation==7)stopped_or_complete=true;return pdTRUE;
}
static int esp32_mquickjs_wifi_stop_scan_for_results(uint32_t generation) {
    assert(generation==7);stop_calls++;if(!stop_error)stopped_or_complete=true;return stop_error;
}
static int esp32_mquickjs_wifi_cancel_scan(uint32_t generation) {assert(generation==7);cancel_calls++;return 0;}
static JSValue esp32_mquickjs_wifi_make_scan_results_array(JSContext *ctx,uint16_t count) {return wifi_make_scan_results_array(ctx,count);}
static JSValue esp32_mquickjs_wifi_throw_scan_error(JSContext *ctx,int error) {return JS_ThrowInternalError(ctx,"scan SDK error %d",error);}
static JSValue esp32_mquickjs_wifi_throw_operation_error(JSContext *ctx,const char *code,const char *op,int error,int reason,unsigned status) {
    (void)op;(void)error;(void)reason;(void)status;return JS_ThrowInternalError(ctx,"%s",code);
}
static void esp32_mquickjs_wifi_clear_connect_future(void) {assert(0);}
static JSValue esp32_mquickjs_wifi_make_status_object(JSContext *ctx) {(void)ctx;assert(0);return JS_NULL;}
static JSValue esp32_mquickjs_wifi_make_connect_result(JSContext *ctx,const esp32_mquickjs_wifi_link_snapshot_t *s,double ms) {(void)ctx;(void)s;(void)ms;assert(0);return JS_NULL;}
