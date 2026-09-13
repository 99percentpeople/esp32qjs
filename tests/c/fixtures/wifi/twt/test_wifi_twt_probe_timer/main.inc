
static void cancelled_wake(void) {wake_state.held=false;}
static void (*installed)(void *),(*post_hook)(void);
static void *installed_arg,*queued;
static unsigned sets,disarms,dones,arms,posts,processes,foreign_calls;
static int post_error,create_error,start_error,stop_error,delete_error;
static bool process_before_post_return,fire_on_disarm,fire_on_done,active_timer;
static uint64_t armed_us;
void itwt_probe_timeout_fn(void *arg) {(void)arg;assert(0);}
void __real_esp_coex_common_timer_setfn_wrapper(void *timer,void *callback,void *arg) {
    assert(!locked && timer!=&itwt_probe_timer);++foreign_calls;
    ((ETSTimer *)timer)->timer_arg=(void *)3;
    installed=(void (*)(void *))callback;installed_arg=arg;
}
void __real_esp_coex_common_timer_disarm_wrapper(void *timer) {
    assert(!locked && timer!=&itwt_probe_timer);++foreign_calls;
}
void __real_esp_coex_common_timer_done_wrapper(void *timer) {
    assert(!locked && timer!=&itwt_probe_timer);++foreign_calls;
    ((ETSTimer *)timer)->timer_arg=NULL;
}
void __real_esp_coex_common_timer_arm_us_wrapper(void *timer,uint32_t us,bool repeat) {
    assert(!locked && timer!=&itwt_probe_timer && us==37 && repeat);++foreign_calls;
}
void __real_ets_timer_arm(ETSTimer *timer,uint32_t ms,bool repeat) {
    assert(!locked && timer!=&itwt_probe_timer && ms==29 && repeat);++foreign_calls;
}
esp_err_t esp_timer_create(const esp_timer_create_args_t *args,esp_timer_handle_t *out) {
    assert(!locked && args && out && args->dispatch_method==ESP_TIMER_TASK);++sets;
    if(create_error)return create_error;
    installed=args->callback;installed_arg=args->arg;*out=(void *)(uintptr_t)(0x100+sets);return ESP_OK;
}
esp_err_t esp_timer_stop(esp_timer_handle_t timer) {
    assert(!locked && timer);++disarms;
    if(fire_on_disarm){fire_on_disarm=false;installed(installed_arg);}
    if(stop_error)return stop_error;
    if(!active_timer)return ESP_ERR_INVALID_STATE;
    active_timer=false;return ESP_OK;
}
esp_err_t esp_timer_start_once(esp_timer_handle_t timer,uint64_t us) {
    assert(!locked && timer);++arms;armed_us=us;
    if(start_error)return start_error;
    active_timer=true;return ESP_OK;
}
esp_err_t esp_timer_delete(esp_timer_handle_t timer) {
    assert(!locked && timer);++dones;
    if(fire_on_done){fire_on_done=false;installed(installed_arg);}
    if(delete_error)return delete_error;
    return active_timer?ESP_ERR_INVALID_STATE:ESP_OK;
}
int ieee80211_timer_process(int signal,int op,void *arg) {
    assert(!locked && signal==7 && op==28 && arg);++posts;queued=arg;
    if(post_hook)post_hook();
    if(process_before_post_return)__wrap_itwt_probe_timeout_fn_process(arg);
    return post_error;
}
void __real_itwt_probe_timeout_fn_process(void *arg) {
    assert(!locked && current_active && current_node && *(uint8_t *)arg==current_phase);
    assert(arg!=(void *)(current_node+1057));++processes;current_active=false;
    __wrap_esp_coex_common_timer_disarm_wrapper(&itwt_probe_timer);
    __wrap_esp_coex_common_timer_done_wrapper(&itwt_probe_timer);
}
static esp32_mquickjs_wifi_twt_probe_timer_snapshot_t snapshot(void) {
    esp32_mquickjs_wifi_twt_probe_timer_snapshot_t s;esp32_mquickjs_wifi_twt_probe_timer_snapshot(&s);return s;
}
static void create(void) {
    __wrap_esp_coex_common_timer_setfn_wrapper(&itwt_probe_timer,(void *)itwt_probe_timeout_fn,(void *)(current_node+1057));
    if(itwt_probe_timer.timer_arg) {
        assert(installed!=(void (*)(void *))itwt_probe_timeout_fn);
        assert(installed_arg!=(void *)(current_node+1057));
    }
}
static void clear_timer(void) {__wrap_esp_coex_common_timer_done_wrapper(&itwt_probe_timer);}
static void next_phase(void) {
    clear_timer();current_phase=0;create();post_hook=NULL;
}
static void cold_boot(void) {
    memset(&s_probe_timer,0,sizeof(s_probe_timer));memset(&itwt_probe_timer,0,sizeof(itwt_probe_timer));
    memset(&wake_state,0,sizeof(wake_state));
    sets=disarms=dones=posts=processes=arms=foreign_calls=aborted=0;
    current_node=0x100000;current_phase=1;current_active=true;control_error=0;
    installed=NULL;installed_arg=queued=NULL;post_hook=NULL;post_error=0;
    process_before_post_return=fire_on_disarm=fire_on_done=false;
    create_error=start_error=stop_error=delete_error=0;active_timer=false;armed_us=0;
}
int main(void) {
    create();void (*callback)(void *)=installed;void *first=installed_arg;
    assert(snapshot().current_identity==1 && snapshot().last_identity==1);
    __wrap_itwt_probe_timeout_fn_process(first);assert(processes==0); /* Not fired. */
    callback(first);assert(posts==1 && snapshot().fired);
    callback(first);assert(posts==1); /* One post for this timer identity. */
    clear_timer();current_phase=0;create();void *second=installed_arg;
    assert(second!=first && snapshot().last_identity==2);
    __wrap_itwt_probe_timeout_fn_process(first);callback(first);assert(processes==0 && posts==1);
    installed(second);__wrap_itwt_probe_timeout_fn_process(second);
    assert(processes==1 && !snapshot().current_identity);
    __wrap_itwt_probe_timeout_fn_process(second);assert(processes==1);

    cold_boot();create();installed(installed_arg);current_node=0;
    __wrap_itwt_probe_timeout_fn_process(queued);assert(processes==0);
    cold_boot();create();installed(installed_arg);current_node+=0x1000;
    __wrap_itwt_probe_timeout_fn_process(queued);assert(processes==0);
    cold_boot();create();installed(installed_arg);current_phase=0;
    __wrap_itwt_probe_timeout_fn_process(queued);assert(processes==0);
    cold_boot();create();installed(installed_arg);current_active=false;
    __wrap_itwt_probe_timeout_fn_process(queued);assert(processes==0);

    cold_boot();create();fire_on_disarm=true;
    __wrap_esp_coex_common_timer_disarm_wrapper(&itwt_probe_timer);assert(posts==0);
    clear_timer();create();fire_on_done=true;clear_timer();assert(posts==0);
    cold_boot();create();process_before_post_return=true;installed(installed_arg);
    assert(processes==1 && !snapshot().current_identity);

    cold_boot();create();post_error=71;installed(installed_arg);
    assert(snapshot().post_error==71 && snapshot().failed_identity==1);
    assert(snapshot().fault_stage==ESP32_MQUICKJS_WIFI_TWT_PROBE_TIMER_POST && snapshot().cleanup_pending);
    assert(esp32_mquickjs_wifi_twt_probe_timer_finish_native()==ESP_FAIL && aborted==1 && posts==1);
    clear_timer();create();assert(itwt_probe_timer.timer_arg==NULL && sets==1);
    installed(installed_arg);assert(posts==1);
    /* A late error from an old post must not poison a replacement timer. */
    cold_boot();create();post_error=72;post_hook=next_phase;installed(installed_arg);
    assert(snapshot().current_identity==2 && snapshot().post_error==0);

    cold_boot();s_probe_timer.snapshot.last_identity=UINT32_MAX-1;create();
    assert(snapshot().current_identity==UINT32_MAX);clear_timer();create();
    assert(snapshot().fault==ESP_ERR_NO_MEM && itwt_probe_timer.timer_arg==NULL && snapshot().last_identity==UINT32_MAX);
    assert(snapshot().fault_stage==ESP32_MQUICKJS_WIFI_TWT_PROBE_TIMER_IDENTITY && sets==1);
    installed(installed_arg);assert(posts==0);
    cold_boot();current_active=false;create();assert(snapshot().fault==ESP_ERR_INVALID_STATE && sets==0);
    cold_boot();ETSTimer foreign={0};
    __wrap_esp_coex_common_timer_setfn_wrapper(&foreign,(void *)itwt_probe_timeout_fn,(void *)91);
    assert(installed==(void (*)(void *))itwt_probe_timeout_fn && installed_arg==(void *)91);
    __wrap_esp_coex_common_timer_disarm_wrapper(&foreign);__wrap_esp_coex_common_timer_done_wrapper(&foreign);
    __wrap_ets_timer_arm(&foreign,29,true);__wrap_esp_coex_common_timer_arm_us_wrapper(&foreign,37,true);
    assert(snapshot().last_identity==0 && snapshot().fault==0 && foreign_calls==5);

    cold_boot();create();__wrap_ets_timer_arm(&itwt_probe_timer,UINT32_MAX,false);
    assert(armed_us==UINT64_C(4294967295000) && arms==1);
    __wrap_esp_coex_common_timer_disarm_wrapper(&itwt_probe_timer);clear_timer();current_phase=0;create();
    __wrap_esp_coex_common_timer_arm_us_wrapper(&itwt_probe_timer,37,false);
    assert(armed_us==37 && arms==2 && snapshot().phase==0);
    __wrap_esp_coex_common_timer_disarm_wrapper(&itwt_probe_timer);clear_timer();
    cold_boot();create_error=ESP_ERR_NO_MEM;create();
    assert(!itwt_probe_timer.timer_arg && snapshot().fault_stage==ESP32_MQUICKJS_WIFI_TWT_PROBE_TIMER_CREATE);
    __wrap_ets_timer_arm(&itwt_probe_timer,10,false);assert(arms==0);
    assert(esp32_mquickjs_wifi_twt_probe_timer_finish_native()==ESP_ERR_NO_MEM);
    assert(aborted==1 && !current_active && !snapshot().cleanup_pending && control_error==ESP_ERR_NO_MEM);
    esp32_mquickjs_wifi_twt_probe_timer_finish_native();assert(aborted==1);
    cold_boot();create();start_error=81;__wrap_ets_timer_arm(&itwt_probe_timer,10,false);
    assert(snapshot().fault==81 && snapshot().fault_stage==ESP32_MQUICKJS_WIFI_TWT_PROBE_TIMER_START);
    assert(esp32_mquickjs_wifi_twt_probe_timer_finish_native()==81 && aborted==1 && itwt_probe_timer.timer_arg==NULL);
    cold_boot();current_phase=0;create();start_error=82;
    __wrap_esp_coex_common_timer_arm_us_wrapper(&itwt_probe_timer,10,false);
    assert(esp32_mquickjs_wifi_twt_probe_timer_finish_native()==82 && aborted==1 && posts==0);
    cold_boot();create();void *retained=itwt_probe_timer.timer_arg;delete_error=83;clear_timer();
    assert(itwt_probe_timer.timer_arg==retained && snapshot().fault_stage==ESP32_MQUICKJS_WIFI_TWT_PROBE_TIMER_DELETE);
    create();assert(sets==1 && itwt_probe_timer.timer_arg==retained); /* Never overwrite retained storage. */
    assert(esp32_mquickjs_wifi_twt_probe_timer_finish_native()==83 && aborted==1 && snapshot().cleanup_error==83);
    delete_error=0;esp32_mquickjs_wifi_twt_probe_timer_finish_native();
    assert(!itwt_probe_timer.timer_arg && snapshot().cleanup_error==0 && aborted==1 && snapshot().fault==83);
    cold_boot();create();__wrap_ets_timer_arm(&itwt_probe_timer,10,false);retained=itwt_probe_timer.timer_arg;
    stop_error=84;__wrap_esp_coex_common_timer_disarm_wrapper(&itwt_probe_timer);clear_timer();
    assert(itwt_probe_timer.timer_arg==retained && snapshot().fault==84);
    esp32_mquickjs_wifi_twt_probe_timer_finish_native();assert(aborted==1 && itwt_probe_timer.timer_arg==retained);
    stop_error=0;esp32_mquickjs_wifi_twt_probe_timer_finish_native();assert(!itwt_probe_timer.timer_arg && aborted==1);
    cold_boot();create_error=85;create();current_node+=0x1000;
    esp32_mquickjs_wifi_twt_probe_timer_finish_native();assert(aborted==0 && current_active && snapshot().cleanup_pending);
    cold_boot();create();__wrap_ets_timer_arm(&itwt_probe_timer,0,false);
    assert(snapshot().fault==ESP_OK && arms==1 && armed_us==0); /* Native immediate timer semantics. */
    cold_boot();create();__wrap_esp_coex_common_timer_arm_us_wrapper(&itwt_probe_timer,10,true);
    assert(snapshot().fault==ESP_ERR_INVALID_ARG && arms==0);
    cold_boot();create_error=85;create();wake_state.fault=86;
    assert(esp32_mquickjs_wifi_twt_probe_timer_finish_native()==86 && control_error==86);
    assert(aborted==0 && current_active && snapshot().cleanup_pending); /* Ambiguous PM stays retained. */
    cold_boot();wake_state.held=true;create();__wrap_ets_timer_arm(&itwt_probe_timer,10,false);
    first=installed_arg;callback=installed;fire_on_disarm=fire_on_done=true;
    assert(esp32_mquickjs_wifi_twt_probe_timer_cancel_native()==ESP_OK);
    assert(!current_active && !wake_state.held && aborted==1 && !itwt_probe_timer.timer_arg);
    assert(!snapshot().current_identity && !snapshot().cleanup_pending && posts==0);
    callback(first);__wrap_itwt_probe_timeout_fn_process(first);assert(posts==0 && processes==0);
    unsigned before_disarm=disarms,before_done=dones;
    assert(esp32_mquickjs_wifi_twt_probe_timer_cancel_native()==ESP_OK);
    assert(aborted==1 && disarms==before_disarm && dones==before_done);
    /* Delete failure relinquishes authority once, retaining only its handle. */
    cold_boot();wake_state.held=true;create();delete_error=87;
    retained=itwt_probe_timer.timer_arg;
    assert(esp32_mquickjs_wifi_twt_probe_timer_cancel_native()==87);
    assert(itwt_probe_timer.timer_arg==retained && aborted==1 && !wake_state.held && !current_active);
    assert(!snapshot().current_identity && !snapshot().cleanup_pending && snapshot().fault==87);
    delete_error=0;assert(esp32_mquickjs_wifi_twt_probe_timer_cancel_native()==ESP_OK);
    assert(!itwt_probe_timer.timer_arg && aborted==1 && snapshot().fault==87);
    /* Stop failure keeps an armed handle; repeated cleanup must stop it. */
    cold_boot();wake_state.held=true;create();__wrap_ets_timer_arm(&itwt_probe_timer,10,false);stop_error=88;
    assert(esp32_mquickjs_wifi_twt_probe_timer_cancel_native()==ESP_ERR_INVALID_STATE);
    assert(snapshot().fault==88 && active_timer && itwt_probe_timer.timer_arg && aborted==1);
    stop_error=0;assert(esp32_mquickjs_wifi_twt_probe_timer_cancel_native()==ESP_OK && !active_timer && aborted==1);
    /* Never mutate a new node/phase or an unowned active probe. */
    for(unsigned mismatch=0;mismatch<6;++mismatch) {
        cold_boot();wake_state.held=true;create();retained=itwt_probe_timer.timer_arg;
        if(mismatch==0)current_node+=0x1000;
        if(mismatch==1)current_phase=0;
        if(mismatch==2)wake_state.held=false;
        if(mismatch==3)current_active=false;
        if(mismatch==4)wake_state.acquiring=true;
        if(mismatch==5)wake_state.releasing=true;
        assert(esp32_mquickjs_wifi_twt_probe_timer_cancel_native()==ESP_ERR_INVALID_STATE);
        assert(itwt_probe_timer.timer_arg==retained && !disarms && !dones && !aborted);
    }
    cold_boot();wake_state.held=true;create();wake_state.fault=89;
    assert(esp32_mquickjs_wifi_twt_probe_timer_cancel_native()==89 && !disarms && !dones && !aborted);
    /* An already completed probe may leave only a failed-delete suffix. */
    cold_boot();create();current_active=false;delete_error=90;
    assert(esp32_mquickjs_wifi_twt_probe_timer_cancel_native()==90 && !aborted);
    delete_error=0;assert(esp32_mquickjs_wifi_twt_probe_timer_cancel_native()==ESP_OK && !aborted);
    /* A failed late queue post can be cleaned by the native cancel entry. */
    cold_boot();wake_state.held=true;create();post_error=91;installed(installed_arg);
    assert(snapshot().cleanup_pending && snapshot().post_error==91);
    assert(esp32_mquickjs_wifi_twt_probe_timer_cancel_native()==ESP_OK);
    assert(!snapshot().cleanup_pending && snapshot().fault==ESP_FAIL && !current_active && aborted==1);
    return 0;
}
