
#define OK ESP32_MQUICKJS_WIFI_PROMISCUOUS_OK
static uint8_t packet[sizeof(wifi_pkt_rx_ctrl_t)+24];
static pthread_mutex_t pause_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t pause_changed=PTHREAD_COND_INITIALIZER;
static bool paused,proceed;
typedef struct { unsigned calls,accepted;bool block,reentrant;uint8_t copied[24]; } context_t;
static void receive(void *argument,const esp32_mquickjs_wifi_rx_target_view_t *view,
                    esp32_mquickjs_wifi_rx_filter_result_t result,uint64_t now) {
    assert(!critical_depth && now==123456789);
    context_t *context=argument;++context->calls;
    esp32_mquickjs_wifi_promiscuous_snapshot_t snapshot;
    esp32_mquickjs_wifi_promiscuous_snapshot(&snapshot);
    assert(snapshot.dispatch_busy && snapshot.entered>0);
    if(result==ESP32_MQUICKJS_WIFI_RX_FILTER_ACCEPT) {
        ++context->accepted;assert(view->readable_length==24 && view->bytes);
        memcpy(context->copied,view->bytes,24);
    }
    if(context->reentrant)assert(!esp32_mquickjs_wifi_promiscuous_dispatch(NULL,WIFI_PKT_DATA,now));
    if(context->block) {
        assert(!pthread_mutex_lock(&pause_lock));paused=true;assert(!pthread_cond_broadcast(&pause_changed));
        while(!proceed)assert(!pthread_cond_wait(&pause_changed,&pause_lock));
        assert(!pthread_mutex_unlock(&pause_lock));
    }
}
static bool dispatch(void) { return esp32_mquickjs_wifi_promiscuous_dispatch(packet,WIFI_PKT_DATA,123456789); }
static void *dispatch_thread(void *unused) { (void)unused;assert(dispatch());return NULL; }
static void close_token(esp32_mquickjs_wifi_promiscuous_token_t *token) {
    assert(esp32_mquickjs_wifi_promiscuous_begin_close(token)==OK);
    assert(esp32_mquickjs_wifi_promiscuous_begin_close(token)==OK);
    assert(esp32_mquickjs_wifi_promiscuous_activate(token)==ESP32_MQUICKJS_WIFI_PROMISCUOUS_INVALID_STATE);
    assert(esp32_mquickjs_wifi_promiscuous_finish_close(token)==OK && !token->identity);
}
int main(void) {
    wifi_pkt_rx_ctrl_t rx;memset(&rx,0,sizeof(rx));rx.sig_len=24;rx.rssi=-50;
#if CONFIG_SOC_WIFI_HE_SUPPORT
    rx.dump_len=24;
#endif
    memcpy(packet,&rx,sizeof(rx));packet[sizeof(rx)]=0x08;
    esp32_mquickjs_wifi_rx_filter_t filter={.type_mask=4,.sample_every=1,.valid_only=true};
    esp32_mquickjs_wifi_promiscuous_subscriber_t subscribers[9];
    memset(subscribers,0xa5,sizeof(subscribers));
    esp32_mquickjs_wifi_promiscuous_token_t tokens[9]={0},other={0};
    context_t contexts[9]={0};
    esp32_mquickjs_wifi_promiscuous_snapshot_t snapshot;
    esp32_mquickjs_wifi_promiscuous_snapshot(NULL);
    assert(esp32_mquickjs_wifi_promiscuous_reserve(NULL,&filter,receive,contexts,&other)==ESP32_MQUICKJS_WIFI_PROMISCUOUS_INVALID_ARGUMENT);
    assert(esp32_mquickjs_wifi_promiscuous_activate(NULL)==ESP32_MQUICKJS_WIFI_PROMISCUOUS_INVALID_ARGUMENT);
    assert(esp32_mquickjs_wifi_promiscuous_begin_close(NULL)==ESP32_MQUICKJS_WIFI_PROMISCUOUS_INVALID_ARGUMENT);
    assert(esp32_mquickjs_wifi_promiscuous_finish_close(NULL)==ESP32_MQUICKJS_WIFI_PROMISCUOUS_INVALID_ARGUMENT);
    for(unsigned i=0;i<8;i++) {
        filter.sample_every=i==1?2:1;
        assert(esp32_mquickjs_wifi_promiscuous_reserve(&subscribers[i],&filter,receive,&contexts[i],&tokens[i])==OK);
        assert(tokens[i].identity>0 && (i==0 || tokens[i].identity>tokens[i-1].identity));
    }
    assert(esp32_mquickjs_wifi_promiscuous_reserve(&subscribers[0],&filter,receive,contexts,&other)==ESP32_MQUICKJS_WIFI_PROMISCUOUS_INVALID_STATE);
    assert(esp32_mquickjs_wifi_promiscuous_reserve(&subscribers[8],&filter,receive,contexts,&tokens[0])==ESP32_MQUICKJS_WIFI_PROMISCUOUS_INVALID_STATE);
    assert(esp32_mquickjs_wifi_promiscuous_reserve(&subscribers[8],&filter,receive,contexts,&other)==ESP32_MQUICKJS_WIFI_PROMISCUOUS_CAPACITY && !other.identity);
    esp32_mquickjs_wifi_promiscuous_snapshot(&snapshot);
    assert(snapshot.reserved==8 && !snapshot.active && snapshot.required_types==4 && !snapshot.entered);
    assert(esp32_mquickjs_wifi_promiscuous_owned_requirements(tokens,8,&snapshot));
    assert(!esp32_mquickjs_wifi_promiscuous_owned_requirements(tokens,7,&snapshot) && !snapshot.required_types);
    esp32_mquickjs_wifi_promiscuous_token_t duplicates[8];memcpy(duplicates,tokens,sizeof(duplicates));
    duplicates[7]=duplicates[0];
    assert(!esp32_mquickjs_wifi_promiscuous_owned_requirements(duplicates,8,&snapshot));
    duplicates[7].identity=UINT32_MAX;
    assert(!esp32_mquickjs_wifi_promiscuous_owned_requirements(duplicates,8,&snapshot));
    assert(!esp32_mquickjs_wifi_promiscuous_owned_requirements(NULL,1,&snapshot));
    assert(!esp32_mquickjs_wifi_promiscuous_owned_requirements(tokens,9,&snapshot));
    assert(dispatch());for(unsigned i=0;i<8;i++)assert(!contexts[i].calls);
    for(unsigned i=0;i<8;i++) {
        assert(esp32_mquickjs_wifi_promiscuous_finish_close(&tokens[i])==ESP32_MQUICKJS_WIFI_PROMISCUOUS_INVALID_STATE);
        assert(esp32_mquickjs_wifi_promiscuous_activate(&tokens[i])==OK);
        assert(esp32_mquickjs_wifi_promiscuous_activate(&tokens[i])==OK);
    }
    contexts[0].reentrant=true;
    assert(dispatch() && dispatch());
    for(unsigned i=0;i<8;i++) {
        assert(contexts[i].calls==2 && contexts[i].accepted==(i==1?1U:2U));
        assert(!memcmp(contexts[i].copied,packet+sizeof(rx),24));
    }
    esp32_mquickjs_wifi_promiscuous_snapshot(&snapshot);
    assert(snapshot.overlapping_dispatches==2 && !snapshot.dispatch_busy && !snapshot.entered);
    esp32_mquickjs_wifi_promiscuous_token_t stale=tokens[0];
    for(unsigned i=8;i>0;i--)close_token(&tokens[i-1]);
    assert(esp32_mquickjs_wifi_promiscuous_finish_close(&stale)==ESP32_MQUICKJS_WIFI_PROMISCUOUS_INVALID_TOKEN);
    assert(esp32_mquickjs_wifi_promiscuous_finish_close(&tokens[0])==ESP32_MQUICKJS_WIFI_PROMISCUOUS_INVALID_TOKEN);

    /* A closing subscriber is retained through the already-entered callback;
     * a newly activated subscriber cannot join that earlier admission set. */
    memset(contexts,0,sizeof(contexts));contexts[0].block=true;filter.sample_every=1;
    assert(esp32_mquickjs_wifi_promiscuous_reserve(&subscribers[0],&filter,receive,&contexts[0],&tokens[0])==OK);
    assert(esp32_mquickjs_wifi_promiscuous_activate(&tokens[0])==OK);
    assert(esp32_mquickjs_wifi_promiscuous_reserve(&subscribers[1],&filter,receive,&contexts[1],&tokens[1])==OK);
    assert(esp32_mquickjs_wifi_promiscuous_activate(&tokens[1])==OK);
    pthread_t worker;assert(!pthread_create(&worker,NULL,dispatch_thread,NULL));
    assert(!pthread_mutex_lock(&pause_lock));
    while(!paused)assert(!pthread_cond_wait(&pause_changed,&pause_lock));
    assert(!pthread_mutex_unlock(&pause_lock));
    assert(esp32_mquickjs_wifi_promiscuous_begin_close(&tokens[0])==OK);
    assert(esp32_mquickjs_wifi_promiscuous_finish_close(&tokens[0])==ESP32_MQUICKJS_WIFI_PROMISCUOUS_DRAINING);
    assert(esp32_mquickjs_wifi_promiscuous_begin_close(&tokens[1])==OK);
    /* Subscriber 1 has not reached its sink yet, but is already retained. */
    assert(!contexts[1].calls && esp32_mquickjs_wifi_promiscuous_finish_close(&tokens[1])==ESP32_MQUICKJS_WIFI_PROMISCUOUS_DRAINING);
    assert(esp32_mquickjs_wifi_promiscuous_reserve(&subscribers[0],&filter,receive,contexts,&other)==ESP32_MQUICKJS_WIFI_PROMISCUOUS_INVALID_STATE);
    assert(esp32_mquickjs_wifi_promiscuous_reserve(&subscribers[2],&filter,receive,&contexts[2],&tokens[2])==OK);
    assert(esp32_mquickjs_wifi_promiscuous_activate(&tokens[2])==OK);
    assert(!dispatch() && !contexts[2].calls);
    esp32_mquickjs_wifi_promiscuous_snapshot(&snapshot);
    assert(snapshot.closing==2 && snapshot.active==1 && snapshot.entered==2 && snapshot.dispatch_busy);
    assert(!pthread_mutex_lock(&pause_lock));proceed=true;assert(!pthread_cond_broadcast(&pause_changed));
    assert(!pthread_mutex_unlock(&pause_lock));assert(!pthread_join(worker,NULL));
    assert(contexts[0].calls==1 && contexts[1].calls==1 && !contexts[2].calls);
    stale=tokens[0];assert(esp32_mquickjs_wifi_promiscuous_finish_close(&tokens[0])==OK);
    assert(esp32_mquickjs_wifi_promiscuous_finish_close(&tokens[1])==OK);
    assert(dispatch() && contexts[0].calls==1 && contexts[1].calls==1 && contexts[2].calls==1);
    assert(esp32_mquickjs_wifi_promiscuous_reserve(&subscribers[0],&filter,receive,&contexts[3],&tokens[0])==OK);
    assert(tokens[0].identity!=stale.identity);
    assert(esp32_mquickjs_wifi_promiscuous_begin_close(&stale)==ESP32_MQUICKJS_WIFI_PROMISCUOUS_INVALID_TOKEN);
    close_token(&tokens[0]);close_token(&tokens[2]);

    /* Requirements include prepared subscriptions and exclude closing ones. */
    filter.type_mask=2;filter.subtype_filter=true;filter.subtype_mask=1U<<13;filter.valid_only=false;
    assert(esp32_mquickjs_wifi_promiscuous_reserve(&subscribers[0],&filter,receive,contexts,&tokens[0])==OK);
    filter.type_mask=3;filter.subtype_mask=1U<<11;filter.valid_only=true;
    assert(esp32_mquickjs_wifi_promiscuous_reserve(&subscribers[1],&filter,receive,contexts,&tokens[1])==OK);
    esp32_mquickjs_wifi_promiscuous_snapshot(&snapshot);
    assert(snapshot.required_types==3 && snapshot.required_control_subtypes==((1U<<13)|(1U<<11)) && snapshot.require_error_frames);
    assert(esp32_mquickjs_wifi_promiscuous_begin_close(&tokens[0])==OK);
    esp32_mquickjs_wifi_promiscuous_snapshot(&snapshot);
    assert(snapshot.required_types==3 && snapshot.required_control_subtypes==(1U<<11) && !snapshot.require_error_frames);
    assert(!esp32_mquickjs_wifi_promiscuous_owned_requirements(tokens,2,&snapshot));
    assert(esp32_mquickjs_wifi_promiscuous_owned_requirements(tokens+1,1,&snapshot) && snapshot.closing==1);
    assert(esp32_mquickjs_wifi_promiscuous_finish_close(&tokens[0])==OK);close_token(&tokens[1]);

    /* After close, an actual unreadable control page cannot be dereferenced by
     * a late callback. Driver-unregister guarantees are deliberately not mocked. */
    size_t page=(size_t)sysconf(_SC_PAGESIZE);
    void *storage=mmap(NULL,page,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    assert(storage!=MAP_FAILED && sizeof(subscribers[0])<page);
    filter.type_mask=4;filter.subtype_filter=false;
    assert(esp32_mquickjs_wifi_promiscuous_reserve(storage,&filter,receive,contexts,&other)==OK);
    assert(esp32_mquickjs_wifi_promiscuous_activate(&other)==OK);close_token(&other);
    assert(!mprotect(storage,page,PROT_NONE));assert(dispatch());assert(!munmap(storage,page));

    /* Fault-inject the production counter's final identity; do not reset it or
     * replace registry logic with a test state machine. */
    s_promiscuous.next_identity=UINT32_MAX;
    assert(esp32_mquickjs_wifi_promiscuous_reserve(&subscribers[0],&filter,receive,contexts,&tokens[0])==OK);
    assert(tokens[0].identity==UINT32_MAX);close_token(&tokens[0]);
    assert(esp32_mquickjs_wifi_promiscuous_reserve(&subscribers[0],&filter,receive,contexts,&tokens[0])==ESP32_MQUICKJS_WIFI_PROMISCUOUS_IDENTITY_EXHAUSTED);
    esp32_mquickjs_wifi_promiscuous_snapshot(&snapshot);
    assert(snapshot.identity_exhausted && !snapshot.active && !snapshot.reserved && !snapshot.closing && !snapshot.entered);
    assert(!critical_depth);
}
