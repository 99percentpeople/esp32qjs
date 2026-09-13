
uint8_t setup_timer_param[372];
static struct timer_record {esp_timer_create_args_t args;bool active,live;} timers[128];
static esp32_mquickjs_wifi_twt_setup_timer_identity_t current[8];
static bool association=true, process_in_post;
static int create_error,start_error,stop_error,delete_error,post_error;
static unsigned creates,starts,stops,deletes,posts,processes,matches;
static uint64_t interval;
static void *queued;
static uint8_t queued_phase,last_dialog;
static void (*post_hook)(void);
static ETSTimer *legacy(unsigned slot) {assert(slot<8);return (ETSTimer *)(setup_timer_param+144+slot*20);}
static void cold_boot(void) {
    heap_caps_free(s_setup_timer.entries);memset(&s_setup_timer,0,sizeof(s_setup_timer));
    memset(setup_timer_param,0,sizeof(setup_timer_param));memset(timers,0,sizeof(timers));
    allocations=creates=starts=stops=deletes=posts=processes=matches=0;
    create_error=start_error=stop_error=delete_error=post_error=0;
    allocation_error=process_in_post=false;association=true;post_hook=NULL;queued=NULL;
    for(unsigned i=0;i<8;++i)current[i]=(esp32_mquickjs_wifi_twt_setup_timer_identity_t){0x123000,400+i,10+i,i};
}
bool esp32_mquickjs_wifi_twt_sdk_setup_timer_capture_native(unsigned slot,uint8_t phase,const void *arg,
    esp32_mquickjs_wifi_twt_setup_timer_identity_t *out) {
    assert(!locked);
    if(!association || slot>=8 || (phase!=26 && phase!=27) || arg!=setup_timer_param+136+slot)return false;
    *out=current[slot];return true;
}
bool esp32_mquickjs_wifi_twt_sdk_setup_timer_matches_native(unsigned slot,uint8_t phase,
    const esp32_mquickjs_wifi_twt_setup_timer_identity_t *id) {
    assert(!locked && slot<8 && (phase==26 || phase==27));++matches;
    return association && id->node==current[slot].node && id->request_id==current[slot].request_id &&
        id->dialog==current[slot].dialog && id->flow==current[slot].flow;
}
bool esp32_mquickjs_wifi_twt_sdk_setup_tx_matches_native(unsigned slot,
    const esp32_mquickjs_wifi_twt_setup_timer_identity_t *id) {
    return esp32_mquickjs_wifi_twt_sdk_setup_timer_matches_native(slot,26,id);
}
void itwt_setup_timeout_fn(void *arg) {(void)arg;assert(0);}
void itwt_setup_dwell_timeout_fn(void *arg) {(void)arg;assert(0);}
void __real_itwt_setup_timeout_fn_process(void *arg) {assert(!locked);++processes;last_dialog=*(uint8_t *)arg;}
void __real_itwt_setup_dwell_timeout_fn_process(void *arg) {assert(!locked);++processes;last_dialog=*(uint8_t *)arg;}
esp_err_t esp_timer_create(const esp_timer_create_args_t *args,esp_timer_handle_t *out) {
    assert(!locked && creates<128 && args->dispatch_method==ESP_TIMER_TASK);
    struct timer_record *timer=&timers[creates++];
    if(create_error)return create_error;timer->args=*args;timer->live=true;*out=timer;return ESP_OK;
}
esp_err_t esp_timer_stop(esp_timer_handle_t h) {
    assert(!locked);struct timer_record *timer=h;assert(timer->live);++stops;
    if(stop_error)return stop_error;if(!timer->active)return ESP_ERR_INVALID_STATE;
    timer->active=false;return ESP_OK;
}
esp_err_t esp_timer_delete(esp_timer_handle_t h) {
    assert(!locked);struct timer_record *timer=h;assert(timer->live);++deletes;
    if(delete_error)return delete_error;if(timer->active)return ESP_ERR_INVALID_STATE;
    timer->live=false;return ESP_OK;
}
esp_err_t esp_timer_start_once(esp_timer_handle_t h,uint64_t us) {
    assert(!locked);struct timer_record *timer=h;assert(timer->live);++starts;interval=us;
    if(start_error)return start_error;timer->active=true;return ESP_OK;
}
static void deliver(void) {
    assert(queued);void *arg=queued;uint8_t phase=queued_phase;queued=NULL;
    if(phase==26)__wrap_itwt_setup_timeout_fn_process(arg);else __wrap_itwt_setup_dwell_timeout_fn_process(arg);
}
int ieee80211_timer_process(int signal,int operation,void *arg) {
    assert(!locked && signal==7 && (operation==26 || operation==27) && arg);++posts;
    if(post_hook)post_hook();
    if(process_in_post) {
        if(operation==26)__wrap_itwt_setup_timeout_fn_process(arg);else __wrap_itwt_setup_dwell_timeout_fn_process(arg);
    }
    if(!post_error && !process_in_post){queued=arg;queued_phase=operation;}
    return post_error;
}
static void install(unsigned slot,uint8_t phase) {
    assert(esp32_mquickjs_wifi_twt_setup_timer_setfn(legacy(slot),
        phase==26?(void *)itwt_setup_timeout_fn:(void *)itwt_setup_dwell_timeout_fn,setup_timer_param+136+slot));
}
static void fire(struct timer_record *timer) {
    timer->active=false;timer->args.callback(timer->args.arg);
}
static void destroy(unsigned slot) {
    assert(esp32_mquickjs_wifi_twt_setup_timer_disarm(legacy(slot)));
    assert(esp32_mquickjs_wifi_twt_setup_timer_done(legacy(slot)));
}
static esp32_mquickjs_wifi_twt_setup_timer_snapshot_t snapshot(void) {
    esp32_mquickjs_wifi_twt_setup_timer_snapshot_t out;esp32_mquickjs_wifi_twt_setup_timer_snapshot(&out);return out;
}
static void replace_in_post(void) {
    post_hook=NULL;destroy(0);++current[0].request_id;install(0,27);
}
int main(void) {
    assert(sizeof(ETSTimer)==20);cold_boot();ETSTimer foreign={0};
    assert(!esp32_mquickjs_wifi_twt_setup_timer_setfn(&foreign,NULL,NULL));
    assert(!esp32_mquickjs_wifi_twt_setup_timer_disarm(&foreign));
    assert(!esp32_mquickjs_wifi_twt_setup_timer_done(&foreign));
    assert(!esp32_mquickjs_wifi_twt_setup_timer_arm(&foreign,1,false) && !allocations && !creates);
    assert(!esp32_mquickjs_wifi_twt_setup_timer_setfn(setup_timer_param+145,NULL,NULL));
    for(unsigned i=0;i<8;i++)install(i,26);
    assert(allocations==1 && creates==8 && snapshot().active_mask==255);
    assert(snapshot().reserved_bytes==8*sizeof(twt_setup_timer_entry_t));
    for(unsigned i=0;i<8;i++) {
        assert(esp32_mquickjs_wifi_twt_setup_timer_arm(legacy(i),UINT64_C(4294967295)*1000,false));
        assert(interval==UINT64_C(4294967295)*1000);
        fire(legacy(i)->timer_arg);assert(posts==i+1 && queued_phase==26);deliver();
        assert(processes==i+1 && last_dialog==10+i);destroy(i);
    }
    assert(!snapshot().active_mask && !snapshot().fault && native_live==1);
    cold_boot();install(0,26);struct timer_record *old=legacy(0)->timer_arg;
    esp32_mquickjs_wifi_twt_setup_timer_arm(legacy(0),1,false);fire(old);assert(queued);
    destroy(0);++current[0].request_id;install(0,27);unsigned before=posts;
    old->args.callback(old->args.arg);assert(posts==before);deliver();assert(!processes && !matches);
    struct timer_record *next=legacy(0)->timer_arg;
    __wrap_itwt_setup_timeout_fn_process(next->args.arg);assert(!processes); /* not fired */
    fire(next);assert(queued_phase==27);
    __wrap_itwt_setup_timeout_fn_process(queued);assert(!processes); /* wrong phase */
    deliver();assert(processes==1);next->args.callback(next->args.arg);assert(posts==before+1);
    destroy(0);assert(!snapshot().fault);
    cold_boot();install(0,26);next=legacy(0)->timer_arg;fire(next);
    ++current[0].request_id;deliver();assert(!processes && snapshot().fault_stage==ESP32_MQUICKJS_WIFI_TWT_SETUP_TIMER_NATIVE);
    destroy(0);
    cold_boot();install(0,26);next=legacy(0)->timer_arg;fire(next);association=false;deliver();
    assert(!processes && snapshot().fault==ESP_ERR_INVALID_STATE);destroy(0);
    cold_boot();install(0,26);process_in_post=true;post_error=77;fire(legacy(0)->timer_arg);
    assert(processes==1 && !snapshot().fault);destroy(0); /* consumed before post return */
    cold_boot();install(0,26);post_hook=replace_in_post;post_error=78;
    fire(legacy(0)->timer_arg);assert(!snapshot().fault && snapshot().active_mask==1 && snapshot().last_identity==2);
    destroy(0);
    cold_boot();install(0,26);post_error=79;fire(legacy(0)->timer_arg);
    assert(snapshot().fault==79 && snapshot().fault_stage==ESP32_MQUICKJS_WIFI_TWT_SETUP_TIMER_POST && !snapshot().active_mask);
    destroy(0);
    cold_boot();allocation_error=true;install(0,26);
    assert(!creates && !native_live && snapshot().fault==ESP_ERR_NO_MEM && !legacy(0)->timer_arg);
    cold_boot();create_error=80;install(0,26);assert(snapshot().fault==80 && !legacy(0)->timer_arg);
    cold_boot();install(0,26);start_error=81;esp32_mquickjs_wifi_twt_setup_timer_arm(legacy(0),10,false);
    assert(snapshot().fault==81 && !snapshot().active_mask);destroy(0);assert(!snapshot().cleanup_mask);
    cold_boot();install(0,26);esp32_mquickjs_wifi_twt_setup_timer_arm(legacy(0),10,false);
    stop_error=82;esp32_mquickjs_wifi_twt_setup_timer_disarm(legacy(0));assert(snapshot().fault==82 && snapshot().cleanup_mask==1);
    stop_error=0;esp32_mquickjs_wifi_twt_setup_timer_disarm(legacy(0));delete_error=83;
    next=legacy(0)->timer_arg;esp32_mquickjs_wifi_twt_setup_timer_done(legacy(0));
    assert(legacy(0)->timer_arg==next && snapshot().cleanup_mask==1 && snapshot().fault==82);
    unsigned before_create=creates;install(0,27);assert(creates==before_create && legacy(0)->timer_arg==next);
    delete_error=0;esp32_mquickjs_wifi_twt_setup_timer_done(legacy(0));
    assert(!legacy(0)->timer_arg && !snapshot().cleanup_mask && snapshot().fault==82);
    cold_boot();install(0,26);esp32_mquickjs_wifi_twt_setup_timer_arm(legacy(0),1,true);
    assert(snapshot().fault==ESP_ERR_INVALID_ARG && !starts);destroy(0);
    cold_boot();s_setup_timer.snapshot.last_identity=UINT32_MAX;install(7,26);
    assert(!creates && snapshot().last_identity==UINT32_MAX && snapshot().fault==ESP_ERR_NO_MEM && snapshot().fault_slot==7);
    cold_boot();install(0,26);uint32_t tx=0;
    assert(esp32_mquickjs_wifi_twt_setup_tx_capture_native(current[0].node,10,0,&tx) && tx==1);
    assert(esp32_mquickjs_wifi_twt_setup_tx_matches_native(tx,10,0));
    assert(!esp32_mquickjs_wifi_twt_setup_tx_capture_native(current[0].node,10,0,&tx));
    assert(!esp32_mquickjs_wifi_twt_setup_tx_matches_native(tx,11,0));
    assert(!esp32_mquickjs_wifi_twt_setup_tx_matches_native(tx,10,1));
    destroy(0);install(0,26); /* Even an identical native tuple gets a new identity. */
    assert(!esp32_mquickjs_wifi_twt_setup_tx_matches_native(tx,10,0));tx=0;
    assert(esp32_mquickjs_wifi_twt_setup_tx_capture_native(current[0].node,10,0,&tx) && tx==2);
    fire(legacy(0)->timer_arg);assert(!esp32_mquickjs_wifi_twt_setup_tx_matches_native(tx,10,0));
    destroy(0);install(0,27);tx=0;
    assert(!esp32_mquickjs_wifi_twt_setup_tx_capture_native(current[0].node,10,0,&tx) && tx==0);
    destroy(0);install(0,26);tx=0;association=false;
    assert(!esp32_mquickjs_wifi_twt_setup_tx_capture_native(current[0].node,10,0,&tx) && tx==0);
    cold_boot();install(0,26);install(1,27);next=legacy(0)->timer_arg;
    assert(esp32_mquickjs_wifi_twt_setup_timer_cancel_native(400,2)==ESP_ERR_INVALID_STATE);
    assert(snapshot().active_mask==3 && stops==0 && deletes==0); /* Preflight foreign pending slot. */
    stop_error=90;
    assert(esp32_mquickjs_wifi_twt_setup_timer_cancel_native(400,1)==90);
    assert(snapshot().active_mask==2 && legacy(0)->timer_arg==next && deletes==0);
    unsigned posted=posts;next->args.callback(next->args.arg);assert(posts==posted);
    stop_error=0;delete_error=91;
    assert(esp32_mquickjs_wifi_twt_setup_timer_cancel_native(400,1)==91 && legacy(0)->timer_arg==next);
    unsigned stopped=stops;delete_error=0;
    assert(esp32_mquickjs_wifi_twt_setup_timer_cancel_native(400,1)==ESP_OK && stops==stopped);
    assert(!legacy(0)->timer_arg && legacy(1)->timer_arg && snapshot().active_mask==2 && !snapshot().cleanup_mask);
    assert(snapshot().fault==90); /* First fault is diagnostic, not retry result. */
    unsigned deleted=deletes;
    assert(esp32_mquickjs_wifi_twt_setup_timer_cancel_native(400,0)==ESP_OK && stops==stopped && deletes==deleted);
    assert(esp32_mquickjs_wifi_twt_setup_timer_cancel_native(-1,0)==ESP_ERR_INVALID_ARG);
    assert(esp32_mquickjs_wifi_twt_setup_timer_cancel_native(401,0)==ESP_OK); /* Orphaned handle, no pending bit. */
    assert(!legacy(1)->timer_arg && !snapshot().active_mask);
    cold_boot();allocation_error=true;install(0,26);
    assert(esp32_mquickjs_wifi_twt_setup_timer_cancel_native(400,1)==ESP_OK && !stops && !deletes);
    cold_boot();install(0,26);fire(legacy(0)->timer_arg);
    assert(esp32_mquickjs_wifi_twt_setup_timer_cancel_native(400,1)==ESP_OK);deliver();assert(!processes);
    cold_boot();install(0,26);install(1,27);uint32_t revision=111;
    assert(esp32_mquickjs_wifi_twt_setup_timer_quiescent_native(400,&revision)==ESP_ERR_NOT_FINISHED && revision==111);
    stop_error=91;assert(esp32_mquickjs_wifi_twt_setup_timer_cancel_native(400,1)==91);
    assert(esp32_mquickjs_wifi_twt_setup_timer_quiescent_native(400,&revision)==ESP_ERR_NOT_FINISHED && revision==111);
    stop_error=0;assert(esp32_mquickjs_wifi_twt_setup_timer_cancel_native(400,1)==0);
    assert(esp32_mquickjs_wifi_twt_setup_timer_quiescent_native(400,&revision)==0 && revision==snapshot().revision);
    assert(esp32_mquickjs_wifi_twt_setup_timer_quiescent_native(401,&revision)==ESP_ERR_NOT_FINISHED);
    assert(esp32_mquickjs_wifi_twt_setup_timer_quiescent_native(-1,&revision)==ESP_ERR_INVALID_ARG);
    assert(esp32_mquickjs_wifi_twt_setup_timer_quiescent_native(400,NULL)==ESP_ERR_INVALID_ARG);
    s_setup_timer.snapshot.revision=UINT32_MAX;revision=111;
    assert(esp32_mquickjs_wifi_twt_setup_timer_quiescent_native(400,&revision)==ESP_ERR_NO_MEM && revision==111);
    cold_boot();install(0,26);install(1,27);fire(legacy(0)->timer_arg);
    esp32_mquickjs_wifi_twt_setup_timers_connection_closed_native();
    assert(!snapshot().active_mask && !legacy(0)->timer_arg && !legacy(1)->timer_arg);
    deliver();assert(!processes && !snapshot().fault);
    cold_boot();install(0,26);install(1,27);stop_error=92;
    esp32_mquickjs_wifi_twt_setup_timers_connection_closed_native();
    assert(!snapshot().active_mask && legacy(0)->timer_arg && legacy(1)->timer_arg);
    stop_error=0;delete_error=93;
    esp32_mquickjs_wifi_twt_setup_timers_connection_closed_native();
    stopped=stops;delete_error=0;
    esp32_mquickjs_wifi_twt_setup_timers_connection_closed_native();
    assert(stops==stopped && !legacy(0)->timer_arg && !legacy(1)->timer_arg);
    cold_boot();assert(!native_live && !locked);return 0;
}
