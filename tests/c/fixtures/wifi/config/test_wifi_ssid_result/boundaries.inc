
#include "cutils.h"
#define ESP_ERR_INVALID_RESPONSE -7
#define ESP_OK 0
typedef int wifi_phy_mode_t;
enum {WIFI_PHY_MODE_LR,WIFI_PHY_MODE_11B,WIFI_PHY_MODE_11G,WIFI_PHY_MODE_11A,
    WIFI_PHY_MODE_HT20,WIFI_PHY_MODE_HT40,WIFI_PHY_MODE_HE20,WIFI_PHY_MODE_VHT20};
typedef struct {bool valid;uint8_t ssid[32],ssid_len,bssid[6],channel;uint16_t aid;} esp32_mquickjs_wifi_link_snapshot_t;
typedef struct {bool rssi_valid,phy_valid;int rssi;wifi_phy_mode_t phy;} esp32_mquickjs_wifi_link_sample_t;
static struct {int radio_lease;} s_wifi_state;
static int esp32_mquickjs_wifi_radio_sample_station_link(const int *lease,const uint8_t *bssid,
    uint8_t channel,esp32_mquickjs_wifi_link_sample_t *sample) {
    assert(lease==&s_wifi_state.radio_lease && bssid[0]==2 && channel==6);
    memset(sample,0,sizeof(*sample));return -1;
}
static JSValue esp32_mquickjs_wifi_throw_operation_error(JSContext *ctx,const char *code,
    const char *operation,int error,int reason,uint32_t status) {
    assert(!strcmp(code,"WIFI_CONNECT_FAILED") && !strcmp(operation,"wifi.connect"));
    assert(error==ESP_ERR_INVALID_RESPONSE);(void)reason;(void)status;
    return JS_ThrowTypeError(ctx,"invalid connection snapshot");
}
