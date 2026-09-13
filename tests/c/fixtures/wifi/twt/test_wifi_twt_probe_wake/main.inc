
static unsigned shared_references=3,ups,downs;
static bool release_inside_up,acquire_inside_down;
static esp32_mquickjs_wifi_twt_probe_wake_snapshot_t snapshot(void) {
    esp32_mquickjs_wifi_twt_probe_wake_snapshot_t s;esp32_mquickjs_wifi_twt_probe_wake_snapshot(&s);return s;
}
void pm_wake_up(void) {
    assert(!locked && snapshot().held && snapshot().acquiring);++ups;++shared_references;
    if(release_inside_up){release_inside_up=false;esp32_mquickjs_wifi_twt_probe_wake_done_native();}
}
void pm_wake_done(void) {
    assert(!locked && !snapshot().held && snapshot().releasing && shared_references>3);++downs;--shared_references;
    if(acquire_inside_down){acquire_inside_down=false;esp32_mquickjs_wifi_twt_probe_wake_up_native();}
}
static void cold_boot(void) {
    memset(&s_probe_wake,0,sizeof(s_probe_wake));shared_references=3;ups=downs=0;
    release_inside_up=acquire_inside_down=false;
}
int main(void) {
    esp32_mquickjs_wifi_twt_probe_wake_done_native();assert(shared_references==3 && downs==0);
    esp32_mquickjs_wifi_twt_probe_wake_up_native();assert(snapshot().held && shared_references==4 && ups==1);
    esp32_mquickjs_wifi_twt_probe_wake_done_native();assert(!snapshot().held && shared_references==3 && downs==1);
    /* A retained timer's later SDK stop releases no additional PM owner. */
    esp32_mquickjs_wifi_twt_probe_wake_done_native();assert(shared_references==3 && downs==1 && snapshot().fault==0);
    for(unsigned i=0;i<100;++i){esp32_mquickjs_wifi_twt_probe_wake_up_native();esp32_mquickjs_wifi_twt_probe_wake_done_native();}
    assert(shared_references==3 && ups==101 && downs==101 && !snapshot().held);
    cold_boot();esp32_mquickjs_wifi_twt_probe_wake_up_native();esp32_mquickjs_wifi_twt_probe_wake_up_native();
    assert(snapshot().fault==ESP_ERR_INVALID_STATE && snapshot().held && ups==1);
    esp32_mquickjs_wifi_twt_probe_wake_done_native();assert(downs==0 && shared_references==4); /* Quarantine, not false drain. */
    cold_boot();release_inside_up=true;esp32_mquickjs_wifi_twt_probe_wake_up_native();
    assert(snapshot().fault==ESP_ERR_INVALID_STATE && snapshot().held && shared_references==4 && downs==0);
    cold_boot();esp32_mquickjs_wifi_twt_probe_wake_up_native();acquire_inside_down=true;
    esp32_mquickjs_wifi_twt_probe_wake_done_native();assert(snapshot().fault==ESP_ERR_INVALID_STATE && ups==1 && shared_references==3);
    return 0;
}
