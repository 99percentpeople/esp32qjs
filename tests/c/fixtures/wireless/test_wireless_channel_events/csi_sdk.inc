
typedef struct { atomic_int invalid_callback_data; } counters_t;
typedef struct { counters_t counters;atomic_bool accepting,identity_exhausted; } resources_t;
#define ESP32_MQUICKJS_WIFI_CSI_PUBLISH_IDENTITY_EXHAUSTED 9
typedef struct { int channel;uint64_t timestamp_us;uint32_t radio_generation; } esp32_mquickjs_wifi_csi_metadata_t;
typedef struct { uint8_t *buf;unsigned len,payload_len;struct {unsigned sig_len;} rx_ctrl; } wifi_csi_info_t;
typedef struct {const uint8_t *bytes;size_t readable_bytes;} esp32_mquickjs_wifi_csi_rx_packet_t;
typedef struct {const uint8_t *bytes;size_t copied_length;uint32_t driver_packet_length,driver_payload_length;} esp32_mquickjs_wifi_csi_packet_input_t;
static bool esp32_mquickjs_wifi_csi_rx_native_take(const wifi_csi_info_t *info,esp32_mquickjs_wifi_csi_rx_packet_t *packet){
    (void)info;memset(packet,0,sizeof(*packet));return false;
}
typedef struct {
    esp32_mquickjs_wifi_radio_lease_t radio_lease;atomic_int lifecycle;atomic_bool channel_conflicted;
    resources_t *resources;resources_t storage;struct { int capture;bool fixed_channel;uint8_t channel; } options;
    uint8_t effective_channel;int effective_secondary;unsigned radio_generation;
    int last_error;const char *last_error_code,*last_error_stage;
} wifi_csi_session_t;
static unsigned published,leaves;
static uint64_t published_time;static uint32_t published_generation;
static int64_t esp_timer_get_time(void) { return INT64_C(0x300000064); }
static bool esp32_mquickjs_wifi_csi_callback_enter(resources_t *r) { return atomic_load(&r->accepting); }
static void esp32_mquickjs_wifi_csi_callback_leave(resources_t *r) { (void)r;leaves++; }
static void esp32_mquickjs_wifi_csi_resources_set_accepting(resources_t *r,bool v) { atomic_store(&r->accepting,v); }
static void esp32_mquickjs_wifi_csi_target_normalize_metadata(void *i,void *c,void *m) { (void)i;(void)c;((esp32_mquickjs_wifi_csi_metadata_t *)m)->channel=11; }
static int wifi_csi_publish_event;
static int esp32_mquickjs_wifi_csi_callback_publish(resources_t *r,void *m,const uint8_t *b,unsigned len,const esp32_mquickjs_wifi_csi_packet_input_t *packet,int f,void *p) { (void)r;(void)b;(void)len;(void)packet;(void)f;(void)p;esp32_mquickjs_wifi_csi_metadata_t *meta=m;published_time=meta->timestamp_us;published_generation=meta->radio_generation;published++;return 0; }
