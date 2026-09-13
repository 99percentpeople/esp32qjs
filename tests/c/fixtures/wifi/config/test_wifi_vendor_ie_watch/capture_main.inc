
int main(void) {
    uint8_t bytes[257]={221,255,1,2,3,4},address[6]={2,3,4,5,6,7};bytes[256]=255;
    esp32_mquickjs_event_queue_t queues[4]={0};vendor_watch_source_t sources[4]={0};
    esp32_mquickjs_wifi_vendor_ie_watch_capture(7,0,(void *)1,(void *)1,-42);
    s_vendor_watch_mutex=&s_vendor_watch_mutex_storage;
    for(unsigned i=0;i<4;++i){sources[i].queue=&queues[i];s_vendor_sources[i]=&sources[i];}
    sources[1].oui_count=1;sources[1].oui[0][0]=99;
    sources[2].oui_count=1;memcpy(sources[2].oui[0],bytes+2,3);queues[3].full=true;
    esp32_mquickjs_wifi_vendor_ie_watch_capture(7,0,address,(void *)bytes,-42);
    assert(queues[0].count==1 && !queues[1].count && queues[2].count==1 && !queues[3].count);
    assert(last.length==257 && last.sequence==1 && last.generation==7 && last.timestamp_us==123);
    assert(last.rssi==-42 && !memcmp(last.bytes,bytes,257) && !memcmp(last.address,address,6));
    memset(bytes,0,sizeof(bytes));assert(last.bytes[256]==255 && last.bytes[0]==221);
    assert(atomic_load(&s_vendor_watch_filtered)==1 && atomic_load(&s_vendor_watch_dropped)==1);
    lock_busy=true;esp32_mquickjs_wifi_vendor_ie_watch_capture(7,0,(void *)1,(void *)1,-42);lock_busy=false;
    assert(atomic_load(&s_vendor_watch_busy)==1 && s_vendor_watch_sequence==1);
    for(unsigned i=0;i<3;++i)esp32_mquickjs_wifi_vendor_ie_watch_capture(7,i==0?99:0,i==1?NULL:address,(void *)bytes,-42);
    assert(atomic_load(&s_vendor_watch_invalid)==3 && s_vendor_watch_sequence==1);
    bytes[0]=221;bytes[1]=4;now=-1;
    esp32_mquickjs_wifi_vendor_ie_watch_capture(7,0,address,(void *)bytes,-42);
    assert(atomic_load(&s_vendor_watch_invalid)==4);now=123;
    s_vendor_watch_sequence=VENDOR_WATCH_MAX_SEQUENCE;
    esp32_mquickjs_wifi_vendor_ie_watch_capture(7,0,address,(void *)bytes,-42);assert(queues[0].count==1);
    vendor_watch_closed(&sources[0]);assert(!s_vendor_sources[0]);
    queues[1].disposed=true;s_vendor_watch_handles=3;s_vendor_watch_capacity=24;
    esp32_mquickjs_deinit_wifi_vendor_ie_watch_runtime();
    for(unsigned i=0;i<4;++i)assert(!s_vendor_sources[i]);
    assert(!queues[1].closes && queues[2].closes==1 && queues[3].closes==1);
    assert(queues[2].retains==1 && queues[2].releases==1);
    assert(s_vendor_watch_handles==3 && s_vendor_watch_capacity==24);
    for(unsigned i=0;i<3;++i){vendor_watch_source_t *p=calloc(1,sizeof(*p));p->capacity=8;vendor_watch_destroyed(p);}
    assert(!s_vendor_watch_handles && !s_vendor_watch_capacity && !locked);return 0;
}
