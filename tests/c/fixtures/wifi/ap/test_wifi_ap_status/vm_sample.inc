
static int esp32_mquickjs_wifi_radio_sample_ap(const void *lease,esp32_mquickjs_wifi_ap_snapshot_t *s,const char **stage) {
    assert(lease==(scenario==3?NULL:&s_ap_lease));queries++;
    memset(s,0,sizeof(*s));s->started=true;
    if(scenario==2 || scenario==3){*stage=scenario==3?"admission":"config";return -27;}
    s->ssid[0]='A';s->ssid[1]=0;s->ssid[2]=255;s->ssid_len=3;s->hidden=true;
    if(scenario==4){s->ssid[2]=0xc3;s->ssid[3]=0xa9;s->ssid_len=4;}
    s->channel=6;s->max_connections=4;s->client_count=2;s->mac[0]=2;s->authmode=WIFI_AUTH_WPA2_PSK;
    *stage=NULL;return 0;
}
