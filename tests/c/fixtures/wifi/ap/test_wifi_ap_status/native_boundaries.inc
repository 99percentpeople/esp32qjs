
static struct {wifi_interface_t interface;uint32_t identity,generation;} s_tx_rate_lease;

#define ESP_ERR_INVALID_SIZE -7
#define ESP_WIFI_MAX_CONN_NUM 4
#define MALLOC_CAP_8BIT 1
/* SDK association-list boundary: only the consumed count is simulated here. */
typedef struct { int num; } wifi_sta_list_t;
typedef int wifi_second_chan_t;
static struct { int lock;unsigned generation;bool started;int event_live;
    const char *fault_stage,*cleanup_stage;struct { unsigned identity; } lifecycle;
    wifi_radio_live_lease_t leases[WIFI_RADIO_MAX_LEASES]; } s_radio;
static int depth,critical,calls,fail_at;
static bool fail_alloc,stop_during_channel;
static int bad_count,bad_length;
static void *allocation;static size_t allocation_size;
static wifi_mode_t native_mode;
static void wifi_radio_operation_lock(void) { assert(!depth && !critical);depth=1; }
static void wifi_radio_operation_unlock(void) { assert(depth && !critical);depth=0; }
#define taskENTER_CRITICAL(p) do { (void)(p);assert(!critical);critical=1; } while(0)
#define taskEXIT_CRITICAL(p) do { (void)(p);assert(critical);critical=0; } while(0)
static int boundary(void) { assert(depth && !critical);calls++;return calls==fail_at?-40-calls:0; }
static void *heap_caps_calloc(size_t count,size_t size,int caps) {
    assert(depth && !critical && count==1 && caps==1 && !allocation);
    if(fail_alloc)return NULL;allocation_size=size;return allocation=calloc(count,size);
}
static void heap_caps_free(void *p) {
    assert(depth && !critical && p==allocation);
    for(size_t i=0;i<allocation_size;i++)assert(!((unsigned char *)p)[i]);
    free(p);allocation=NULL;
}
static int esp_wifi_get_mode(wifi_mode_t *mode) { *mode=native_mode;return boundary(); }
static int esp_wifi_get_config(wifi_interface_t interface,wifi_config_t *config) {
    assert(interface==WIFI_IF_AP);memset(config,0,sizeof(*config));
    memcpy(config->ap.ssid,"A\0B\xff",4);config->ap.ssid_len=bad_length==1?33:bad_length==2?0:4;
    memset(config->ap.password,0x55,sizeof(config->ap.password));
    config->ap.ssid_hidden=true;config->ap.max_connection=4;config->ap.channel=11;
    config->ap.authmode=WIFI_AUTH_WPA2_PSK;return boundary();
}
static int esp_wifi_get_mac(wifi_interface_t interface,uint8_t mac[6]) {
    assert(interface==WIFI_IF_AP);memset(mac,0,6);mac[0]=2;return boundary();
}
static int esp_wifi_ap_get_sta_list(wifi_sta_list_t *list) { list->num=bad_count==1?-1:bad_count==2?5:2;return boundary(); }
static int esp_wifi_get_channel(uint8_t *primary,wifi_second_chan_t *secondary) {
    *primary=6;*secondary=0;if(stop_during_channel)s_radio.event_live=WIFI_MODE_STA;return boundary();
}
