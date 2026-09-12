"""Deferred native EAP install transaction; no execution during API wave.

Real profile allocation/ref/wipe and installer bodies. Only SDK driver/setter,
eloop, allocator and task observation boundaries are injected. Separate SDK
control/lifecycle fixtures exercise the production implementations of those
SDK boundaries; this fixture does not claim live supplicant/RF verification.
"""
from pathlib import Path
import unittest
from test_wifi_enterprise_profile import PRELUDE as PROFILE_PRELUDE
from test_wifi_rx_target import unit, INTERNAL
from test_wireless_control_regression import compile_run
from wireless_vm_fixture import ROOT, CORE, extract


class WiFiEAPInstall(unittest.TestCase):
    def test_production_install_retirement_policy_and_exact_owner(self):
        sdk = Path('/home/zach/esp/esp-idf/components/wpa_supplicant/esp_supplicant/include/esp_eap_client.h')
        for tls in (0, 1):
            code = PROFILE_PRELUDE + PRELUDE + f'\n#define CONFIG_ESP_WIFI_MBEDTLS_TLS_CLIENT {tls}\n'
            code += unit(sdk)
            for name in ('esp32_mquickjs_wifi_enterprise_profile.h', 'esp32_mquickjs_wifi_eap_sdk.h', 'esp32_mquickjs_wifi_eap_install.h'):
                code += unit(INTERNAL / name)
            code += extract((CORE / 'esp32_mquickjs_wireless_core.c').read_text(), 'esp32_mquickjs_wireless_secure_zero')
            folder = ROOT / 'components/esp32_mquickjs/src/modules/wifi_enterprise'
            code += unit(folder / 'esp32_mquickjs_wifi_enterprise_profile.c')
            code += BOUNDARIES
            code += unit(folder / 'esp32_mquickjs_wifi_eap_install.c')
            compile_run(self, code + MAIN)


PRELUDE = r'''
#define CONFIG_ESP_WIFI_SUITE_B_192 1
#define ESP_ERR_INVALID_STATE 5
#define ESP_FAIL -1
static bool on_wifi,on_worker,dispatch_fail,foreign,clear_fail,dirty,okc,time_disabled;
static bool activation_attempted;
static unsigned steps,fail_at,clears,commits,dispatches;
static uint32_t resources;
static int observed_cleanup_error,observed_control_error;
static const unsigned char *borrowed_ca,*borrowed_cert,*borrowed_key;
static int ca_length,cert_length,key_length;
static unsigned seen_methods,seen_phase2,seen_fast,seen_pac;
static bool seen_bundle,seen_suiteb;
static unsigned setter(void) {
    assert(on_wifi && !critical);steps++;
    return fail_at && steps==fail_at ? 71 : 0;
}
'''

BOUNDARIES = r'''
bool current_task_is_wifi_task(void){return on_wifi;}
bool esp32qjs_eap_on_worker(void){return on_worker;}
bool esp32qjs_eap_configuration_idle(void){assert(on_wifi);return !foreign && !resources;}
void esp32qjs_eap_configuration_changed(void){assert(on_wifi);dirty=true;commits++;}
static int eloop_register_timeout_blocking(int (*fn)(void *,void *),void *a,void *b){
    assert(!on_wifi && !on_worker);dispatches++;if(dispatch_fail)return 72;
    on_wifi=true;int result=fn(a,b);on_wifi=false;return result;
}
esp_err_t esp32_mquickjs_wifi_eap_sdk_snapshot(esp32_mquickjs_wifi_eap_sdk_snapshot_t *out){
    assert(on_wifi);*out=(esp32_mquickjs_wifi_eap_sdk_snapshot_t){.entered=true,.resources=resources,
        .cleanup_error=observed_cleanup_error,.control_error=observed_control_error};return ESP_OK;
}
esp_err_t esp_wifi_sta_enterprise_disable(void){
    assert(on_wifi);clears++;if(clear_fail && activation_attempted)return 73;
    resources=0;borrowed_ca=borrowed_cert=borrowed_key=NULL;activation_attempted=false;return 0;
}
esp_err_t esp_wifi_sta_enterprise_enable(void){
    assert(on_wifi && dirty);activation_attempted=true;
    unsigned result=setter();resources|=ESP32_MQUICKJS_WIFI_EAP_SDK_DRIVER_ENABLED;
    if(!result){resources|=ESP32_MQUICKJS_WIFI_EAP_SDK_ENABLED;okc=true;}
    return result;
}
void esp_wifi_set_okc_support(bool value){assert(on_wifi);okc=value;}
esp_err_t esp_eap_client_set_disable_time_check(bool value){unsigned r=setter();if(!r)time_disabled=value;return r;}
esp_err_t esp_eap_client_set_eap_methods(esp_eap_method_t value){seen_methods=value;return setter();}
esp_err_t esp_eap_client_set_ttls_phase2_method(esp_eap_ttls_phase2_types value){seen_phase2=value;return setter();}
esp_err_t esp_eap_client_set_suiteb_192bit_certification(bool value){seen_suiteb=value;return setter();}
esp_err_t esp_eap_client_use_default_cert_bundle(bool value){seen_bundle=value;return setter();}
#define BYTES_SETTER(name) esp_err_t name(const unsigned char *p,int n){assert(p && n>0);unsigned r=setter();if(!r)resources|=ESP32_MQUICKJS_WIFI_EAP_SDK_GLOBAL_CREDENTIALS;return r;}
BYTES_SETTER(esp_eap_client_set_identity)
BYTES_SETTER(esp_eap_client_set_username)
BYTES_SETTER(esp_eap_client_set_password)
BYTES_SETTER(esp_eap_client_set_new_password)
esp_err_t esp_eap_client_set_ca_cert(const unsigned char *p,int n){
    unsigned r=setter();if(!r){borrowed_ca=p;ca_length=n;resources|=ESP32_MQUICKJS_WIFI_EAP_SDK_GLOBAL_CREDENTIALS;}return r;
}
esp_err_t esp_eap_client_set_certificate_and_key(const unsigned char *c,int cn,const unsigned char *k,int kn,const unsigned char *p,int pn){
    assert(p && pn==4 && !memcmp(p,"pass",4));unsigned r=setter();
    if(!r){borrowed_cert=c;cert_length=cn;borrowed_key=k;key_length=kn;resources|=ESP32_MQUICKJS_WIFI_EAP_SDK_GLOBAL_CREDENTIALS;}return r;
}
esp_err_t esp_eap_client_set_domain_name(const char *p){assert(p && !strcmp(p,"example.test"));return setter();}
esp_err_t esp_eap_client_set_fast_params(esp_eap_fast_config p){seen_fast=p.fast_provisioning;return setter();}
esp_err_t esp_eap_client_set_pac_file(const unsigned char *p,int n){assert(p && n==0);seen_pac++;return setter();}
'''

MAIN = r'''
static void reset_observations(void){steps=fail_at=0;activation_attempted=false;clear_fail=dirty=false;}
static esp32_mquickjs_wifi_eap_profile_t *make_profile(void){
    static const uint8_t user[]={1,0,3},password[]={4,0,6};
    static const uint8_t ca[]="-----BEGIN CERTIFICATE-----\nCA\n-----END CERTIFICATE-----";
    static const uint8_t cert[]="-----BEGIN CERTIFICATE-----\nCLIENT\n-----END CERTIFICATE-----";
    static const uint8_t key[]={0x30,0x02,0x01,0x00};
    esp32_mquickjs_wifi_eap_input_t in={.policy={.methods=ESP_EAP_TYPE_TLS|ESP_EAP_TYPE_PEAP,
        .ttls_phase2=ESP_EAP_TTLS_PHASE2_CHAP,.suiteb=true}};
#define FIELD(i,p,n) in.fields[i]=(esp32_mquickjs_wifi_eap_span_t){p,n}
    FIELD(ESP32_MQUICKJS_WIFI_EAP_ANONYMOUS_IDENTITY,user,sizeof(user));
    FIELD(ESP32_MQUICKJS_WIFI_EAP_USERNAME,user,sizeof(user));
    FIELD(ESP32_MQUICKJS_WIFI_EAP_PASSWORD,password,sizeof(password));
    FIELD(ESP32_MQUICKJS_WIFI_EAP_NEW_PASSWORD,password,sizeof(password));
    FIELD(ESP32_MQUICKJS_WIFI_EAP_CA_CERT,ca,sizeof(ca)-1);
    FIELD(ESP32_MQUICKJS_WIFI_EAP_CLIENT_CERT,cert,sizeof(cert)); /* Existing PEM NUL. */
    FIELD(ESP32_MQUICKJS_WIFI_EAP_PRIVATE_KEY,key,sizeof(key)); /* DER exact bytes. */
    FIELD(ESP32_MQUICKJS_WIFI_EAP_PRIVATE_KEY_PASSWORD,(const uint8_t *)"pass",4);
#if CONFIG_ESP_WIFI_MBEDTLS_TLS_CLIENT
    FIELD(ESP32_MQUICKJS_WIFI_EAP_DOMAIN,(const uint8_t *)"example.test",12);
#endif
#undef FIELD
    esp32_mquickjs_wifi_eap_profile_t *p=NULL;
    assert(esp32_mquickjs_wifi_eap_profile_create(&in,&p)==0);return p;
}
int main(void){
    esp32_mquickjs_wifi_eap_profile_t *p=make_profile();
    esp32_mquickjs_wifi_eap_install_result_t out;
    dispatch_fail=true;assert(esp32_mquickjs_wifi_eap_install(p,&out)==72 && !out.entered && !out.identity && p->refs==1 && !steps);dispatch_fail=false;
    on_worker=true;assert(esp32_mquickjs_wifi_eap_install(p,&out)==ESP_ERR_INVALID_STATE && !out.entered);on_worker=false;
    foreign=true;assert(esp32_mquickjs_wifi_eap_install(p,&out)==ESP_ERR_INVALID_STATE && !clears && p->refs==1);foreign=false;
    p->refs=UINT32_MAX;assert(esp32_mquickjs_wifi_eap_install(p,&out)==ESP_ERR_NO_MEM && !clears);p->refs=1;
    assert(esp32_mquickjs_wifi_eap_install(p,&out)==0 && out.enabled && out.retained && out.identity && p->refs==2);
    unsigned total_steps=steps;uint64_t first=out.identity;
    assert(!time_disabled && !okc && dirty && seen_suiteb && seen_phase2==ESP_EAP_TTLS_PHASE2_CHAP);
    assert(ca_length==(int)p->input.fields[ESP32_MQUICKJS_WIFI_EAP_CA_CERT].length+1);
    assert(cert_length==(int)p->input.fields[ESP32_MQUICKJS_WIFI_EAP_CLIENT_CERT].length && key_length==4);
    assert(borrowed_ca==p->input.fields[ESP32_MQUICKJS_WIFI_EAP_CA_CERT].data);
    unsigned before=steps;
    assert(esp32_mquickjs_wifi_eap_install(p,&out)==ESP_ERR_INVALID_STATE && steps==before && p->refs==2);
    assert(esp32_mquickjs_wifi_eap_install_status(first+1,&out)==ESP_ERR_INVALID_STATE && !out.retained);
    assert(esp32_mquickjs_wifi_eap_install_status(first,&out)==0 && out.identity==first && out.enabled);
    /* Restart admission observes the actual enabled binding and exact profile,
     * without setters, reference transfer or native identity consumption. */
    unsigned clear_before=clears;uint64_t next_before=s_eap_install_next_identity;
    esp32_mquickjs_wifi_eap_profile_t *different=make_profile();
    assert(esp32_mquickjs_wifi_eap_install_restart_source(first,different,&out)==ESP_ERR_INVALID_STATE);
    esp32_mquickjs_wifi_eap_profile_release(different);
    assert(esp32_mquickjs_wifi_eap_install_restart_source(first+1,p,&out)==ESP_ERR_INVALID_STATE);
    observed_control_error=91;
    assert(esp32_mquickjs_wifi_eap_install_restart_source(first,p,&out)==ESP_ERR_INVALID_STATE);
    observed_control_error=0;observed_cleanup_error=92;
    assert(esp32_mquickjs_wifi_eap_install_restart_source(first,p,&out)==ESP_ERR_INVALID_STATE);
    observed_cleanup_error=0;resources &= ~ESP32_MQUICKJS_WIFI_EAP_SDK_ENABLED;
    assert(esp32_mquickjs_wifi_eap_install_restart_source(first,p,&out)==ESP_ERR_INVALID_STATE);
    resources |= ESP32_MQUICKJS_WIFI_EAP_SDK_ENABLED;
    s_eap_install_next_identity=UINT64_MAX;
    assert(esp32_mquickjs_wifi_eap_install_restart_source(first,p,&out)==ESP_ERR_INVALID_STATE);
    s_eap_install_next_identity=next_before;
    assert(esp32_mquickjs_wifi_eap_install_restart_source(first,p,&out)==ESP_OK && out.enabled && out.retained);
    assert(steps==before && clears==clear_before && p->refs==2 && s_eap_install_next_identity==next_before);
    assert(esp32_mquickjs_wifi_eap_install_clear(first,&out)==0 && !out.retained && !out.identity && p->refs==1 && !borrowed_ca);
    before=clears;assert(esp32_mquickjs_wifi_eap_install_clear(first,&out)==ESP_ERR_INVALID_STATE && clears==before);
    /* Every setter/enable failure runs production rollback; native reference
     * survives until the injected SDK says it has retired all borrowers. */
    for(unsigned n=1;n<=total_steps;n++){
        reset_observations();fail_at=n;
        assert(esp32_mquickjs_wifi_eap_install(p,&out)==71 && !out.retained && !out.identity && !resources && p->refs==1);
        assert(out.cleanup_error==0 && !borrowed_ca);
    }
    reset_observations();fail_at=total_steps;clear_fail=true;
    assert(esp32_mquickjs_wifi_eap_install(p,&out)==71 && out.retained && out.identity && out.cleanup_error==73 && p->refs==2);
    uint64_t failed=out.identity;assert(failed>first && borrowed_ca);
    before=clears;assert(esp32_mquickjs_wifi_eap_install_clear(first,&out)==ESP_ERR_INVALID_STATE && clears==before && p->refs==2);
    dispatch_fail=true;assert(esp32_mquickjs_wifi_eap_install_clear(failed,&out)==72 && p->refs==2);dispatch_fail=false;
    clear_fail=false;fail_at=0;assert(esp32_mquickjs_wifi_eap_install_clear(failed,&out)==0 && p->refs==1);
    /* Last caller can go away while native cleanup is still pending. */
    reset_observations();assert(esp32_mquickjs_wifi_eap_install(p,&out)==0);uint64_t last=out.identity;
    esp32_mquickjs_wifi_eap_profile_release(p);assert(s_eap_profile_counts.profiles==1);
    clear_fail=true;assert(esp32_mquickjs_wifi_eap_install_clear(last,&out)==73 && out.retained && borrowed_ca);
    clear_fail=false;assert(esp32_mquickjs_wifi_eap_install_clear(last,&out)==0 && !s_eap_profile_counts.profiles && !s_eap_profile_counts.reserved_bytes);
    p=make_profile();reset_observations();
    /* Trust rejection precedes all SDK mutations; retained bytes remain caller-owned. */
    esp32_mquickjs_wifi_eap_input_t untrusted=p->input;untrusted.fields[ESP32_MQUICKJS_WIFI_EAP_CA_CERT]=(esp32_mquickjs_wifi_eap_span_t){0};
    esp32_mquickjs_wifi_eap_profile_t *q=NULL;assert(esp32_mquickjs_wifi_eap_profile_create(&untrusted,&q)==0);
    before=clears;assert(esp32_mquickjs_wifi_eap_install(q,&out)==ESP_ERR_INVALID_ARG && !strcmp(out.stage,"server-trust") && clears==before && q->refs==1);
    esp32_mquickjs_wifi_eap_profile_release(q);
    on_wifi=true;before=dispatches;assert(esp32_mquickjs_wifi_eap_install(p,&out)==0 && dispatches==before);last=out.identity;
    assert(esp32_mquickjs_wifi_eap_install_clear(last,&out)==0 && dispatches==before);on_wifi=false;
#if !CONFIG_ESP_WIFI_MBEDTLS_TLS_CLIENT
    esp32_mquickjs_wifi_eap_input_t fast={.policy={.methods=ESP_EAP_TYPE_FAST,.fast_provisioning=1}};
    assert(esp32_mquickjs_wifi_eap_profile_create(&fast,&q)==0);
    assert(esp32_mquickjs_wifi_eap_install(q,&out)==0 && seen_fast==1 && seen_pac==1);last=out.identity;
    assert(esp32_mquickjs_wifi_eap_install_clear(last,&out)==0);esp32_mquickjs_wifi_eap_profile_release(q);
#endif
    s_eap_install_next_identity=UINT64_MAX;before=clears;
    assert(esp32_mquickjs_wifi_eap_install(p,&out)==ESP_ERR_INVALID_STATE && !strcmp(out.stage,"identity") && clears==before && p->refs==1);
    esp32_mquickjs_wifi_eap_profile_release(p);
    assert(!s_eap_profile_counts.profiles && !s_eap_profile_counts.reserved_bytes && !s_eap_installed_profile);
    return 0;
}
'''
