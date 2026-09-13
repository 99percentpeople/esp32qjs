
typedef int esp32_mquickjs_runtime_t;
static esp32_mquickjs_runtime_t runtime;
static bool active=true;static unsigned providers;static int64_t clock_us;
static esp32_mquickjs_runtime_t *esp32_mquickjs_get_active_runtime(void){return active?&runtime:NULL;}
static int64_t esp_timer_get_time(void){return ++clock_us;}
static void esp32_mquickjs_reset_event_queue_counters(void *r){assert(r==&runtime);providers++;}
static void esp32_mquickjs_wifi_watch_reset_counters(void){providers++;}
static void esp32_mquickjs_wifi_reset_connection_counters(void){providers++;}
static uint32_t esp32_mquickjs_wifi_monitor_reset_counters(void){providers++;return 2;}
static uint32_t esp32_mquickjs_wifi_csi_reset_counters(void){providers++;return 3;}
static void esp32_mquickjs_memory_reset_counters(void){providers++;}
