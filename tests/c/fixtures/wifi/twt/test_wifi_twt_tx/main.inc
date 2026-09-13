
/* Non-information cases use this SDK boundary; the full producer/identity
 * fixture supplies its own capture, match and original completion. */
#ifndef TEST_REAL_INFORMATION_SUBMIT
bool esp32_mquickjs_wifi_twt_sdk_information_capture_native(unsigned slot,const void *arg,
    esp32_mquickjs_wifi_twt_information_identity_t *out) {(void)slot;(void)arg;(void)out;return false;}
bool esp32_mquickjs_wifi_twt_sdk_information_matches_native(unsigned slot,
    const esp32_mquickjs_wifi_twt_information_identity_t *id) {(void)slot;(void)id;return false;}
void __real_he_twt_information_txcb(void *buffer) {(void)buffer;assert(0);}
#endif
#ifndef TEST_REAL_INFORMATION_SUBMIT
int __real_ieee80211_itwt_information(void *node,uint32_t flow,uint32_t size,uint32_t all,uint32_t timeout) {
    (void)node;(void)flow;(void)size;(void)all;(void)timeout;return 0;
}
#ifndef TEST_REAL_TEARDOWN_TX
void pm_twt_wake_up(void) {}
void pm_twt_wake_done(void) {}
#endif
#endif
typedef esp32_mquickjs_wifi_twt_tx_snapshot_t snapshot_t;
static snapshot_t read_snapshot(void) {
    snapshot_t s;esp32_mquickjs_wifi_twt_tx_snapshot(&s);return s;
}
static void check(unsigned tracked,unsigned output,unsigned recycle) {
    snapshot_t s=read_snapshot();
    assert(s.tracked==tracked && s.output_calls==output && s.recycle_calls==recycle);
}
int __real_ht_action_output(void *node,void *buffer) {
    assert(!locked && node==(void *)17);++output_calls;callback_buffer=buffer;
    if(output_hook)output_hook(buffer);
    if(early_setup_callback) {early_setup_callback=false;__wrap_he_twt_setup_txcb(buffer);}
    if(early_probe_callback) {
        early_probe_callback=false;assert(!native_pending && native_callback_calls==0);
        assert(__wrap_itwt_probe_rc_tx_cb(buffer)==0);
        assert(native_callback_calls==0);
    }
    if(early_recycle) {
        early_recycle=false;check(1,1,0);
        __wrap_esf_buf_recycle(buffer);
        /* Native output still owns its stack/EB lifetime until return. */
        check(1,1,0);
    }
    return native_result;
}
void __real_esf_buf_recycle(void *buffer) {
    assert(!locked);++recycle_calls;
    if(reuse_during_recycle) {
        reuse_during_recycle=false;check(1,0,1);
        /* Native free/reallocation can reuse the address before this wrapper
         * returns. Keep both production entries alive through that boundary. */
        assert(__wrap_ht_action_output((void *)17,buffer)==native_result);
        check(2,0,1);
    }
}
int __real_ic_tx_pkt(void *buffer) {return __real_ht_action_output((void *)17,buffer);}
int __real_wifi_sta_itwt_send_probe_req_process(void *message) {
    assert(!locked && read_snapshot().probe_calls>0);
    assert(s_probe_result.identity && s_probe_result.submitting);
    ++native_probe_calls;
    if(probe_hook)probe_hook(message);
    native_pending=native_result==ESP_OK;
    return native_result;
}
int __real_itwt_probe_rc_tx_cb(int status) {
    assert(!locked && read_snapshot().probe_calls==0 && native_pending);
    assert(read_snapshot().tracked==1);
    ++native_callback_calls;last_callback_status=status;native_pending=false;
    wifi_event_sta_itwt_probe_t event={.status=ITWT_PROBE_FAIL,.reason=(uint8_t)status};
    assert(__wrap_wifi_event_post(30,&event,sizeof(event))==0 && s_probe_result.event_seen);
    if(callback_recycle) {
        callback_recycle=false;__wrap_esf_buf_recycle(callback_buffer);
        check(1,0,0); /* Callback pin keeps its slot until this native return. */
    }
    return -1; /* Even TWT failure must not advance ordinary cnx traversal. */
}
void __real_he_twt_setup_txcb(void *buffer) {
    assert(!locked);++setup_callbacks;
    if(setup_callback_recycle) {
        setup_callback_recycle=false;__wrap_esf_buf_recycle(buffer);check(1,0,0);
        /* Poison native storage after recycle; wrapper must not reread it. */
        memset(buffer,0xa5,72);
    }
}
void esp32_mquickjs_wifi_btwt_setup_tx_complete_native(unsigned slot,uint32_t identity,uintptr_t node,uint8_t dialog,const uint8_t parameter[17],uint8_t status) {
    assert(slot==(unsigned)(parameter[10]>>3) && identity);
    assert(!locked && node==17 && dialog!=0 && parameter[0]==216 && parameter[1]==10);
    for(unsigned i=12;i<17;++i)assert(parameter[i]==0);
    (void)status;__real_he_twt_setup_txcb(callback_buffer);
}
typedef struct {uint8_t eb[72],metadata[72],descriptor[16],frame[64];} buffer_t;
static void prepare(buffer_t *b,uint32_t mask) {
    memset(b,0,sizeof(*b));void *metadata=b->metadata;
    /* Setup helpers below supply actual native bodies and identities. */
    if(mask==(1U<<19))b->eb[64]=1;
    memcpy(b->eb+56,&metadata,sizeof(metadata));memcpy(b->metadata+20,&mask,sizeof(mask));
}
static void prepare_probe(buffer_t *b) {
    prepare(b,1);void *descriptor=b->descriptor,*frame=b->frame;uint16_t header_length=24;
    memcpy(b->eb+4,&descriptor,sizeof(descriptor));memcpy(b->descriptor+4,&frame,sizeof(frame));
    memcpy(b->eb+20,&header_length,sizeof(header_length));b->frame[0]=0x40;
}
static void prepare_setup(buffer_t *b,bool protected_frame) {
    prepare(b,1U<<19);b->eb[64]=0;
    void *descriptor=b->descriptor,*frame=b->frame;uint16_t header=24;uint32_t length=20,protection=protected_frame;
    memcpy(b->eb+4,&descriptor,sizeof(descriptor));memcpy(b->descriptor+4,&frame,sizeof(frame));
    memcpy(b->eb+20,&header,2);memcpy(b->eb+24,&length,4);memcpy(b->metadata,&protection,4);
    b->frame[0]=0xd0;b->frame[1]=protected_frame?0x40:0;
    uint8_t *body=b->frame+(protected_frame?32:24);
    body[0]=protected_frame?23:22;body[1]=protected_frame?4:6;
    body[2]=9;body[3]=216;body[4]=15;body[6]=0x80;body[7]=1;
}
static void prepare_information(buffer_t *b,bool protected_frame,uint8_t control) {
    prepare_setup(b,protected_frame);uint32_t mask=1U<<20,length=(control&0x60U)?11:3;
    memcpy(b->metadata+20,&mask,4);memcpy(b->eb+24,&length,4);
    uint8_t *body=b->frame+(protected_frame?32:24);
    body[1]=protected_frame?6:11;body[2]=control;
}
static int send(buffer_t *b) {return __wrap_ht_action_output((void *)17,b->eb);}
static void release(buffer_t *b) {__wrap_esf_buf_recycle(b->eb);}
static void cold_boot(void) {
    check(0,0,0);free(s_twt_tx.entries);free(s_twt_tx.information);memset(&s_twt_tx,0,sizeof(s_twt_tx));
    memset(&s_probe_result,0,sizeof(s_probe_result));
    allocations=output_calls=recycle_calls=0;
    fail_allocation_at=0;output_hook=NULL;
    fail_allocate=early_recycle=reuse_during_recycle=false;native_result=0;
    current_task=(void *)31;in_isr=false;probe_hook=NULL;
    admission_error=last_callback_status=0;native_probe_calls=native_callback_calls=0;
    broadcast_node_valid=true;broadcast_timer_error=0;broadcast_builds=0;broadcast_begin_error=0;broadcast_held_mask=0;memset(broadcast_request,0,sizeof(broadcast_request));
    timer_finish_error=0;setup_identity=1;setup_callbacks=0;setup_valid=true;
    early_setup_callback=setup_callback_recycle=false;
    early_probe_callback=native_pending=callback_recycle=false;callback_buffer=NULL;
}
static void probe_send(void *opaque) {
    buffer_t *b=opaque;
    assert(read_snapshot().probe_calls==1);
    assert(__wrap_ic_tx_pkt(b->eb)==native_result);
    /* Even an early recycle cannot prove the native handler has finished:
     * the actual SDK arms its response timer after sending the frame. */
    assert(read_snapshot().probe_calls==1);
}
static void probe_isolation(void *opaque) {
    buffer_t *b=opaque;
    current_task=(void *)32;__wrap_ic_tx_pkt(b->eb);check(0,0,0);
    current_task=(void *)31;in_isr=true;__wrap_ic_tx_pkt(b->eb);check(0,0,0);in_isr=false;
    b->frame[0]=0x48;__wrap_ic_tx_pkt(b->eb);check(0,0,0);b->frame[0]=0x40;
    uint32_t callbacks=2;memcpy(b->metadata+20,&callbacks,4);
    __wrap_ic_tx_pkt(b->eb);check(0,0,0);callbacks=1;memcpy(b->metadata+20,&callbacks,4);
    probe_send(b);check(1,0,0);
}
static void probe_nested(void *opaque) {
    probe_hook=NULL;assert(__wrap_wifi_sta_itwt_send_probe_req_process(opaque)==ESP_ERR_INVALID_STATE);
    assert(read_snapshot().probe_calls==1 && s_twt_tx.probe_task==current_task);
    assert(read_snapshot().fault==ESP32_MQUICKJS_WIFI_TWT_TX_OK);
}
static void probe_send_failure(void *opaque) {
    buffer_t *b=opaque;native_result=__wrap_ic_tx_pkt(b->eb);
    assert(native_result==ESP_ERR_NO_MEM && output_calls==0 && recycle_calls==1);
}
int main(void) {
    buffer_t a,b;prepare(&a,4);prepare(&b,1U<<18);
    assert(send(&a)==0);release(&a);__wrap_esf_buf_recycle(NULL);
    check(0,0,0);assert(allocations==0 && output_calls==1 && recycle_calls==2);
    uint32_t last=read_snapshot().revision;
    assert(send(&b)==0);check(1,0,0);
    assert(read_snapshot().revision>last && allocations==1);
    assert(read_snapshot().reserved_bytes==TWT_TX_CAPACITY*sizeof(twt_tx_entry_t));
    /* Unrelated free must neither reduce another owner's count nor change its
     * revision; success returning from a cached send does not retire it. */
    last=read_snapshot().revision;release(&a);check(1,0,0);
    assert(read_snapshot().revision==last);release(&b);check(0,0,0);
    /* Information bit 20 now requires the real producer/identity fixture. */
    for(unsigned bit=18;bit<=18;++bit) {
        prepare(&b,1U<<bit);early_recycle=true;native_result=-19;
        assert(send(&b)==-19);check(0,0,0);
    }
    native_result=-42;assert(send(&b)==-42);check(1,0,0);
    release(&b);check(0,0,0);
    native_result=0;assert(send(&b)==0);reuse_during_recycle=true;
    release(&b);check(1,0,0);release(&b);check(0,0,0);
    assert(read_snapshot().fault==ESP32_MQUICKJS_WIFI_TWT_TX_OK && allocations==1);

    cold_boot();prepare(&b,1U<<18);assert(send(&b)==0);
    assert(send(&b)==0);check(1,0,0);
    assert(read_snapshot().fault==ESP32_MQUICKJS_WIFI_TWT_TX_DUPLICATE);
    release(&b);check(0,0,0);assert(read_snapshot().fault!=ESP32_MQUICKJS_WIFI_TWT_TX_OK);

    cold_boot();buffer_t buffers[TWT_TX_CAPACITY+1];
    for(unsigned i=0;i<=TWT_TX_CAPACITY;++i) {prepare(&buffers[i],1U<<18);assert(send(&buffers[i])==0);}
    check(TWT_TX_CAPACITY,0,0);assert(output_calls==TWT_TX_CAPACITY+1);
    assert(read_snapshot().fault==ESP32_MQUICKJS_WIFI_TWT_TX_CAPACITY);
    for(unsigned i=0;i<=TWT_TX_CAPACITY;++i)release(&buffers[i]);
    check(0,0,0);assert(read_snapshot().fault==ESP32_MQUICKJS_WIFI_TWT_TX_CAPACITY);

    cold_boot();fail_allocate=true;native_result=29;assert(send(&b)==29);
    assert(read_snapshot().fault==ESP32_MQUICKJS_WIFI_TWT_TX_NO_MEMORY);
    fail_allocate=false;assert(send(&b)==29);assert(allocations==1 && output_calls==2);
    check(0,0,0);release(&b);assert(read_snapshot().reserved_bytes==0);

    cold_boot();assert(send(&b)==0);s_twt_tx.snapshot.revision=UINT32_MAX;
    release(&b);check(0,0,0);
    assert(read_snapshot().fault==ESP32_MQUICKJS_WIFI_TWT_TX_REVISION_EXHAUSTED);
    assert(send(&b)==0 && read_snapshot().revision==UINT32_MAX);
    assert(read_snapshot().fault==ESP32_MQUICKJS_WIFI_TWT_TX_REVISION_EXHAUSTED);
    cold_boot();prepare_probe(&b);
    /* Ordinary scan/connection Probe Requests share the same callback bit. */
    __wrap_ic_tx_pkt(b.eb);check(0,0,0);assert(allocations==0);
    probe_hook=probe_isolation;native_result=-51;
    assert(__wrap_wifi_sta_itwt_send_probe_req_process(&b)==-51);
    check(1,0,0);assert(read_snapshot().probe_calls==0 && s_twt_tx.probe_task==NULL);
    assert(read_snapshot().probe_buffer_present);
    release(&b);check(0,0,0);
    assert(!read_snapshot().probe_buffer_present);
    probe_hook=probe_send;early_recycle=true;
    assert(__wrap_wifi_sta_itwt_send_probe_req_process(&b)==-51);
    check(0,0,0);assert(read_snapshot().probe_calls==0 && read_snapshot().fault==0);
    probe_hook=probe_nested;__wrap_wifi_sta_itwt_send_probe_req_process(&b);
    assert(read_snapshot().probe_calls==0 && s_twt_tx.probe_task==NULL);
    cold_boot();prepare_probe(&b);probe_hook=probe_send;timer_finish_error=85;
    early_probe_callback=early_recycle=true;
    assert(__wrap_wifi_sta_itwt_send_probe_req_process(&b)==85 && native_callback_calls==0);
    assert(!s_probe_result.submitted && s_probe_result.submit_error==85);check(0,0,0);
    assert(read_snapshot().fault==ESP32_MQUICKJS_WIFI_TWT_TX_OK);
    cold_boot();prepare_probe(&a);prepare_probe(&b);b.metadata[19]=7;
    assert(__wrap_itwt_probe_rc_tx_cb(a.eb)==-1 && native_callback_calls==0);
    probe_hook=probe_send;assert(__wrap_wifi_sta_itwt_send_probe_req_process(&b)==0);
    assert(native_pending && __wrap_itwt_probe_rc_tx_cb(a.eb)==-1 && native_callback_calls==0);
    assert(__wrap_wifi_sta_itwt_send_probe_req_process(&a)==ESP_ERR_INVALID_STATE && native_probe_calls==1);
    assert(__wrap_itwt_probe_rc_tx_cb(b.eb)==0 && native_callback_calls==1 && last_callback_status==7);
    assert(__wrap_itwt_probe_rc_tx_cb(b.eb)==0 && native_callback_calls==1);
    release(&b);check(0,0,0);
    /* Old storage is no longer a native completion authority. */
    assert(__wrap_itwt_probe_rc_tx_cb(b.eb)==-1 && native_callback_calls==1);
    cold_boot();prepare_probe(&b);b.metadata[19]=1;probe_hook=probe_send;
    early_probe_callback=early_recycle=true;
    assert(__wrap_wifi_sta_itwt_send_probe_req_process(&b)==0);
    assert(native_callback_calls==1 && last_callback_status==1);check(0,0,0);
    assert(s_probe_result.identity==1 && s_probe_result.submitted && !s_probe_result.submitting);
    assert(s_probe_result.event_seen && s_probe_result.event.reason==1);
    cold_boot();prepare_probe(&b);b.metadata[19]=2;probe_hook=probe_send;
    native_result=-9;early_probe_callback=early_recycle=true;
    assert(__wrap_wifi_sta_itwt_send_probe_req_process(&b)==-9);
    assert(native_callback_calls==0);check(0,0,0);
    cold_boot();prepare_probe(&b);probe_hook=probe_send;
    assert(__wrap_wifi_sta_itwt_send_probe_req_process(&b)==0);
    callback_recycle=true;callback_buffer=b.eb;
    assert(__wrap_itwt_probe_rc_tx_cb(b.eb)==0 && native_callback_calls==1);check(0,0,0);
    cold_boot();prepare_probe(&b);fail_allocate=true;probe_hook=probe_send_failure;
    assert(__wrap_wifi_sta_itwt_send_probe_req_process(&b)==ESP_ERR_NO_MEM);
    assert(native_callback_calls==0 && s_twt_tx.probe_entry==NULL);check(0,0,0);
    cold_boot();admission_error=77;
    assert(__wrap_wifi_sta_itwt_send_probe_req_process(&b)==77 && native_probe_calls==0);
    assert(s_probe_result.identity==0);
    cold_boot();s_probe_result.identity=UINT32_MAX;
    assert(__wrap_wifi_sta_itwt_send_probe_req_process(&b)==ESP_ERR_NO_MEM && native_probe_calls==0);
    assert(read_snapshot().probe_calls==0 && s_twt_tx.probe_task==NULL);
    cold_boot();prepare_setup(&a,false);assert(send(&a)==0);check(1,0,0);
    ++setup_identity;prepare_setup(&b,false);assert(send(&b)==0);check(2,0,0);
    /* Same dialog/flow, different submission. An old TX must never dispatch
     * the SDK callback, even while its EB has not yet been recycled. */
    __wrap_he_twt_setup_txcb(a.eb);assert(setup_callbacks==0);
    __wrap_he_twt_setup_txcb(b.eb);assert(setup_callbacks==1);
    __wrap_he_twt_setup_txcb(b.eb);assert(setup_callbacks==1);
    release(&a);release(&b);check(0,0,0);
    __wrap_he_twt_setup_txcb(a.eb);assert(setup_callbacks==1);
    cold_boot();prepare_setup(&a,true);assert(send(&a)==0);
    /* Native descriptor gains the reviewed 8-byte prefix before completion. */
    memmove(a.frame+8,a.frame,52);uint16_t prefix=0x2000;memcpy(a.eb+40,&prefix,2);
    __wrap_he_twt_setup_txcb(a.eb);assert(setup_callbacks==1);release(&a);
    cold_boot();prepare_setup(&a,false);early_setup_callback=true;
    assert(send(&a)==0 && setup_callbacks==1);release(&a);check(0,0,0);
    cold_boot();prepare_setup(&a,false);assert(send(&a)==0);
    setup_callback_recycle=true;__wrap_he_twt_setup_txcb(a.eb);
    assert(setup_callbacks==1);check(0,0,0);
    cold_boot();prepare_setup(&a,false);assert(send(&a)==0);setup_valid=false;
    __wrap_he_twt_setup_txcb(a.eb);assert(!setup_callbacks);release(&a);
    cold_boot();prepare_setup(&a,false);assert(send(&a)==0);
    assert(send(&a)==ESP_ERR_INVALID_STATE && recycle_calls==0 && output_calls==1);
    check(1,0,0);release(&a);check(0,0,0);
    cold_boot();prepare_setup(&a,false);setup_valid=false;
    assert(send(&a)==ESP_ERR_INVALID_STATE && output_calls==0 && recycle_calls==1);
    assert(read_snapshot().fault==ESP32_MQUICKJS_WIFI_TWT_TX_SETUP_IDENTITY);check(0,0,0);
    cold_boot();prepare_setup(&a,false);fail_allocate=true;
    assert(send(&a)==ESP_ERR_NO_MEM && output_calls==0 && recycle_calls==1);check(0,0,0);
    cold_boot();prepare_setup(&a,false);uint32_t short_length=7;memcpy(a.eb+24,&short_length,4);
    assert(send(&a)==ESP_ERR_INVALID_STATE && output_calls==0 && recycle_calls==1);check(0,0,0);
    cold_boot();prepare(&a,1U<<19);__wrap_he_twt_setup_txcb(a.eb);assert(setup_callbacks==0);
    cold_boot();return 0;
}
