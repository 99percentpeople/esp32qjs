
static struct { void *connect_queue;bool connect_future_registered;int connect_future_token;uint32_t connect_generation; } s_wifi_state;
static int64_t esp_timer_get_time(void) {return 12345;}
static _Atomic(esp32_mquickjs_runtime_t *) s_wifi_runtime;
static int writes,wakes,notifications;
static void wifi_lock(void) {}
static void wifi_unlock(void) {}
static void xQueueOverwrite(void *q,const void *e) { assert(q && e);writes++; }
static esp32_mquickjs_runtime_t *esp32_mquickjs_get_active_runtime(void) { return (void *)1; }
static int esp32_mquickjs_future_wake(void *r,int token) { assert(r && token);wakes++;s_wifi_runtime=NULL;return 0; }
static void esp32_mquickjs_notify_activity(void *r) { assert(r);notifications++; }
