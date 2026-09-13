
typedef struct { struct { uint8_t channel; } ap; } wifi_config_t;
static int reopen_failure,reopen_begins,prepares,reopen_finishes,aborts;
static int esp32_mquickjs_wifi_radio_begin_ap_reopen(
    const esp32_mquickjs_wifi_radio_lease_t *app,const esp32_mquickjs_wifi_radio_lease_t *sta,
    const wifi_config_t *config,esp32_mquickjs_wifi_radio_lease_t *ap,
    esp32_mquickjs_wifi_radio_lifecycle_t *token) {
    assert(config && !s_ap_netif && !ap->acquired);reopen_begins++;
    if(reopen_failure==1)return -11;
    int err=esp32_mquickjs_wifi_radio_begin_lifecycle(app,sta,ap,token);
    if(!err)*ap=(esp32_mquickjs_wifi_radio_lease_t){9,3,true};return err;
}
static int esp32_mquickjs_wifi_radio_check_ap_reopen(const esp32_mquickjs_wifi_radio_lifecycle_t *t) { assert(exact(t));return 0; }
static int wifi_ap_prepare_netif(void) { assert(s_ap_coordinator.identity==41 && !s_ap_netif);prepares++;s_ap_netif=(void *)1;return reopen_failure==2?-12:0; }
static int esp32_mquickjs_wifi_radio_finish_ap_reopen(esp32_mquickjs_wifi_radio_lifecycle_t *t,uint8_t *primary) {
    assert(exact(t) && s_ap_netif);reopen_finishes++;
    if(reopen_failure==3)return -13;
    *primary=6;memset(t,0,sizeof(*t));memset(&native_token,0,sizeof(native_token));return 0;
}
static int esp32_mquickjs_wifi_radio_abort_ap_reopen(const esp32_mquickjs_wifi_radio_lifecycle_t *t) {
    assert(exact(t));aborts++;partial_quiesced=true;return 0;
}
