
#define ESP32_MQUICKJS_WIFI_BTWT_FENCE_EVENT 6
static void esp32_mquickjs_wifi_btwt_setup_observe_fence(const void *event){(void)event;assert(0);}
typedef const char *esp_event_base_t;
#define taskENTER_CRITICAL(lock) portENTER_CRITICAL_SAFE(lock)
#define taskEXIT_CRITICAL(lock) portEXIT_CRITICAL_SAFE(lock)
#define RADIO_EVENTS_IDLE 0
static struct { unsigned lock,event_phase,event_identity,event_revision;
    bool event_fence_posted,event_fence_seen; } s_radio;
typedef struct { uint32_t identity,revision; } wifi_radio_event_fence_t;
typedef struct { uint32_t token,revision; } wifi_radio_action_fence_t;
static struct { bool fence_posted; uint32_t posted_revision,lane; } s_action;
static bool esp32_mquickjs_wifi_action_event_fenced(uint32_t *lane,const uint32_t *token,uint32_t revision) {
    (void)lane;(void)token;(void)revision;assert(0);return false;
}
