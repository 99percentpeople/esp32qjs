
#define CONFIG_IDF_TARGET "esp32c5"
static int64_t clock_us;
static void (*worker)(void *);
static void *worker_arg;
static bool worker_full;
static int64_t esp_timer_get_time(void) {return clock_us;}
static bool esp32_mquickjs_submit_background_worker(void (*fn)(void *),void *arg) {
    assert(!critical);if(worker_full || worker)return false;worker=fn;worker_arg=arg;return true;
}
static void run_worker(void) {
    assert(worker);void (*fn)(void *)=worker;void *arg=worker_arg;worker=NULL;worker_arg=NULL;fn(arg);
}
typedef unsigned portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
static bool retired_locked;
#define portENTER_CRITICAL(p) do{(void)(p);assert(!retired_locked);retired_locked=true;}while(0)
#define portEXIT_CRITICAL(p) do{(void)(p);assert(retired_locked);retired_locked=false;}while(0)
static const char *esp_err_to_name(int error) {(void)error;return "injected";}
static JSValue twt_get_property(JSContext *ctx,JSValue object,const char *name) {
    JSGCRef ref;JSValue *root=JS_PushGCRef(ctx,&ref);*root=object;
    JSValue value=step(ctx)?JS_ThrowOutOfMemory(ctx):JS_GetPropertyStr(ctx,*root,name);
    JS_PopGCRef(ctx,&ref);return value;
}
#define JS_GetPropertyStr twt_get_property
