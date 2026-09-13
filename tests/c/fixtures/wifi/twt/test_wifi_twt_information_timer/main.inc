
ETSTimer itwt_information_timer[8];
static struct native_timer {esp_timer_create_args_t args;bool active,live;} timers[64];
static unsigned creates,stops,deletes,posts,processes;
static esp_err_t create_error,start_error,stop_error,delete_error,post_error;
static bool same_request=true,early_process,ps_enabled=true;
static bool other_closing;
bool esp32_mquickjs_wifi_twt_setup_request_closing(int16_t id) {assert(!locked);return id==124 && other_closing;}
static void *queued;
void itwt_information_timeout_fn(void *arg) {(void)arg;assert(0);}
bool esp32_mquickjs_wifi_twt_sdk_information_capture_native(unsigned slot,const void *arg,
    esp32_mquickjs_wifi_twt_information_identity_t *out) {
    assert(!locked && slot<8 && arg);memset(out,0,sizeof(*out));
    out->node=17;out->flows=1U<<slot;out->control=*(const uint8_t *)arg;out->request_ids[slot]=123;
    if(out->control&0x80U){assert(slot==3);out->flows|=16;out->request_ids[4]=124;}
    return same_request;
}
bool esp32_mquickjs_wifi_twt_sdk_information_matches_native(unsigned slot,
    const esp32_mquickjs_wifi_twt_information_identity_t *id) {
    assert(!locked && slot<8 && id->node==17);return same_request;
}
bool esp32_mquickjs_wifi_twt_sdk_information_resume_allowed_native(const esp32_mquickjs_wifi_twt_information_identity_t *id){
    return ps_enabled && esp32_mquickjs_wifi_twt_sdk_information_matches_native(id->control&7U,id);
}
bool esp32_mquickjs_wifi_twt_sdk_information_resumed_native(const esp32_mquickjs_wifi_twt_information_identity_t *id){
    return esp32_mquickjs_wifi_twt_sdk_information_resume_allowed_native(id);
}
void esp32_mquickjs_wifi_twt_information_timer_bound_native(const esp32_mquickjs_wifi_twt_information_identity_t *id,uint32_t timer){
    assert(!locked && id && timer);
}
void esp32_mquickjs_wifi_twt_information_timer_finished_native(const esp32_mquickjs_wifi_twt_information_identity_t *id,uint32_t timer,esp_err_t error){
    assert(!locked && id && timer);(void)error;
}
esp_err_t esp_timer_create(const esp_timer_create_args_t *args,esp_timer_handle_t *out) {
    assert(!locked && creates<64 && args->dispatch_method==ESP_TIMER_TASK);
    struct native_timer *timer=&timers[creates++];
    if(create_error)return create_error;timer->args=*args;timer->live=true;*out=timer;return 0;
}
esp_err_t esp_timer_stop(esp_timer_handle_t h) {
    assert(!locked);struct native_timer *timer=h;assert(timer->live);++stops;
    if(stop_error)return stop_error;if(!timer->active)return ESP_ERR_INVALID_STATE;
    timer->active=false;return 0;
}
esp_err_t esp_timer_delete(esp_timer_handle_t h) {
    assert(!locked);struct native_timer *timer=h;assert(timer->live);++deletes;
    if(delete_error)return delete_error;if(timer->active)return ESP_ERR_INVALID_STATE;
    timer->live=false;return 0;
}
esp_err_t esp_timer_start_once(esp_timer_handle_t h,uint64_t us) {
    assert(!locked && us==1200);struct native_timer *timer=h;assert(timer->live);
    if(start_error)return start_error;timer->active=true;return 0;
}
void __real_itwt_information_timeout_fn_process(void *arg) {
    assert(!locked && arg);unsigned slot=*(uint8_t *)arg&7U;++processes;
    assert(s_information_timer.entries[slot].busy && !s_information_timer.entries[slot].payload);
    esp32_mquickjs_wifi_twt_information_timer_disarm(&itwt_information_timer[slot]);
    esp32_mquickjs_wifi_twt_information_timer_done(&itwt_information_timer[slot]);
    heap_caps_free(arg); /* Reviewed native tail call owns this free. */
}
int ieee80211_timer_process(int signal,int operation,void *arg) {
    assert(!locked && signal==7 && operation==29 && arg);++posts;
    if(early_process)__wrap_itwt_information_timeout_fn_process(arg);
    else if(!post_error)queued=arg;
    return post_error;
}
static void *payload(unsigned slot) {
    void *p=heap_caps_calloc(1,9,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);assert(p);*(uint8_t *)p=slot;return p;
}
static void install(unsigned slot) {
    assert(esp32_mquickjs_wifi_twt_information_timer_setfn(&itwt_information_timer[slot],
        (void *)itwt_information_timeout_fn,payload(slot)));
}
static void arm(unsigned slot) {assert(esp32_mquickjs_wifi_twt_information_timer_arm(&itwt_information_timer[slot],1200,false));}
static void fire(unsigned i) {timers[i].active=false;timers[i].args.callback(timers[i].args.arg);}
static void deliver(void) {void *arg=queued;queued=NULL;assert(arg);__wrap_itwt_information_timeout_fn_process(arg);}
static void done(unsigned slot) {assert(esp32_mquickjs_wifi_twt_information_timer_done(&itwt_information_timer[slot]));}
static void cold_boot(void) {
    for(unsigned i=0;i<8;++i)assert(!itwt_information_timer[i].timer_arg);
    heap_caps_free(s_information_timer.entries);assert(native_live==0);
    memset(&s_information_timer,0,sizeof(s_information_timer));memset(timers,0,sizeof(timers));
    creates=stops=deletes=posts=processes=0;create_error=start_error=stop_error=delete_error=post_error=0;
    early_process=allocation_error=false;same_request=true;queued=NULL;
}
int main(void) {
    ETSTimer foreign={0};uint32_t revision=99;
    assert(!esp32_mquickjs_wifi_twt_information_timer_setfn(&foreign,NULL,NULL));
    install(3);assert(native_live==2);arm(3);
    assert(esp32_mquickjs_wifi_twt_information_timer_quiescent_native(123,&revision)==ESP_ERR_NOT_FINISHED && revision==99);
    fire(0);assert(posts==1 && queued && native_live==2);deliver();
    assert(processes==1 && native_live==1 && !itwt_information_timer[3].timer_arg);
    assert(esp32_mquickjs_wifi_twt_information_timer_quiescent_native(123,&revision)==0);
    /* Cancel a posted timeout, then reuse its native slot. Old number cannot
     * touch the newly allocated argument or execute the original process. */
    install(3);arm(3);fire(1);void *old=queued;queued=NULL;done(3);assert(native_live==1);
    install(3);arm(3);__wrap_itwt_information_timeout_fn_process(old);
    assert(processes==1 && native_live==2);done(3);cold_boot();
    /* Callback can be dispatched synchronously while the native post returns. */
    install(1);arm(1);early_process=true;post_error=-88;fire(0);
    assert(processes==1 && !s_information_timer.snapshot.fault && native_live==1);cold_boot();
    /* Stop/delete failure retains payload and exact handle; retry only suffix. */
    install(2);arm(2);void *handle=itwt_information_timer[2].timer_arg;
    stop_error=-11;done(2);assert(!deletes && native_live==2 && itwt_information_timer[2].timer_arg==handle);
    stop_error=0;delete_error=-12;done(2);unsigned stopped=stops;
    assert(deletes==1 && native_live==2 && itwt_information_timer[2].timer_arg==handle);
    delete_error=0;done(2);assert(stops==stopped && deletes==2 && native_live==1);
    assert(s_information_timer.snapshot.fault==-11);cold_boot();
    /* Post failure leaves the argument for native cleanup, never esp_timer. */
    install(4);arm(4);post_error=-13;fire(0);
    assert(native_live==2 && s_information_timer.snapshot.fault==-13);done(4);cold_boot();
    /* Changed association/request discards old control mutation, frees once. */
    install(5);arm(5);fire(0);same_request=false;deliver();
    assert(!processes && native_live==1 && !itwt_information_timer[5].timer_arg);cold_boot();
    /* Native cleanup must preflight the whole all-flow timer before mutating
     * anything; closing only one Agreement cannot cancel another's timer. */
    void *shared=payload(3);*(uint8_t *)shared|=0x80;
    assert(esp32_mquickjs_wifi_twt_information_timer_setfn(&itwt_information_timer[3],(void *)itwt_information_timeout_fn,shared));
    arm(3);unsigned before=stops;
    assert(esp32_mquickjs_wifi_twt_information_timer_cleanup_native(123,8)==ESP_ERR_NOT_FINISHED && stops==before && native_live==2);
    assert(esp32_mquickjs_wifi_twt_information_timer_cleanup_native(123,24)==0 && native_live==1);cold_boot();
    /* All-flow close consent breaks the teardown-scope/timer dependency.
     * A queued callback must not regain permission after shared cancellation. */
    shared=payload(3);*(uint8_t *)shared|=0x80;
    assert(esp32_mquickjs_wifi_twt_information_timer_setfn(&itwt_information_timer[3],(void *)itwt_information_timeout_fn,shared));
    arm(3);fire(0);old=queued;queued=NULL;before=stops;
    assert(esp32_mquickjs_wifi_twt_information_timer_cleanup_native(123,8)==ESP_ERR_NOT_FINISHED && stops==before);
    other_closing=true;delete_error=-12;
    assert(esp32_mquickjs_wifi_twt_information_timer_cleanup_native(123,8)==-12 && native_live==2);
    stopped=stops;delete_error=0;
    assert(!esp32_mquickjs_wifi_twt_information_timer_cleanup_native(123,8) && native_live==1 && stops==stopped);
    __wrap_itwt_information_timeout_fn_process(old);assert(!processes);
    other_closing=false;cold_boot();
    create_error=-14;install(0);assert(native_live==1 && !itwt_information_timer[0].timer_arg);cold_boot();
    install(0);start_error=-15;arm(0);done(0);assert(native_live==1);cold_boot();
    /* Ledger allocation fails after the SDK already allocated its argument. */
    void *arg=payload(0);allocation_error=true;
    assert(esp32_mquickjs_wifi_twt_information_timer_setfn(&itwt_information_timer[0],(void *)itwt_information_timeout_fn,arg));
    assert(!native_live && !creates && s_information_timer.snapshot.fault==ESP_ERR_NO_MEM);cold_boot();
    /* Replacement admission is read-only, including a callback already queued. */
    install(3);arm(3);before=stops;
    assert(!esp32_mquickjs_wifi_twt_information_timer_replaceable_native(123,3) && stops==before);
    assert(esp32_mquickjs_wifi_twt_information_timer_replaceable_native(124,3)==ESP_ERR_INVALID_STATE && stops==before);
    fire(0);assert(!esp32_mquickjs_wifi_twt_information_timer_replaceable_native(123,3));
    ps_enabled=false;deliver();assert(!processes && s_information_timer.snapshot.fault==ESP_ERR_INVALID_STATE);
    ps_enabled=true;cold_boot();
    /* Whole connection retirement owns all flows, unlike one handle close. */
    shared=payload(3);*(uint8_t *)shared|=0x80;
    assert(esp32_mquickjs_wifi_twt_information_timer_setfn(&itwt_information_timer[3],(void *)itwt_information_timeout_fn,shared));
    arm(3);fire(0);
    esp32_mquickjs_wifi_twt_information_timers_connection_closed_native();
    assert(!s_information_timer.entries[3].active && native_live==1 && !itwt_information_timer[3].timer_arg);
    deliver();assert(!processes && !s_information_timer.snapshot.fault);cold_boot();
    install(2);arm(2);stop_error=-31;
    esp32_mquickjs_wifi_twt_information_timers_connection_closed_native();
    assert(native_live==2 && itwt_information_timer[2].timer_arg && !s_information_timer.entries[2].active);
    stop_error=0;delete_error=-32;
    esp32_mquickjs_wifi_twt_information_timers_connection_closed_native();
    stopped=stops;delete_error=0;
    esp32_mquickjs_wifi_twt_information_timers_connection_closed_native();
    assert(stops==stopped && native_live==1 && !itwt_information_timer[2].timer_arg);cold_boot();
    assert(!locked);return 0;
}
