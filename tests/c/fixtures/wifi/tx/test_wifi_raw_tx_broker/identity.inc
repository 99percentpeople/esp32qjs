/* Inject SDK allocation/completion edges; broker and descriptor hooks are real. */
typedef struct {
    void *words[16];
    uint8_t tx_info[32];
} descriptor_fixture_t;
static descriptor_fixture_t descriptors[16];
#if CONFIG_IDF_TARGET_ESP32C5
#define FIXTURE_TXINFO_WORD 14
#else
#define FIXTURE_TXINFO_WORD 11
#endif
static unsigned next_descriptor;
static void *last_descriptor;
void *esp32qjs_raw_tx_alloc(const void *, int, int);
void esp32qjs_raw_tx_info(void *, esp_80211_tx_info_t *);
void ieee80211_copy_eb_header(void *destination, void *source) {
    assert(destination && source && !critical_depth);
    descriptor_fixture_t *out=destination,*in=source;
    memcpy(out->tx_info,in->tx_info,sizeof(out->tx_info));
    out->words[FIXTURE_TXINFO_WORD]=out->tx_info;
}
void *ic_ebuf_alloc(const void *bytes, int kind, int length) {
    assert(bytes && kind==1 && length==24);
    descriptor_fixture_t *descriptor=&descriptors[next_descriptor++ % 16];
    memset(descriptor,0,sizeof(*descriptor));
    descriptor->words[FIXTURE_TXINFO_WORD]=descriptor->tx_info;
    descriptor->tx_info[19]=callback_info.tx_status==WIFI_SEND_SUCCESS ? 1 : 4;
    return descriptor;
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
