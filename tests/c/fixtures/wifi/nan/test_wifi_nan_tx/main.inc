
static void *allocate(void){return esp32qjs_wifi_nan_alloc_sdf(1,2,3,4,5);}
static void identify(void *b,uint8_t id){uint8_t *m;memcpy(&m,(uint8_t*)b+56,sizeof(m));m[8]=id;esp32_mquickjs_wifi_nan_tx_submitting(b);}
static void retire(void *b){uint32_t ticket=esp32_mquickjs_wifi_nan_tx_recycling(b);assert(ticket);esp32_mquickjs_wifi_nan_tx_recycled(ticket);}
int main(void) {
    fail_heap=true;assert(esp32_mquickjs_wifi_nan_tx_open()==ESP_ERR_NO_MEM&&!heap_live&&!native_calls);
    fail_heap=false;assert(!esp32_mquickjs_wifi_nan_tx_open()&&heap_live==1);
    fail_native=true;assert(!allocate());fail_native=false;
    esp32_mquickjs_wifi_nan_tx_status_t status;esp32_mquickjs_wifi_nan_tx_status(&status);assert(!status.tracked);
    next_buffer=0;void *a=allocate(),*b=allocate();assert(a&&b&&a!=b);identify(a,7);
    assert(!esp32_mquickjs_wifi_nan_tx_service_drained(7));
    assert(!esp32_mquickjs_wifi_nan_tx_service_drained(99)); /* unsubmitted b */
    identify(b,8);assert(esp32_mquickjs_wifi_nan_tx_service_drained(99));
    uint32_t old=esp32_mquickjs_wifi_nan_tx_recycling(a);assert(old);
    assert(!esp32_mquickjs_wifi_nan_tx_service_drained(7)); /* real recycle has not returned */
    next_buffer=0;void *reused=allocate();assert(reused==a);identify(reused,9);
    esp32_mquickjs_wifi_nan_tx_recycled(old);
    assert(esp32_mquickjs_wifi_nan_tx_service_drained(7)&&!esp32_mquickjs_wifi_nan_tx_service_drained(9));
    esp32_mquickjs_wifi_nan_tx_recycled(old); /* stale completion cannot release reused address */
    assert(!esp32_mquickjs_wifi_nan_tx_service_drained(9));retire(reused);retire(b);
    assert(!esp32_mquickjs_wifi_nan_tx_close()&&!heap_live);

    assert(!esp32_mquickjs_wifi_nan_tx_open());void *list[NAN_TX_CAPACITY];
    for(unsigned i=0;i<NAN_TX_CAPACITY;i++){list[i]=allocate();assert(list[i]);}
    unsigned before=native_calls;assert(!allocate()&&native_calls==before);
    esp32_mquickjs_wifi_nan_tx_status(&status);assert(status.tracked==NAN_TX_CAPACITY&&status.unidentified==NAN_TX_CAPACITY&&status.rejected==1);
    esp32_mquickjs_wifi_nan_tx_seal();assert(!allocate()&&native_calls==before);
    assert(esp32_mquickjs_wifi_nan_tx_close()==ESP_ERR_TIMEOUT&&heap_live==1);
    for(unsigned i=0;i<NAN_TX_CAPACITY;i++)retire(list[i]);
    assert(!esp32_mquickjs_wifi_nan_tx_close()&&!heap_live);

    assert(!esp32_mquickjs_wifi_nan_tx_open());a=allocate();identify(a,7);identify(a,9);
    esp32_mquickjs_wifi_nan_tx_status(&status);assert(status.error==ESP_ERR_INVALID_RESPONSE);
    retire(a);assert(!esp32_mquickjs_wifi_nan_tx_service_drained(10));
    assert(!esp32_mquickjs_wifi_nan_tx_close()&&!heap_live);

    s_nan_tx_next_ticket=UINT32_MAX;assert(!esp32_mquickjs_wifi_nan_tx_open());a=allocate();assert(a);
    assert(!allocate());esp32_mquickjs_wifi_nan_tx_status(&status);assert(status.identity_exhausted);
    retire(a);assert(!esp32_mquickjs_wifi_nan_tx_close()&&!heap_live);
    assert(esp32_mquickjs_wifi_nan_tx_open()==ESP_ERR_NO_MEM&&!heap_live&&!locked);
}
