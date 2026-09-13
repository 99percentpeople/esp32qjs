
static struct {
    int lock;
    bool restart_required,event_fence_posted,event_fence_seen;
    uint32_t event_identity,event_revision;
    uint8_t event_phase,event_expected,event_seen,event_live;
} s_radio;
static unsigned critical,posts,ticks,script_at,script_count;
static int script[24],post_error;
static bool cancelled,late_stop,late_start;
#define taskENTER_CRITICAL(p) do {(void)(p);assert(!critical);++critical;} while(0)
#define taskEXIT_CRITICAL(p) do {(void)(p);assert(critical==1);--critical;} while(0)
static TickType_t xTaskGetTickCount(void) {return ticks;}
static TickType_t esp32_mquickjs_wifi_wait_remaining(TickType_t t) {return t;}
static void vTaskDelay(TickType_t t) {assert(!critical);ticks+=t;}
static bool esp32_mquickjs_cooperate(esp32_mquickjs_runtime_t *runtime);
static int esp_event_post(esp_event_base_t base,int id,const void *data,size_t size,TickType_t wait);
