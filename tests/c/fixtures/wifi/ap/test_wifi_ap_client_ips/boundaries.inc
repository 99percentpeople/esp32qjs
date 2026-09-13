
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE -2
#define ESP_ERR_NOT_SUPPORTED -3
#define ESP32_MQUICKJS_WIFI_MAX_AP_CLIENTS 4
#define MACSTR "%02x:%02x:%02x:%02x:%02x:%02x"
#define MAC2STR(a) (a)[0],(a)[1],(a)[2],(a)[3],(a)[4],(a)[5]
typedef int esp_err_t;
typedef struct { uint32_t addr; } esp_ip4_addr_t;
typedef struct { uint8_t mac[6];esp_ip4_addr_t ip; } esp_netif_pair_mac_ip_t;
typedef struct { uint8_t mac[6];int rssi;bool phy_11b,phy_11g,phy_11n,phy_11a,phy_11ac,phy_11ax,phy_lr; } wifi_sta_info_t;
typedef struct { struct { int num;wifi_sta_info_t sta[4]; } stations;uint16_t aid[4]; } esp32_mquickjs_wifi_ap_clients_t;
static bool s_ap_cleanup_pending;
static int s_ap_lease;
static void *s_ap_netif=(void *)1;
static const char *s_ap_stage;
static int scenario,radio_calls,dhcp_calls,error_code;
static int esp32_mquickjs_wifi_radio_ap_clients(const void *lease,esp32_mquickjs_wifi_ap_clients_t *s) {
    assert(lease==&s_ap_lease);radio_calls++;memset(s,0,sizeof(*s));
    if(scenario==1)return -7;
    s->stations.num=scenario==2?0:2;
    for(int i=0;i<s->stations.num;i++) {
        s->stations.sta[i].mac[0]=2;s->stations.sta[i].mac[5]=i+1;
        s->stations.sta[i].rssi=-40-i;s->stations.sta[i].phy_11n=true;s->aid[i]=i?0:17;
    }
    return 0;
}
static int esp_netif_dhcps_get_clients_by_mac(void *netif,int num,esp_netif_pair_mac_ip_t *p) {
    assert(netif==(void *)1 && num==2);dhcp_calls++;
    assert(p[0].mac[0]==2 && p[0].mac[5]==1 && p[1].mac[5]==2);
    for(int i=0;i<4;i++)assert(!p[i].ip.addr);
    if(scenario==3)return -8;
    p[0].ip.addr=0x12345678; /* Missing second MAC deliberately leaves output unchanged. */
    return 0;
}
static char *esp_ip4addr_ntoa(const esp_ip4_addr_t *ip,char *text,int size) {
    assert(ip->addr==0x12345678 && size==16);
    if(scenario==4)return NULL;
    strcpy(text,"192.168.4.2");return text;
}
static JSValue wifi_ap_error(JSContext *ctx,const char *op,int code) {
    assert(!strcmp(op,"wifi.apClients"));error_code=code;
    return JS_ThrowTypeError(ctx,"native AP clients error");
}
