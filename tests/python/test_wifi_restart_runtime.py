"""Deferred real restart executor and central cleanup; native boundaries injected.

This checks runtime sequencing, not SDK rebuild, event delivery or RF. The
Radio checkpoint/replay and netif retirement have separate production fixtures.
"""
import unittest

from test_wifi_configuration_cleanup import WiFiConfigurationCleanup, WIFI, AP
from test_wifi_config_controls import structure
from test_wireless_control_regression import compile_run
from wireless_vm_fixture import CORE, extract


def runtime_code(ap, wapi=False):
    code = WiFiConfigurationCleanup().code()
    code = '#include <stdlib.h>\n#define CONFIG_ESP_WIFI_SOFTAP_SUPPORT %d\n' % ap + code
    code = '#define CONFIG_ESP_WIFI_WAPI_PSK %d\n' % wapi + code
    code = code.replace('assert(cleanup);return exact(t)', '(void)cleanup;return exact(t)')
    code = code.replace('assert(!finish && native_token.identity && !running && !s_ap_netif);',
                        'assert(!finish && native_token.identity && !running);')
    code = code.replace('if(!sta_storage)return 0;',
                        'if(!sta_storage){s_wifi_state.runtime_cleanup_pending=false;return 0;}')
    header = (WIFI.parents[3] / 'internal/esp32_mquickjs_wifi.h').read_text()
    code += structure(header, 'esp32_mquickjs_wifi_configuration_execution_t')
    radio_header = (WIFI.parents[3] / 'internal/esp32_mquickjs_wifi_radio.h').read_text()
    code += structure(radio_header, 'esp32_mquickjs_wifi_radio_stop_snapshot_t')
    code += structure(radio_header, 'esp32_mquickjs_wifi_radio_restart_selection_t')
    code += extract((CORE / 'esp32_mquickjs_wireless_core.c').read_text(), 'esp32_mquickjs_wireless_secure_zero')
    code += BOUNDARIES
    code += extract(AP.read_text(), 'esp32_mquickjs_wifi_ap_begin_stopped_restart')
    code += ''.join(extract(WIFI.read_text(), name) for name in (
        'wifi_restart_restore_interfaces', 'wifi_restart_retry_interfaces', 'wifi_restart_interfaces_inner', 'esp32_mquickjs_wifi_restart_interfaces',
        'esp32_mquickjs_wifi_restart_stopped_interfaces'))
    return code


class WiFiRestartRuntime(unittest.TestCase):
    def test_explicit_retry_reuses_checkpoint_and_retires_helpers_before_rebuild(self):
        setup = MAIN[:MAIN.index('int main(void)')]
        setup += STOPPED_MAIN[:STOPPED_MAIN.index('int main(void)')]
        for ap in (0, 1):
            with self.subTest(ap=ap):
                compile_run(self, runtime_code(ap) + setup + RETRY_MAIN)

    def test_off_source_retires_helpers_and_token_only_after_final_stop(self):
        setup = MAIN[:MAIN.index('int main(void)')]
        setup += STOPPED_MAIN[:STOPPED_MAIN.index('int main(void)')]
        for ap in (0, 1):
            with self.subTest(ap=ap):
                compile_run(self, runtime_code(ap) + setup + OFF_MAIN)

    def test_cold_source_defers_helpers_until_init_and_keeps_failure_cleanup(self):
        setup = MAIN[:MAIN.index('int main(void)')]
        setup += STOPPED_MAIN[:STOPPED_MAIN.index('int main(void)')]
        for ap in (0, 1):
            with self.subTest(ap=ap):
                compile_run(self, runtime_code(ap) + setup + r'''
int main(void){
 esp32_mquickjs_wifi_configuration_execution_t result;
 stopped_setup(WIFI_MODE_NULL);history_missing=true;
 assert(!esp32_mquickjs_wifi_restart_stopped_interfaces(false,&result));
 assert(result.admitted&&result.configuration_attempted&&result.resume_attempted&&running);
 assert(target_mode==WIFI_MODE_STA&&reconstructed&&replayed&&!phase_calls[10]&&!phase_calls[11]);
 assert(phase_calls[1]==1&&phase_calls[2]==1&&phase_calls[3]==1&&phase_calls[9]==1);
 assert(!s_wifi_lifecycle.identity&&!s_wifi_state.runtime_cleanup_pending&&!temporary);
 for(unsigned failure=1;failure<=9;++failure){
  if(failure>=4&&failure!=5&&failure!=9)continue;
  stopped_setup(WIFI_MODE_NULL);history_missing=true;phase_failure=failure;
  assert(esp32_mquickjs_wifi_restart_stopped_interfaces(false,&result)==-(100+(int)failure));
  assert(result.admitted&&s_wifi_lifecycle.identity==41&&s_wifi_configuration_cleanup);
  assert(!phase_calls[10]&&!phase_calls[11]&&!temporary);
  if(failure==1) {
   unsigned previous[13];memcpy(previous,phase_calls,sizeof(previous));
   assert(esp32_mquickjs_wifi_restart_stopped_interfaces(false,&result)==ESP_ERR_INVALID_STATE);
   assert(!memcmp(previous,phase_calls,sizeof(previous)));
  }
  phase_failure=0;assert(!wifi_finish_configuration_cleanup());
  assert(!s_wifi_lifecycle.identity&&!s_wifi_state.runtime_cleanup_pending&&!running&&!sta_storage);
 }
}
''')

    def test_wapi_policy_changes_only_after_checkpoint_and_retiring_old_helpers(self):
        setup = MAIN[:MAIN.index('int main(void)')]
        compile_run(self, runtime_code(1, wapi=True) + setup + r'''
int main(void){
 esp32_mquickjs_wifi_configuration_execution_t result;bool enabled=false;
 setup_restart();stopped_source=true;running=false;s_wifi_application.acquired=false;s_wifi_state.radio_lease.acquired=false;
 assert(!wifi_restart_interfaces_inner(WIFI_MODE_NULL,true,&result,&enabled,false));
 assert(phase_calls[10]==2&&!selected_wapi&&reconstructed&&replayed&&running); /* Source Station prepare plus policy select. */
 setup_restart();stopped_source=true;running=false;s_wifi_application.acquired=false;s_wifi_state.radio_lease.acquired=false;
 phase_failure=1;selected_wapi=true;
 assert(wifi_restart_interfaces_inner(WIFI_MODE_NULL,true,&result,&enabled,false)==-101);
 assert(phase_calls[10]==1&&selected_wapi&&!reconstructed); /* Source prepare ran; policy select did not. */
}
''')

    def test_real_executor_modes_failure_handoff_and_cleanup_suffixes(self):
        for ap in (0, 1):
            with self.subTest(ap=ap):
                compile_run(self, runtime_code(ap) + MAIN)

    def test_stopped_executor_ap_admission_allocation_and_cleanup_handoff(self):
        setup = MAIN[:MAIN.index('int main(void)')]
        for ap in (0, 1):
            with self.subTest(ap=ap):
                compile_run(self, runtime_code(ap) + setup + STOPPED_MAIN)

    def test_missing_history_prepares_source_ap_before_checkpoint_and_retains_failed_helper(self):
        setup = MAIN[:MAIN.index('int main(void)')]
        setup += STOPPED_MAIN[:STOPPED_MAIN.index('int main(void)')]
        for ap in (0, 1):
            with self.subTest(ap=ap):
                compile_run(self, runtime_code(ap) + setup + SOURCE_MAIN)


BOUNDARIES = r'''
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
'''


MAIN = r'''
static void setup_restart(void) {
    assert(!temporary);
    memset(&s_wifi_state,0,sizeof(s_wifi_state));memset(&s_wifi_lifecycle,0,sizeof(s_wifi_lifecycle));
    memset(&s_ap_lifecycle,0,sizeof(s_ap_lifecycle));memset(&s_ap_coordinator,0,sizeof(s_ap_coordinator));
    memset(&native_token,0,sizeof(native_token));memset(phase_calls,0,sizeof(phase_calls));
    s_wifi_application=(esp32_mquickjs_wifi_radio_lease_t){9,1,true};
    s_wifi_state.radio_lease=(esp32_mquickjs_wifi_radio_lease_t){9,2,true};
    s_ap_lease=(esp32_mquickjs_wifi_radio_lease_t){0};
    memset(&s_ap_raw_owner,0,sizeof(s_ap_raw_owner));
    s_ap_netif=NULL;s_ap_detach_error=0;s_ap_stage=NULL;
    s_ap_cleanup_pending=s_wifi_configuration_cleanup=s_wifi_ap_stop_cleanup=false;
    s_wifi_configuration_disconnect_pending=s_wifi_configuration_stop_only=false;
    foreign_owner=alloc_fail=checkpoint=reconstructed=replayed=expect_stop_only=stopped_source=history_missing=off_source=off_needs_ap=false;
    begin_calls=owner_releases=stop_calls=ap_calls=sta_calls=shutdown_calls=0;
    phase_failure=fail_at=temporary_frees=off_cleanup_failure=0;running=sta_storage=true;
    stopped_mode=WIFI_MODE_STA;retry_allowed=false;retry_admissions=0;
}
int main(void) {
    esp32_mquickjs_wifi_configuration_execution_t result;
    setup_restart();s_wifi_state.status.connected=true;
    assert(esp32_mquickjs_wifi_restart_interfaces(WIFI_MODE_STA,&result)==ESP_ERR_INVALID_STATE && !result.admitted && !begin_calls);
    s_wifi_state.status.connected=false;s_wifi_state.scan_future_registered=true;
    assert(esp32_mquickjs_wifi_restart_interfaces(WIFI_MODE_STA,&result)==ESP_ERR_INVALID_STATE && !begin_calls);
    s_wifi_state.scan_future_registered=false;foreign_owner=true;
    assert(esp32_mquickjs_wifi_restart_interfaces(WIFI_MODE_STA,&result)==ESP_ERR_INVALID_STATE && !owner_releases && !result.admitted);
    setup_restart();
    assert(esp32_mquickjs_wifi_restart_interfaces(WIFI_MODE_NULL,&result)==ESP_ERR_INVALID_ARG && !begin_calls);
#if !CONFIG_ESP_WIFI_SOFTAP_SUPPORT
    assert(esp32_mquickjs_wifi_restart_interfaces(WIFI_MODE_AP,&result)==ESP_ERR_NOT_SUPPORTED && !begin_calls);
#else
    alloc_fail=true;
    assert(esp32_mquickjs_wifi_restart_interfaces(WIFI_MODE_AP,&result)==ESP_ERR_NO_MEM && !begin_calls && !result.admitted);
#endif
    for(int mode=1;mode<=(CONFIG_ESP_WIFI_SOFTAP_SUPPORT ? 3 : 1);++mode) {
        setup_restart();
        assert(esp32_mquickjs_wifi_restart_interfaces(mode,&result)==ESP_OK && result.admitted && result.resume_attempted);
        assert(!strcmp(result.stage,"complete") && !s_wifi_configuration_cleanup && !s_wifi_lifecycle.identity);
        assert(!s_wifi_state.runtime_cleanup_pending && !s_wifi_state.cleanup_stage && !temporary);
        assert(phase_calls[1]==1 && phase_calls[2]==1 && phase_calls[3]==1 && phase_calls[5]==1 && phase_calls[9]==1);
        assert(phase_calls[6]==(mode==WIFI_MODE_AP) && temporary_frees==((mode&WIFI_MODE_AP)!=0));
        for(int failure=1;failure<=9;++failure) {
            if(!(mode&WIFI_MODE_AP) && (failure==4 || (failure>=6 && failure<=8)))continue;
            if(mode!=WIFI_MODE_AP && failure==6)continue;
            setup_restart();phase_failure=failure;
            int error=esp32_mquickjs_wifi_restart_interfaces(mode,&result);
            assert(error==(failure==8 ? ESP_ERR_INVALID_STATE : -(100+failure)));
            assert(result.admitted && s_wifi_configuration_cleanup && s_wifi_lifecycle.identity==41 && !temporary);
            assert(s_wifi_state.runtime_cleanup_pending && s_wifi_state.cleanup_error==error);
            assert(!s_wifi_application.acquired && !s_wifi_state.radio_lease.acquired && !s_ap_lease.acquired);
            unsigned previous[13];memcpy(previous,phase_calls,sizeof(previous));
            assert(esp32_mquickjs_wifi_restart_interfaces(mode,&result)==ESP_ERR_INVALID_STATE);
            assert(!result.admitted && !memcmp(previous,phase_calls,sizeof(previous)));
            phase_failure=0;fail_at=4; /* Central shutdown failure retains suffix/token. */
            assert(wifi_finish_configuration_cleanup()==-4 && s_wifi_lifecycle.identity==41 && checkpoint);
            unsigned retired_ap=ap_calls,retired_sta=sta_calls,stopped=stop_calls;
            fail_at=0;assert(wifi_finish_configuration_cleanup()==ESP_OK);
            assert(!s_wifi_configuration_cleanup && !s_wifi_state.runtime_cleanup_pending && !s_wifi_lifecycle.identity);
            assert(ap_calls==retired_ap && sta_calls==retired_sta && stop_calls==stopped);
            assert(!s_ap_netif && !sta_storage && !running && shutdown_calls==2);
        }
    }
    for(int mode=1;mode<=(CONFIG_ESP_WIFI_SOFTAP_SUPPORT ? 3 : 1);++mode) {
        setup_restart();stopped_source=true;running=sta_storage=false;
        memset(&s_wifi_application,0,sizeof(s_wifi_application));memset(&s_wifi_state.radio_lease,0,sizeof(s_wifi_state.radio_lease));
        assert(esp32_mquickjs_wifi_restart_interfaces(mode,&result)==ESP_OK);
        assert(phase_calls[10]==1 && phase_calls[1]==1 && phase_calls[2]==1 && phase_calls[3]==1);
        assert(!s_wifi_configuration_cleanup && !s_wifi_lifecycle.identity && !temporary);
        setup_restart();stopped_source=true;running=sta_storage=false;phase_failure=10;
        assert(esp32_mquickjs_wifi_restart_interfaces(mode,&result)==-110 && result.admitted);
        assert(!checkpoint && !reconstructed && !phase_calls[1] && sta_storage && !temporary);
        phase_failure=0;assert(wifi_finish_configuration_cleanup()==ESP_OK && !sta_storage && !s_wifi_lifecycle.identity);
    }
    return 0;
}
'''


STOPPED_MAIN = r'''
static void stopped_setup(wifi_mode_t mode) {
    setup_restart();stopped_source=true;stopped_mode=mode;running=sta_storage=false;
    memset(&s_wifi_application,0,sizeof(s_wifi_application));
    memset(&s_wifi_state.radio_lease,0,sizeof(s_wifi_state.radio_lease));
}
int main(void) {
    esp32_mquickjs_wifi_configuration_execution_t result;
    setup_restart();
    assert(esp32_mquickjs_wifi_restart_stopped_interfaces(false,&result)==ESP_ERR_INVALID_STATE);
    assert(!result.admitted && !s_wifi_lifecycle.identity && !owner_releases && !temporary);
    stopped_setup(WIFI_MODE_STA);foreign_owner=true;
    assert(esp32_mquickjs_wifi_restart_stopped_interfaces(false,&result)==ESP_ERR_INVALID_STATE && !result.admitted);
    assert(!s_ap_cleanup_pending && !s_ap_coordinator.identity && !temporary);
    stopped_setup(WIFI_MODE_STA);s_ap_cleanup_pending=true;
    assert(esp32_mquickjs_wifi_restart_stopped_interfaces(false,&result)==ESP_ERR_INVALID_STATE && !begin_calls);
    assert(s_ap_cleanup_pending && !s_wifi_configuration_cleanup && !temporary);
    stopped_setup(WIFI_MODE_STA);s_ap_detach_error=77;
    assert(esp32_mquickjs_wifi_restart_stopped_interfaces(false,&result)==ESP_ERR_INVALID_STATE && !begin_calls);
    assert(s_ap_detach_error==77 && !temporary);
    stopped_setup(WIFI_MODE_STA);s_ap_coordinator.identity=13;
    assert(esp32_mquickjs_wifi_restart_stopped_interfaces(false,&result)==ESP_ERR_INVALID_STATE && !begin_calls);
    assert(s_ap_coordinator.identity==13 && !temporary);
    stopped_setup(WIFI_MODE_STA);s_ap_lifecycle.identity=17;
    assert(esp32_mquickjs_wifi_restart_stopped_interfaces(false,&result)==ESP_ERR_INVALID_STATE && !begin_calls);
    assert(s_ap_lifecycle.identity==17 && !temporary);
    stopped_setup(WIFI_MODE_STA);s_ap_lease=(esp32_mquickjs_wifi_radio_lease_t){9,3,true};
    assert(esp32_mquickjs_wifi_restart_stopped_interfaces(false,&result)==ESP_ERR_INVALID_STATE && !begin_calls);
    assert(s_ap_lease.acquired && !owner_releases && !temporary);
    stopped_setup(WIFI_MODE_STA);s_wifi_state.scan_future_registered=true;
    assert(esp32_mquickjs_wifi_restart_stopped_interfaces(false,&result)==ESP_ERR_INVALID_STATE && !begin_calls);
    assert(!temporary && !s_ap_cleanup_pending);
#if CONFIG_ESP_WIFI_SOFTAP_SUPPORT
    stopped_setup(WIFI_MODE_STA);alloc_fail=true;
    assert(esp32_mquickjs_wifi_restart_stopped_interfaces(false,&result)==ESP_ERR_NO_MEM && !begin_calls);
    assert(!result.admitted && !s_ap_cleanup_pending && !temporary);
#endif
    for(int mode=1;mode<=(CONFIG_ESP_WIFI_SOFTAP_SUPPORT ? 3 : 1);++mode) {
        stopped_setup(mode);
        assert(esp32_mquickjs_wifi_restart_stopped_interfaces(false,&result)==ESP_OK);
        assert(result.admitted && result.resume_attempted && !strcmp(result.stage,"complete"));
        assert(target_mode==mode && phase_calls[10]==1 && phase_calls[1]==1 && phase_calls[9]==1);
        assert(!s_wifi_configuration_cleanup && !s_ap_cleanup_pending && !s_ap_coordinator.identity);
        assert(!s_wifi_lifecycle.identity && !temporary && temporary_frees==CONFIG_ESP_WIFI_SOFTAP_SUPPORT);
        stopped_setup(mode);phase_failure=10;
        assert(esp32_mquickjs_wifi_restart_stopped_interfaces(false,&result)==-110 && result.admitted);
        assert(!checkpoint && !reconstructed && !phase_calls[1] && sta_storage && !temporary);
        assert(s_wifi_configuration_cleanup && s_wifi_lifecycle.identity==41);
        unsigned admitted_calls=begin_calls;
        assert(esp32_mquickjs_wifi_restart_stopped_interfaces(false,&result)==ESP_ERR_INVALID_STATE && !result.admitted);
        assert(begin_calls==admitted_calls);
        phase_failure=0;assert(wifi_finish_configuration_cleanup()==ESP_OK);
        assert(!sta_storage && !s_wifi_lifecycle.identity && !s_ap_coordinator.identity);
    }
    return 0;
}
'''


SOURCE_MAIN = r'''
int main(void) {
    esp32_mquickjs_wifi_configuration_execution_t result;
    for(int mode=1;mode<=(CONFIG_ESP_WIFI_SOFTAP_SUPPORT ? 3 : 1);++mode) {
        stopped_setup(mode);history_missing=true;
        assert(esp32_mquickjs_wifi_restart_stopped_interfaces(false,&result)==ESP_OK);
        assert(result.admitted && result.resume_attempted && phase_calls[1]==1);
        assert(phase_calls[11]==((mode&WIFI_MODE_AP)!=0) && phase_calls[10]==1);
        assert(!temporary && !s_wifi_lifecycle.identity && !s_wifi_configuration_cleanup);
        if(!(mode&WIFI_MODE_AP))continue;
        stopped_setup(mode);history_missing=true;phase_failure=11;
        assert(esp32_mquickjs_wifi_restart_stopped_interfaces(false,&result)==-111);
        assert(result.admitted && !result.stop_attempted && !strcmp(result.stage,"restart-stopped-ap-prepare"));
        assert(!checkpoint && !reconstructed && !phase_calls[1] && !temporary);
        assert(sta_storage && s_ap_netif && s_ap_cleanup_pending && s_wifi_lifecycle.identity==41);
        phase_failure=0;assert(wifi_finish_configuration_cleanup()==ESP_OK);
        assert(!sta_storage && !s_ap_netif && !s_wifi_lifecycle.identity);
    }
    return 0;
}
'''

OFF_MAIN = r'''
int main(void) {
    esp32_mquickjs_wifi_configuration_execution_t result;
    for(unsigned ap_policy=0;ap_policy<(CONFIG_ESP_WIFI_SOFTAP_SUPPORT?2U:1U);++ap_policy) {
        stopped_setup(WIFI_MODE_NULL);off_source=history_missing=true;off_needs_ap=ap_policy;
        assert(!esp32_mquickjs_wifi_restart_stopped_interfaces(ap_policy,&result));
        assert(result.admitted && result.configuration_attempted && result.resume_attempted);
        assert(!running && !sta_storage && !s_ap_netif && !s_wifi_application.acquired);
        assert(!s_wifi_state.radio_lease.acquired && !s_ap_lease.acquired && !temporary);
        assert(!s_wifi_lifecycle.identity && !native_token.identity && !s_wifi_configuration_cleanup);
        assert(phase_calls[12]==1 && !phase_calls[9] && phase_calls[10]==1);
        assert(phase_calls[11]==ap_policy && target_mode==(ap_policy?WIFI_MODE_APSTA:WIFI_MODE_STA));
        stopped_setup(WIFI_MODE_NULL);off_source=history_missing=true;off_needs_ap=ap_policy;phase_failure=12;
        assert(esp32_mquickjs_wifi_restart_stopped_interfaces(ap_policy,&result)==-112);
        assert(s_wifi_lifecycle.identity==41 && checkpoint && !temporary && result.admitted);
        assert(s_wifi_configuration_cleanup && s_wifi_state.runtime_cleanup_pending);
        phase_failure=0;
        assert(wifi_finish_configuration_cleanup()==ESP_OK && !running && !s_wifi_lifecycle.identity);
        assert(!sta_storage && !s_ap_netif && !s_wifi_configuration_cleanup);
        for(unsigned failure=2;failure<=4;++failure) {
            if(failure==2 && !ap_policy)continue;
            stopped_setup(WIFI_MODE_NULL);off_source=history_missing=true;off_needs_ap=ap_policy;
            off_cleanup_failure=failure;
            assert(esp32_mquickjs_wifi_restart_stopped_interfaces(ap_policy,&result)==-(int)failure);
            assert(s_wifi_lifecycle.identity==41 && !running && s_wifi_configuration_cleanup);
            assert(!temporary && phase_calls[12]==1 && !phase_calls[9]);
            /* Ordinary central cleanup consumes only remaining helper/shutdown suffix. */
            fail_at=0;expect_stop_only=false;
            assert(!wifi_finish_configuration_cleanup() && !s_wifi_lifecycle.identity && !sta_storage && !s_ap_netif);
            assert(phase_calls[12]==1);
        }
    }
    return 0;
}
'''

RETRY_MAIN = r'''
int main(void) {
    esp32_mquickjs_wifi_configuration_execution_t result;
    for(unsigned off=0;off<2;++off) {
        for(unsigned ap=0;ap<(CONFIG_ESP_WIFI_SOFTAP_SUPPORT?2U:1U);++ap) {
            wifi_mode_t work=ap?WIFI_MODE_APSTA:WIFI_MODE_STA;
            stopped_setup(off?WIFI_MODE_NULL:work);history_missing=true;
            off_source=off;off_needs_ap=off&&ap;phase_failure=off?12:9;
            assert(esp32_mquickjs_wifi_restart_stopped_interfaces(ap,&result)==-(100+phase_failure));
            assert(s_wifi_lifecycle.identity==41 && checkpoint && !temporary && running);
            retry_allowed=true;phase_failure=0;
            unsigned captured=phase_calls[1],begun=begin_calls,rebuilt=phase_calls[2];
            assert(!esp32_mquickjs_wifi_restart_stopped_interfaces(ap,&result));
            assert(result.admitted && result.stop_attempted && result.configuration_attempted && result.resume_attempted);
            assert(retry_admissions==1 && begin_calls==begun && phase_calls[1]==captured && phase_calls[2]==rebuilt+1);
            assert(!s_wifi_configuration_cleanup && !s_wifi_lifecycle.identity && !temporary);
            assert(running==!off && sta_storage==!off && (s_ap_netif!=NULL)==(!off&&ap));
            /* A STOP failure cannot jump to helper retirement or another init. */
            stopped_setup(off?WIFI_MODE_NULL:work);history_missing=true;
            off_source=off;off_needs_ap=off&&ap;phase_failure=off?12:9;
            assert(esp32_mquickjs_wifi_restart_stopped_interfaces(ap,&result)!=ESP_OK);
            retry_allowed=true;phase_failure=0;fail_at=1;
            captured=phase_calls[1];rebuilt=phase_calls[2];unsigned retired=ap_calls+sta_calls;
            assert(esp32_mquickjs_wifi_restart_stopped_interfaces(ap,&result)==-1);
            assert(result.admitted && s_wifi_lifecycle.identity==41 && checkpoint && !temporary);
            assert(phase_calls[1]==captured && phase_calls[2]==rebuilt && ap_calls+sta_calls==retired);
            fail_at=0;
            assert(!esp32_mquickjs_wifi_restart_stopped_interfaces(ap,&result));
            assert(phase_calls[1]==captured && phase_calls[2]==rebuilt+1 && !s_wifi_lifecycle.identity);
        }
    }
    stopped_setup(WIFI_MODE_NULL);history_missing=true;phase_failure=9;
    assert(esp32_mquickjs_wifi_restart_stopped_interfaces(false,&result)==-109);
    assert(phase_calls[1]==1 && !phase_calls[10]);
    retry_allowed=true;phase_failure=0;
    assert(!esp32_mquickjs_wifi_restart_stopped_interfaces(false,&result));
    assert(phase_calls[1]==1 && phase_calls[2]==2 && !phase_calls[10] && running && !s_wifi_lifecycle.identity);
    /* Rejection by the real Radio admission (separately exercised) does not
     * consume central cleanup. Allocation failure also preserves its source. */
    stopped_setup(WIFI_MODE_STA);history_missing=true;phase_failure=9;
    assert(esp32_mquickjs_wifi_restart_stopped_interfaces(false,&result)==-109);
    retry_allowed=false;unsigned stopped=stop_calls;
    assert(esp32_mquickjs_wifi_restart_stopped_interfaces(false,&result)==ESP_ERR_INVALID_STATE);
    assert(!result.admitted && s_wifi_lifecycle.identity==41 && checkpoint && stop_calls==stopped);
    phase_failure=0;assert(!wifi_finish_configuration_cleanup());
#if CONFIG_ESP_WIFI_SOFTAP_SUPPORT
    stopped_setup(WIFI_MODE_APSTA);history_missing=true;phase_failure=9;
    assert(esp32_mquickjs_wifi_restart_stopped_interfaces(false,&result)==-109);
    retry_allowed=alloc_fail=true;phase_failure=0;stopped=stop_calls;
    assert(esp32_mquickjs_wifi_restart_stopped_interfaces(false,&result)==ESP_ERR_NO_MEM);
    assert(result.admitted && s_wifi_lifecycle.identity==41 && checkpoint && stop_calls==stopped && !temporary);
    alloc_fail=false;assert(!esp32_mquickjs_wifi_restart_stopped_interfaces(false,&result));
#endif
    return 0;
}
'''
