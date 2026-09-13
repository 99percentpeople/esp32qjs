
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
typedef struct { uint32_t filter_mask; } wifi_promiscuous_filter_t;
typedef void (*wifi_promiscuous_cb_t)(void *,wifi_promiscuous_pkt_type_t);
static bool native_enabled;
static uint32_t native_packet=17,native_control=0x0d800000;
static wifi_promiscuous_cb_t native_callback;
static unsigned calls,failure1,failure2,packet_sets,control_sets,enable_sets,callback_sets;
static int boundary(void) {
    assert(!critical_depth);++calls;
    return calls==failure1 || calls==failure2 ? -(100+(int)calls) : ESP_OK;
}
/* Setters deliberately mutate even when returning an error. */
int esp_wifi_set_promiscuous(bool value) { int e=boundary();++enable_sets;native_enabled=value;return e; }
int esp_wifi_get_promiscuous(bool *value) { int e=boundary();if(!e)*value=native_enabled;return e; }
int esp_wifi_set_promiscuous_filter(const wifi_promiscuous_filter_t *value) { int e=boundary();++packet_sets;native_packet=value->filter_mask;return e; }
int esp_wifi_get_promiscuous_filter(wifi_promiscuous_filter_t *value) { int e=boundary();if(!e)value->filter_mask=native_packet;return e; }
int esp_wifi_set_promiscuous_ctrl_filter(const wifi_promiscuous_filter_t *value) { int e=boundary();++control_sets;native_control=value->filter_mask;return e; }
int esp_wifi_get_promiscuous_ctrl_filter(wifi_promiscuous_filter_t *value) { int e=boundary();if(!e)value->filter_mask=native_control;return e; }
int esp_wifi_set_promiscuous_rx_cb(wifi_promiscuous_cb_t value) { int e=boundary();++callback_sets;native_callback=value;return e; }
int64_t esp_timer_get_time(void) { return INT64_C(1)<<42; }
