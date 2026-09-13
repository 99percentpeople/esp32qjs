
#define ACCEPT ESP32_MQUICKJS_WIFI_RX_FILTER_ACCEPT
#define EVAL() esp32_mquickjs_wifi_rx_filter_evaluate(&filter,&state,&view,now)
static void packet(uint16_t fc,unsigned length,wifi_promiscuous_pkt_type_t type,
                   uint8_t error,esp32_mquickjs_wifi_rx_target_view_t *view) {
    wifi_pkt_rx_ctrl_t rx;memset(&rx,0,sizeof(rx));
    rx.sig_len=length;rx.rssi=-60;rx.channel=6;rx.rx_state=error;
#if CONFIG_SOC_WIFI_HE_SUPPORT
    rx.dump_len=length;
#endif
    uint8_t buffer[sizeof(rx)+64];assert(length<=64);
    memcpy(buffer,&rx,sizeof(rx));uint8_t *bytes=buffer+sizeof(rx);memset(bytes,0,length);
    if(length>=2){bytes[0]=fc;bytes[1]=fc>>8;}
    if(length>=24){memset(bytes+4,1,6);memset(bytes+10,2,6);memset(bytes+16,3,6);}
    if(length>=30)memset(bytes+24,4,6);
    if(length==10)memset(bytes+4,1,6);
    assert(esp32_mquickjs_wifi_rx_target_view(buffer,type,view)==
           (type==WIFI_PKT_MISC?ESP32_MQUICKJS_WIFI_RX_SPAN_METADATA_ONLY:ESP32_MQUICKJS_WIFI_RX_SPAN_PACKET));
    /* Filter has no permission to follow the now-expired callback pointer. */
    view->bytes=NULL;
}
static void mac(esp32_mquickjs_wifi_rx_mac_filter_t *list,uint8_t octet) {
    memset(list,0,sizeof(*list));list->count=1;memset(list->values[0],octet,6);
}
int main(void) {
    esp32_mquickjs_wifi_rx_filter_t filter={.type_mask=15,.sample_every=1,.valid_only=true};
    esp32_mquickjs_wifi_rx_filter_state_t state={0};
    esp32_mquickjs_wifi_rx_target_view_t view;
    uint64_t now=0;
    packet(0x0008,24,WIFI_PKT_DATA,0,&view);
    assert(esp32_mquickjs_wifi_rx_filter_valid(&filter) && EVAL()==ACCEPT);
    assert(!esp32_mquickjs_wifi_rx_filter_valid(NULL));
    assert(esp32_mquickjs_wifi_rx_filter_evaluate(NULL,&state,&view,now)==ESP32_MQUICKJS_WIFI_RX_FILTER_INVALID_CONFIG);
    assert(esp32_mquickjs_wifi_rx_filter_evaluate(&filter,NULL,&view,now)==ESP32_MQUICKJS_WIFI_RX_FILTER_INVALID_CONFIG);
    assert(esp32_mquickjs_wifi_rx_filter_evaluate(&filter,&state,NULL,now)==ESP32_MQUICKJS_WIFI_RX_FILTER_INVALID_CALLBACK);
    esp32_mquickjs_wifi_rx_filter_state_t before;memcpy(&before,&state,sizeof(state));
    filter.source.count=9;assert(EVAL()==ESP32_MQUICKJS_WIFI_RX_FILTER_INVALID_CONFIG);filter.source.count=0;
    filter.destination.count=255;assert(EVAL()==ESP32_MQUICKJS_WIFI_RX_FILTER_INVALID_CONFIG);filter.destination.count=0;
    filter.bssid.count=9;assert(EVAL()==ESP32_MQUICKJS_WIFI_RX_FILTER_INVALID_CONFIG);filter.bssid.count=0;
    filter.type_mask=16;assert(EVAL()==ESP32_MQUICKJS_WIFI_RX_FILTER_INVALID_CONFIG);filter.type_mask=15;
    filter.sample_every=0;assert(EVAL()==ESP32_MQUICKJS_WIFI_RX_FILTER_INVALID_CONFIG);filter.sample_every=1;
    filter.maximum_rate_hz=1000001;assert(EVAL()==ESP32_MQUICKJS_WIFI_RX_FILTER_INVALID_CONFIG);filter.maximum_rate_hz=0;
    assert(!memcmp(&before,&state,sizeof(state)));
    filter.type_mask=0;assert(EVAL()==ESP32_MQUICKJS_WIFI_RX_FILTER_TYPE);filter.type_mask=1;
    assert(EVAL()==ESP32_MQUICKJS_WIFI_RX_FILTER_TYPE);filter.type_mask=4;assert(EVAL()==ACCEPT);
    filter.subtype_filter=true;filter.subtype_mask=0;assert(EVAL()==ESP32_MQUICKJS_WIFI_RX_FILTER_SUBTYPE);
    filter.subtype_mask=1;assert(EVAL()==ACCEPT);filter.subtype_filter=false;
    filter.minimum_rssi_set=true;filter.minimum_rssi=-59;assert(EVAL()==ESP32_MQUICKJS_WIFI_RX_FILTER_RSSI);
    filter.minimum_rssi=-60;assert(EVAL()==ACCEPT);filter.minimum_rssi_set=false;

    /* OR within a bounded MAC list, AND across roles, using actual DS parser. */
    const uint8_t source[]={2,2,3,4},destination[]={1,3,1,3},bssid[]={3,1,2,0};
    for(unsigned ds=0;ds<4;ds++) {
        packet(0x0008|(ds<<8),ds==3?30:24,WIFI_PKT_DATA,0,&view);
        mac(&filter.source,source[ds]);mac(&filter.destination,destination[ds]);
        mac(&filter.bssid,bssid[ds]);
        if(ds==3){assert(EVAL()==ESP32_MQUICKJS_WIFI_RX_FILTER_MAC);filter.bssid.count=0;}
        assert(EVAL()==ACCEPT);
        memset(filter.source.values[0],9,6);assert(EVAL()==ESP32_MQUICKJS_WIFI_RX_FILTER_MAC);
        filter.source.count=8;memset(filter.source.values[7],source[ds],6);assert(EVAL()==ACCEPT);
    }
    memset(&filter.source,0,sizeof(filter.source));memset(&filter.destination,0,sizeof(filter.destination));
    memset(&filter.bssid,0,sizeof(filter.bssid));filter.type_mask=15;
    packet(0x00d4,10,WIFI_PKT_CTRL,0,&view);mac(&filter.source,0);
    assert(EVAL()==ESP32_MQUICKJS_WIFI_RX_FILTER_MAC);filter.source.count=0;
    mac(&filter.destination,1);assert(EVAL()==ACCEPT);filter.destination.count=0;
    packet(0x0008,24,WIFI_PKT_DATA,7,&view);assert(EVAL()==ESP32_MQUICKJS_WIFI_RX_FILTER_RX_ERROR);
    filter.valid_only=false;assert(EVAL()==ACCEPT);filter.valid_only=true;
    packet(0x0008,23,WIFI_PKT_DATA,0,&view);assert(EVAL()==ESP32_MQUICKJS_WIFI_RX_FILTER_INVALID_HEADER);
    filter.valid_only=false;assert(EVAL()==ACCEPT);
    mac(&filter.source,2);assert(EVAL()==ESP32_MQUICKJS_WIFI_RX_FILTER_MAC);filter.source.count=0;
    packet(0x0080,24,WIFI_PKT_DATA,0,&view);filter.subtype_filter=true;filter.subtype_mask=0xffff;
    assert(EVAL()==ESP32_MQUICKJS_WIFI_RX_FILTER_SUBTYPE);filter.subtype_filter=false;
    packet(0,0,WIFI_PKT_MISC,0,&view);assert(EVAL()==ACCEPT);
    filter.valid_only=true;assert(EVAL()==ESP32_MQUICKJS_WIFI_RX_FILTER_INVALID_HEADER);
    packet(0x0008,24,WIFI_PKT_DATA,0,&view);
    view.metadata.type=(esp32_mquickjs_wifi_packet_type_t)-1;
    assert(EVAL()==ESP32_MQUICKJS_WIFI_RX_FILTER_INVALID_CALLBACK);
    packet(0x0008,24,WIFI_PKT_DATA,0,&view);view.status=ESP32_MQUICKJS_WIFI_RX_SPAN_INVALID_LENGTH;
    assert(EVAL()==ESP32_MQUICKJS_WIFI_RX_FILTER_INVALID_CALLBACK);

    packet(0x0008,24,WIFI_PKT_DATA,0,&view);state=(esp32_mquickjs_wifi_rx_filter_state_t){0};
    filter.sample_every=3;
    for(unsigned i=0;i<30;i++)assert(EVAL()==(i%3==0?ACCEPT:ESP32_MQUICKJS_WIFI_RX_FILTER_DECIMATION));
    assert(state.sample_phase==0);
    filter.type_mask=0;memcpy(&before,&state,sizeof(state));assert(EVAL()==ESP32_MQUICKJS_WIFI_RX_FILTER_TYPE);
    assert(!memcmp(&before,&state,sizeof(state)));filter.type_mask=15;
    filter.sample_every=UINT32_MAX;state.sample_phase=UINT32_MAX-1;
    assert(EVAL()==ESP32_MQUICKJS_WIFI_RX_FILTER_DECIMATION && state.sample_phase==0);
    assert(EVAL()==ACCEPT && state.sample_phase==1);
    filter.sample_every=1;assert(EVAL()==ESP32_MQUICKJS_WIFI_RX_FILTER_INVALID_CONFIG);
    state=(esp32_mquickjs_wifi_rx_filter_state_t){0};filter.maximum_rate_hz=3;
    now=UINT64_C(1)<<40;assert(EVAL()==ACCEPT);
    now+=333333;assert(EVAL()==ESP32_MQUICKJS_WIFI_RX_FILTER_RATE);
    now++;assert(EVAL()==ACCEPT); /* ceiling, not floor interval */
    uint64_t admitted=now;now--;assert(EVAL()==ESP32_MQUICKJS_WIFI_RX_FILTER_RATE);
    assert(state.last_admitted_us==admitted);
    now=admitted+333334;assert(EVAL()==ACCEPT);
    filter.maximum_rate_hz=1000000;assert(EVAL()==ESP32_MQUICKJS_WIFI_RX_FILTER_RATE);
    now++;assert(EVAL()==ACCEPT);filter.maximum_rate_hz=0;assert(EVAL()==ACCEPT);
    /* Independently zeroed subscriber state starts its own rate/decimation. */
    state=(esp32_mquickjs_wifi_rx_filter_state_t){0};filter.maximum_rate_hz=1;now=0;assert(EVAL()==ACCEPT);
    now=999999;assert(EVAL()==ESP32_MQUICKJS_WIFI_RX_FILTER_RATE);now=1000000;assert(EVAL()==ACCEPT);
}
