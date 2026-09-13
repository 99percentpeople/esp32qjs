
#define ESP32_MQUICKJS_FUTURE_SLOT_COUNT 1
typedef int esp32_mquickjs_cancel_result_t;
enum{ESP32_MQUICKJS_CANCELLED,ESP32_MQUICKJS_CANCEL_REQUESTED};
enum{FUTURE_STATE_QUEUED,FUTURE_STATE_PENDING,FUTURE_STATE_FULFILLED,FUTURE_STATE_REJECTED};
enum{FUTURE_KIND_DRIVER,FUTURE_KIND_SLEEP,FUTURE_KIND_TIMEOUT};
typedef struct{JSValue(*on_timeout)(JSContext*,void*,uint32_t);esp32_mquickjs_cancel_result_t(*cancel)(void*);}deadline_driver_t;
typedef struct{bool allocated,driver_active,cancel_requested;int state,kind;uint64_t submitted_us,deadline_us;
 deadline_driver_t*driver;void*driver_state;}future_slot_t;
typedef struct{future_slot_t*slots;}future_runtime_t;
typedef struct{int unused;}esp32_mquickjs_runtime_t;
static future_slot_t slot;static future_runtime_t state={.slots=&slot};static deadline_driver_t driver;
static unsigned cancelled,generic_errors;static JSValue callback_result,pending_exception,settled_value;
static future_runtime_t*future_runtime(esp32_mquickjs_runtime_t*r){(void)r;return &state;}
static int64_t esp_timer_get_time(void){return 100000;}
static bool future_is_terminal(int value){return value>=FUTURE_STATE_FULFILLED;}
static uint32_t esp32_mquickjs_future_elapsed_timeout_ms(uint64_t a,uint64_t b){return (b-a)/1000;}
static bool JS_IsException(JSValue value){return value==JS_EXCEPTION;}
static bool JS_HasException(JSContext*ctx){(void)ctx;return pending_exception!=0;}
static JSValue JS_GetException(JSContext*ctx){(void)ctx;JSValue v=pending_exception;pending_exception=0;return v;}
static void future_destroy_driver(future_slot_t*s){s->driver_active=false;}
static void future_settle(JSContext*ctx,future_slot_t*s,int terminal,JSValue value){(void)ctx;s->state=terminal;settled_value=value;}
static void future_reject_message(JSContext*ctx,future_slot_t*s,const char*message){(void)ctx;assert(strstr(message,"timed out"));++generic_errors;s->state=FUTURE_STATE_REJECTED;}
static JSValue timeout_callback(JSContext*ctx,void*p,uint32_t ms){(void)ctx;(void)p;assert(ms==10);return callback_result;}
static esp32_mquickjs_cancel_result_t cancel_callback(void*p){(void)p;assert(root_count==1);++cancelled;
 if(roots[0]->val==100)roots[0]->val=200;return ESP32_MQUICKJS_CANCELLED;}
static void setup(void){assert(!root_count);cancelled=generic_errors=0;pending_exception=0;settled_value=0;
 driver=(deadline_driver_t){timeout_callback,cancel_callback};slot=(future_slot_t){.allocated=true,.driver_active=true,
 .state=FUTURE_STATE_PENDING,.kind=FUTURE_KIND_DRIVER,.submitted_us=1000,.deadline_us=11000,.driver=&driver};}
