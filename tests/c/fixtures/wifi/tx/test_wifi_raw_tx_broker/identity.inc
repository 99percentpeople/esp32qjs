/* Inject SDK allocation/completion edges; broker and descriptor hooks are real. */
static uintptr_t descriptors[16];
static unsigned next_descriptor;
static void *last_descriptor;
void *esp32qjs_raw_tx_alloc(const void *, int, int);
void esp32qjs_raw_tx_info(void *, esp_80211_tx_info_t *);
void ieee80211_copy_eb_header(void *destination, void *source) {
    assert(destination && source && !critical_depth);
}
void *ic_ebuf_alloc(const void *bytes, int kind, int length) {
    assert(bytes && kind==1 && length==24);
    return &descriptors[next_descriptor++ % 16];
}
void ieee80211_get_tx_info_from_eb(void *descriptor, esp_80211_tx_info_t *info) {
    assert(descriptor); *info=callback_info;
}
static esp_err_t esp_wifi_80211_tx(wifi_interface_t interface,const void *bytes,int length,bool sequence) {
    assert(!critical_depth && interface==WIFI_IF_STA && length==24 && sequence);
    ++sends;driver_bytes=bytes;
    last_descriptor=esp32qjs_raw_tx_alloc(bytes,1,length);
    if(synchronous) { esp_80211_tx_info_t info;esp32qjs_raw_tx_info(last_descriptor,&info); }
    return send_error;
}
