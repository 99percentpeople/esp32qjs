
#define WIFI_MODE_STA 1
#define WIFI_MODE_APSTA 3
#define ESP_ERR_INVALID_ARG -21
#define ESP_ERR_NOT_SUPPORTED -22
#define ESP_ERR_NO_MEM -23
#define MALLOC_CAP_8BIT 0
typedef struct {unsigned char secret[184];} wifi_config_t;
static bool s_wifi_ap_stop_cleanup,alloc_fail;
static void *temporary;static unsigned temporary_frees;
static unsigned phase_calls[13];static int phase_failure,off_cleanup_failure;
static bool checkpoint,reconstructed,replayed;
static bool stopped_source,history_missing,off_source,off_needs_ap,retry_allowed;
static unsigned retry_admissions;
static wifi_mode_t stopped_mode;
/* Radio is the injected phase boundary here; its real predicate/registry/claim
 * are exercised separately by test_wifi_restart_admission. */
static int esp32_mquickjs_wifi_radio_begin_stopped_restart(
    esp32_mquickjs_wifi_radio_lifecycle_t *token,esp32_mquickjs_wifi_radio_restart_selection_t *selection) {
    ++begin_calls;assert(token==&s_wifi_lifecycle && !token->identity);
    bool allow_ap=selection->allow_ap_restart;
    *selection=(esp32_mquickjs_wifi_radio_restart_selection_t){.allow_ap_restart=allow_ap};
    if(!stopped_source || running || foreign_owner || s_wifi_application.acquired ||
        s_wifi_state.radio_lease.acquired || s_ap_lease.acquired)return ESP_ERR_INVALID_STATE;
    native_token=(esp32_mquickjs_wifi_radio_lifecycle_t){9,41};*token=native_token;
    selection->cold=stopped_mode==WIFI_MODE_NULL && !off_source;
    selection->restore_off=off_source;
    selection->mode=stopped_mode==WIFI_MODE_NULL ? WIFI_MODE_STA : stopped_mode;
    if(off_source && off_needs_ap){assert(allow_ap);selection->mode=WIFI_MODE_APSTA;}
    return ESP_OK;
}
static void esp32_mquickjs_wifi_radio_stop_snapshot(esp32_mquickjs_wifi_radio_stop_snapshot_t *out) {
    memset(out,0,sizeof(*out));out->unchanged=stopped_source && !history_missing;
}
static wifi_mode_t target_mode;
static int esp32_mquickjs_wifi_radio_admit_restart_retry(const esp32_mquickjs_wifi_radio_lifecycle_t *t,
    esp32_mquickjs_wifi_radio_restart_selection_t *selection) {
    ++retry_admissions;
    if(!retry_allowed || !exact(t) || !checkpoint || foreign_owner || recovery_admitted)return ESP_ERR_INVALID_STATE;
    if(off_source && (target_mode&WIFI_MODE_AP) && !selection->allow_ap_restart)return ESP_ERR_INVALID_STATE;
    selection->mode=target_mode;selection->restore_off=off_source;
    selection->cold=stopped_mode==WIFI_MODE_NULL&&!off_source;return ESP_OK;
}
static void *heap_caps_calloc(size_t n,size_t size,int caps) {
    (void)caps;assert(!temporary && (!s_wifi_lifecycle.identity || retry_allowed));
    if(alloc_fail)return NULL;temporary=calloc(n,size);assert(temporary);return temporary;
}
static void heap_caps_free(void *p) {
    assert(p==temporary);
    for(unsigned i=0;i<sizeof(wifi_config_t);++i)assert(((unsigned char *)p)[i]==0);
    free(p);temporary=NULL;++temporary_frees;
}
static int phase(unsigned step,const esp32_mquickjs_wifi_radio_lifecycle_t *t) {
    assert(exact(t) && s_wifi_configuration_cleanup && !s_wifi_application.acquired && !s_wifi_state.radio_lease.acquired && !s_ap_lease.acquired);
    ++phase_calls[step];return phase_failure==(int)step ? -(100+(int)step) : ESP_OK;
}
#if CONFIG_ESP_WIFI_WAPI_PSK
static bool selected_wapi=true;
static int esp32_mquickjs_wifi_radio_wapi_select(const esp32_mquickjs_wifi_radio_lifecycle_t*t,wifi_mode_t mode,bool enabled){
    assert(checkpoint&&!running&&!sta_storage&&!s_ap_netif&&mode==target_mode);
    int error=phase(10,t);if(!error)selected_wapi=enabled;return error;
}
#endif
static int esp32_mquickjs_wifi_radio_checkpoint_restart_lifecycle(const esp32_mquickjs_wifi_radio_lifecycle_t *t,wifi_mode_t mode) {
    if(stopped_source&&stopped_mode==WIFI_MODE_NULL&&!off_source)assert(!running&&!sta_storage&&mode==WIFI_MODE_STA);
    else assert((running || stopped_source) && sta_storage);
    target_mode=mode;
    if(history_missing)assert((s_ap_netif!=NULL)==((mode&WIFI_MODE_AP)!=0));
    int err=phase(1,t);checkpoint=true;if(!err)running=false;return err;
}
static int esp32_mquickjs_wifi_radio_rebuild_restart_lifecycle(const esp32_mquickjs_wifi_radio_lifecycle_t *t,wifi_mode_t mode) {
    assert(checkpoint && mode==target_mode && !running && !sta_storage && !s_ap_netif);
    int err=phase(2,t);if(!err)reconstructed=true;return err;
}
static int esp32_mquickjs_wifi_prepare_for_configuration(const esp32_mquickjs_wifi_radio_lifecycle_t *t) {
    assert((reconstructed || stopped_source) && !running && !sta_storage && !s_wifi_state.runtime_cleanup_pending);
    int err=phase(reconstructed ? 3 : 10,t);sta_storage=true; /* Partial allocation remains on failure. */
    if(err)s_wifi_state.runtime_cleanup_pending=true;return err;
}
static int esp32_mquickjs_wifi_ap_prepare_for_configuration(const esp32_mquickjs_wifi_radio_lifecycle_t *t) {
    assert((reconstructed || (stopped_source && history_missing)) && sta_storage && !running && !s_ap_netif && !s_ap_coordinator.identity);
    int err=phase(reconstructed ? 4 : 11,t);s_ap_netif=(void *)2;s_ap_cleanup_pending=err!=ESP_OK;return err;
}
static int esp32_mquickjs_wifi_radio_replay_restart_lifecycle(const esp32_mquickjs_wifi_radio_lifecycle_t *t,wifi_mode_t mode) {
    assert(mode==target_mode && reconstructed && sta_storage && !running);
    assert((s_ap_netif!=NULL)==((mode&WIFI_MODE_AP)!=0));
    int err=phase(5,t);if(err)running=true;else replayed=true;return err;
}
static int esp32_mquickjs_wifi_retire_for_configuration(const esp32_mquickjs_wifi_radio_lifecycle_t *t) {
    assert(replayed && target_mode==WIFI_MODE_AP && sta_storage && s_ap_netif && !running);
    int err=phase(6,t);if(err){s_wifi_state.cleanup_stage="netif-detach";return err;}
    return wifi_cleanup_helper(false);
}
static int esp32_mquickjs_wifi_radio_copy_stopped_ap_configuration(const esp32_mquickjs_wifi_radio_lifecycle_t *t,wifi_config_t *out) {
    assert(replayed && !running && out==temporary && s_ap_netif);
    memset(out,0x5a,sizeof(*out));return phase(7,t);
}
static esp32_mquickjs_wifi_radio_lease_t *esp32_mquickjs_wifi_ap_configuration_slot(const esp32_mquickjs_wifi_radio_lifecycle_t *t) {
    assert(replayed && !running && s_ap_netif);return phase(8,t)==ESP_OK ? &s_ap_lease : NULL;
}
static int esp32_mquickjs_wifi_radio_resume_off_restart_lifecycle(esp32_mquickjs_wifi_radio_lifecycle_t *t,wifi_mode_t mode) {
    assert(off_source && replayed && mode==target_mode && !running && sta_storage);
    assert((s_ap_netif!=NULL)==((mode&WIFI_MODE_AP)!=0));
    int err=phase(12,t);running=err!=ESP_OK;if(!err){expect_stop_only=true;fail_at=off_cleanup_failure;}return err;
}
static int esp32_mquickjs_wifi_radio_resume_lifecycle(esp32_mquickjs_wifi_radio_lifecycle_t *t,wifi_mode_t mode,bool start,
    esp32_mquickjs_wifi_radio_lease_t *app,esp32_mquickjs_wifi_radio_lease_t *sta,
    esp32_mquickjs_wifi_radio_lease_t *ap,const void *controls) {
    assert(replayed && mode==target_mode && start && !controls && !running);
    assert(app==((mode&WIFI_MODE_STA) ? &s_wifi_application : NULL));
    assert(sta==((mode&WIFI_MODE_STA) ? &s_wifi_state.radio_lease : NULL));
    assert(ap==((mode&WIFI_MODE_AP) ? &s_ap_lease : NULL));
    assert(sta_storage==((mode&WIFI_MODE_STA)!=0));
    int err=phase(9,t);running=true;if(err)return err;
    if(app)*app=(esp32_mquickjs_wifi_radio_lease_t){10,11,true};
    if(sta)*sta=(esp32_mquickjs_wifi_radio_lease_t){10,12,true};
    if(ap)*ap=(esp32_mquickjs_wifi_radio_lease_t){10,13,true};
    memset(t,0,sizeof(*t));memset(&native_token,0,sizeof(native_token));checkpoint=false;return ESP_OK;
}
