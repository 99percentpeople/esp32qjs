/* Execute the actual SDK metadata getter, then the production snapshot.
 * HMAC is a modeled boundary, not RF or scan-lifecycle emulation. */
extern void ieee80211_freedom_inside_cb(void *);
extern void ieee80211_get_tx_info_from_eb(void *, esp_80211_tx_info_t *);
static void *bound;
static unsigned allocated_hooks, completed_hooks, public_callbacks;
static unsigned completion_prefix, completion_status, expected_sequence;
static const u8 *expected_frame;
static unsigned expected_length;
static esp32_mquickjs_wifi_raw_tx_snapshot_t observed;
static int metadata_failure;
void *memcpy(void *out, const void *in, size_t length) {
    for (size_t i=0;i<length;i++) ((u8 *)out)[i]=((const u8 *)in)[i];
    return out;
}
void *esp32qjs_raw_tx_alloc(const void *bytes,int kind,int length) {
    bound=ic_ebuf_alloc(bytes,kind,length);
    if(bound) allocated_hooks++;
    return bound;
}
void esp32qjs_raw_tx_info(void *descriptor,esp_80211_tx_info_t *info) {
    if(descriptor!=bound) __builtin_trap();
    completed_hooks++;
    ieee80211_get_tx_info_from_eb(descriptor,info);
    if(!esp32_mquickjs_wifi_raw_tx_snapshot(info,123,&observed)) metadata_failure=1;
    else if(observed.interface!=ESP32_MQUICKJS_WIFI_RAW_TX_STATION) metadata_failure=2;
    else if(!observed.destination_available || memcmp(observed.destination,expected_frame+4,6)) metadata_failure=3;
    else if(!observed.source_available || memcmp(observed.source,expected_frame+10,6)) metadata_failure=4;
    else if(observed.raw_body_length!=expected_length-24) metadata_failure=5;
    else if(observed.status!=(completion_status==1 ? ESP32_MQUICKJS_WIFI_RAW_TX_DRIVER_SUCCESS :
        ESP32_MQUICKJS_WIFI_RAW_TX_DRIVER_FAILED)) metadata_failure=6;
}
static void completed(void *info) {
    (void)info;
    if(completed_hooks!=public_callbacks+1) __builtin_trap();
    public_callbacks++;
}
void fixture_post_hmac_tx(void *buffer) {
    static u32 lldesc[3];
    static u8 completed_frame[1508];
    if(buffer!=bound || allocated_hooks!=posted+1) __builtin_trap();
    if((tx_info[0]&1)!=expected_sequence) __builtin_trap();
    if(copied_length!=(int)expected_length || memcmp(copied,expected_frame,expected_length)) __builtin_trap();
    /* Model C5 completion descriptor layout reviewed against the SDK getter.
     * Include its optional 8-byte prefix removal and driver FCS accounting. */
    memcpy(completed_frame+completion_prefix,copied,(unsigned)copied_length);
    if(expected_sequence) {
        completed_frame[completion_prefix+22]=0x30;
        completed_frame[completion_prefix+23]=0x12;
    }
    lldesc[0]=(24+completion_prefix)<<14;
    lldesc[1]=(u32)completed_frame;
    eb[1]=(u32)lldesc;
    ((u16 *)eb)[10]=(u16)(24+completion_prefix);
    ((u16 *)eb)[20]=completion_prefix ? 0x2000 : 0;
    eb[6]=expected_length-24+4;
    ((u8 *)tx_info)[19]=(u8)completion_status;
    posted++;
    ieee80211_freedom_inside_cb(buffer);
}
