"""Production Radio composite transaction with SDK faults; Wi-Fi phase pending.

Uses recorded SDK declarations and the actual transaction, exact-token admission,
validators, semantic comparison and secure-zero implementation. SDK getter/setter
storage is injected; it does not establish actual SDK normalization/NVS behavior.
"""
import json
import re
import unittest
from wireless_vm_fixture import ROOT, extract
from test_wireless_control_regression import compile_run

RADIO = ROOT / 'components/esp32_mquickjs/src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c'
HEADER = ROOT / 'components/esp32_mquickjs/internal/esp32_mquickjs_wifi_radio.h'


def structure(source, name):
    return re.search(r'typedef struct \{[^}]*\} ' + name + r';', source).group(0) + '\n'


def sdk_types(variant, extra=()):
    symbols = json.loads((ROOT / 'docs/idf-wifi-api-inventory.json').read_text())['variants'][variant]['symbols']
    declarations = {k.split('::')[-1]: v['declaration'] for k, v in symbols.items()
                    if v.get('declaration', '').startswith('typedef ')}
    seen, output = set(), []

    def visit(name):
        if name in seen:
            return
        seen.add(name)
        declaration = declarations[name]
        for dependency in sorted(set(re.findall(r'\b\w+_t\b', declaration)) - {name}):
            if dependency in declarations:
                visit(dependency)
        output.append(declaration + ';')

    for name in ('wifi_country_t', 'wifi_protocols_t', 'wifi_bandwidths_t', 'wifi_band_mode_t',
                 'wifi_ps_type_t', 'wifi_mode_t', 'wifi_interface_t', 'wifi_storage_t', 'wifi_config_t') + tuple(extra):
        visit(name)
    return '\n'.join(output) + '\n'


PRELUDE = r'''
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG -1
#define ESP_ERR_INVALID_STATE -2
#define ESP_ERR_INVALID_RESPONSE -3
#define ESP_ERR_NOT_SUPPORTED -4
#define ESP_ERR_NO_MEM -5
#define ESP_ERR_NOT_ALLOWED -6
#define WIFI_PROTOCOL_11B 1
#define WIFI_PROTOCOL_11G 2
#define WIFI_PROTOCOL_11N 4
#define WIFI_PROTOCOL_LR 8
#define WIFI_PROTOCOL_11A 16
#define WIFI_PROTOCOL_11AC 32
#define WIFI_PROTOCOL_11AX 64
#define WIFI_RADIO_MAX_LEASES 2
#define ESP32_MQUICKJS_WIFI_RADIO_STOPPED 2
typedef int esp_err_t;
'''

BOUNDARIES = r'''
static struct {
    int lock;bool driver_owned,storage_configured,started,stop_required,promiscuous_claimed;
    const char *fault_stage,*cleanup_stage;int driver_state;
    wifi_storage_t storage;wifi_mode_t effective_mode;unsigned wake_locks;
    struct { unsigned identity; } operation,leases[WIFI_RADIO_MAX_LEASES];
    esp32_mquickjs_wifi_radio_lifecycle_t lifecycle;
    esp32_mquickjs_wifi_radio_config_result_t configuration;
} s_radio;
static wifi_mode_t native_mode;
static wifi_storage_t native_storage;
static wifi_country_t native_country;
static wifi_protocols_t native_protocols[2];
static wifi_bandwidths_t native_bandwidths[2];
static wifi_config_t native_config[2];
static wifi_ps_type_t native_ps;
static wifi_band_mode_t native_band;
static esp32_mquickjs_wifi_radio_config_result_t result;
static int depth,critical,calls,writes,fail_at,rollback_fail,alloc_live;
static size_t allocated_size;
static bool allocation_failure,corrupt_ps,corrupted,reenable_pmf;
static int pmf_error,pmf_calls;
static const char *rollback_failed_stage;
static void wifi_radio_operation_lock(void) { assert(!depth && !critical);depth=1; }
static void wifi_radio_operation_unlock(void) { assert(depth && !critical);depth=0; }
#define taskENTER_CRITICAL(p) do { (void)(p);assert(!critical);critical=1; } while(0)
#define taskEXIT_CRITICAL(p) do { (void)(p);assert(critical);critical=0; } while(0)
static int step(bool write) {
    assert(depth && !critical);calls++;writes+=write;
    if(calls==fail_at)return -77;
    if(result.rollback_attempted && rollback_fail && --rollback_fail==0) {
        rollback_failed_stage=result.rollback_stage;return -88;
    }
    return 0;
}
static void *allocate(size_t n,size_t size) {
    assert(!alloc_live && depth && !critical);if(allocation_failure)return NULL;
    allocated_size=n*size;void *p=calloc(n,size);assert(p);alloc_live++;return p;
}
static void release(void *p) {
    assert(alloc_live==1);for(size_t i=0;i<allocated_size;i++)assert(((uint8_t *)p)[i]==0);
    alloc_live--;free(p);
}
static int wifi_radio_record_fault(const char *stage,int error) { s_radio.fault_stage=stage;return error; }
static int wifi_radio_cleanup_fault(const char *stage,int error) { s_radio.cleanup_stage=stage;return error; }
static int esp_wifi_get_mode(wifi_mode_t *v) { int e=step(false);if(!e)*v=native_mode;return e; }
static int esp_wifi_set_mode(wifi_mode_t v) { int e=step(true);native_mode=v;return e; }
static int esp_wifi_set_storage(wifi_storage_t v) { int e=step(true);native_storage=v;return e; }
static int esp_wifi_get_config(wifi_interface_t i,wifi_config_t *v) { int e=step(false);if(!e)*v=native_config[i];return e; }
static int esp_wifi_set_config(wifi_interface_t i,wifi_config_t *v) {
    int e=step(true);native_config[i]=*v;
    if(reenable_pmf) {
        wifi_pmf_config_t *pmf=i==WIFI_IF_STA ? &native_config[i].sta.pmf_cfg : &native_config[i].ap.pmf_cfg;
        pmf->capable=true;
    }
    return e;
}
static int esp_wifi_disable_pmf_config(wifi_interface_t i) {
    assert(!s_radio.started);++pmf_calls;
    wifi_pmf_config_t *pmf=i==WIFI_IF_STA ? &native_config[i].sta.pmf_cfg : &native_config[i].ap.pmf_cfg;
    pmf->capable=pmf->required=false;int err=step(true);return err ? err : pmf_error;
}
static int esp_wifi_get_country(wifi_country_t *v) { int e=step(false);if(!e)*v=native_country;return e; }
static int esp_wifi_set_country(const wifi_country_t *v) { int e=step(true);int8_t power=native_country.max_tx_power;native_country=*v;native_country.max_tx_power=power;return e; }
static int esp_wifi_set_country_code(const char *code,bool automatic) {
    int e=step(true);native_country.cc[0]=code[0];native_country.cc[1]=code[1];native_country.cc[2]=' ';
    native_country.policy=automatic?WIFI_COUNTRY_POLICY_AUTO:WIFI_COUNTRY_POLICY_MANUAL;
    native_country.schan=1;native_country.nchan=13;return e;
}
static int esp_wifi_get_band_mode(wifi_band_mode_t *v) { int e=step(false);if(!e)*v=native_band;return e; }
static int esp_wifi_get_protocols(wifi_interface_t i,wifi_protocols_t *v) { int e=step(false);if(!e)*v=native_protocols[i];return e; }
static int esp_wifi_set_protocols(wifi_interface_t i,wifi_protocols_t *v) { int e=step(true);native_protocols[i]=*v;return e; }
static int esp_wifi_get_bandwidths(wifi_interface_t i,wifi_bandwidths_t *v) { int e=step(false);if(!e)*v=native_bandwidths[i];return e; }
static int esp_wifi_set_bandwidths(wifi_interface_t i,wifi_bandwidths_t *v) { int e=step(true);native_bandwidths[i]=*v;return e; }
static int esp_wifi_get_protocol(wifi_interface_t i,uint8_t *v) { int e=step(false);if(!e)*v=native_protocols[i].ghz_2g;return e; }
static int esp_wifi_set_protocol(wifi_interface_t i,uint8_t v) { int e=step(true);native_protocols[i].ghz_2g=v;return e; }
static int esp_wifi_get_bandwidth(wifi_interface_t i,wifi_bandwidth_t *v) { int e=step(false);if(!e)*v=native_bandwidths[i].ghz_2g;return e; }
static int esp_wifi_set_bandwidth(wifi_interface_t i,wifi_bandwidth_t v) { int e=step(true);native_bandwidths[i].ghz_2g=v;return e; }
static int esp_wifi_set_ps(wifi_ps_type_t v) { int e=step(true);native_ps=v;return e; }
static int esp_wifi_get_ps(wifi_ps_type_t *v) {
    int e=step(false);if(!e)*v=native_ps;
    if(!e && corrupt_ps && !corrupted && !strcmp(result.stage,"power-save-readback")) {
        *v=WIFI_PS_NONE;corrupted=true;
    }
    return e;
}
#define calloc allocate
#define free release
'''

MAIN = r'''
#undef calloc
#undef free
static wifi_config_t requested[2],before[2];
static esp32_mquickjs_wifi_radio_config_controls_t controls;
static bool accept_config(const wifi_config_t *a,const wifi_config_t *b) { return !memcmp(a,b,sizeof(*a)); }
static void reset(void) {
    assert(!depth && !critical && !alloc_live);
    memset(&s_radio,0,sizeof(s_radio));memset(&controls,0,sizeof(controls));memset(&result,0,sizeof(result));
    s_radio.driver_owned=s_radio.storage_configured=true;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STOPPED;
    s_radio.lifecycle=(esp32_mquickjs_wifi_radio_lifecycle_t){.identity=7,.generation=3};
    native_mode=s_radio.effective_mode=WIFI_MODE_STA;native_storage=s_radio.storage=WIFI_STORAGE_RAM;
    native_country=(wifi_country_t){.cc="US",.schan=1,.nchan=11,.max_tx_power=20,.policy=WIFI_COUNTRY_POLICY_MANUAL};
    native_ps=WIFI_PS_NONE;native_band=WIFI_BAND_MODE_2G_ONLY;
    memset(native_config,0,sizeof(native_config));native_config[0].sta.ssid[0]='s';native_config[0].sta.password[0]='x';
    native_config[0].sta.pmf_cfg.capable=true;
    native_config[1].ap.ssid[0]='a';native_config[1].ap.ssid_len=1;native_config[1].ap.channel=1;
    memcpy(before,native_config,sizeof(before));memcpy(requested,before,sizeof(requested));
    requested[0].sta.ssid[0]='n';requested[1].ap.ssid[0]='b';
    for(int i=0;i<2;i++) {
        native_protocols[i]=(wifi_protocols_t){.ghz_2g=WIFI_PROTOCOL_11B|WIFI_PROTOCOL_11G|WIFI_PROTOCOL_11N};
        native_bandwidths[i]=(wifi_bandwidths_t){.ghz_2g=WIFI_BW20};
        controls.protocol_bands[i]=controls.bandwidth_bands[i]=1;
        controls.protocols[i]=native_protocols[i];controls.bandwidths[i].ghz_2g=WIFI_BW40;
    }
    controls.power_save_set=true;controls.power_save=WIFI_PS_MIN_MODEM;
    calls=writes=fail_at=rollback_fail=0;allocation_failure=corrupt_ps=corrupted=reenable_pmf=false;rollback_failed_stage=NULL;
    pmf_error=pmf_calls=0;
}
static int configure(void) {
    return esp32_mquickjs_wifi_radio_configure_lifecycle(&s_radio.lifecycle,WIFI_MODE_APSTA,WIFI_STORAGE_RAM,
        &requested[0],accept_config,&requested[1],accept_config,&controls,&result);
}
static void restored(void) {
    assert(native_mode==WIFI_MODE_STA && native_storage==WIFI_STORAGE_RAM && native_ps==WIFI_PS_NONE);
    assert(!memcmp(native_config,before,sizeof(before)));
    for(int i=0;i<2;i++)assert(native_bandwidths[i].ghz_2g==WIFI_BW20);
    assert(!depth && !critical && !alloc_live && s_radio.lifecycle.identity==7);
}
int main(void) {
    reset();assert(configure()==0 && !strcmp(result.stage,"complete"));int total=calls;
    assert(native_mode==WIFI_MODE_APSTA && native_ps==WIFI_PS_MIN_MODEM && !alloc_live);
    /* Every native call on the success path can fail, including setters that
     * mutate before returning an error. Caller configs stay uncommitted. */
    for(int nth=1;nth<=total;nth++) {
        reset();fail_at=nth;wifi_config_t input[2];memcpy(input,requested,sizeof(input));
        assert(configure()==-77 && result.error==-77 && !memcmp(input,requested,sizeof(input)));
        assert(!depth && !critical && !alloc_live && s_radio.lifecycle.identity==7);
        if(result.mutation_attempted) { assert(result.rollback_complete);restored(); }
        else assert(writes==0 && !result.rollback_attempted);
    }
    /* Enumerate the recovery suffix separately; original failure survives. */
    reset();fail_at=total;assert(configure()==-77);int recovery_calls=calls-total;
    for(int nth=1;nth<=recovery_calls;nth++) {
        reset();fail_at=total;rollback_fail=nth;
        assert(configure()==-77 && result.rollback_error==-88 && !result.rollback_complete);
        assert(result.rollback_stage==rollback_failed_stage && s_radio.fault_stage && s_radio.cleanup_stage);
        assert(!alloc_live && !depth && s_radio.lifecycle.identity==7);
        fail_at=0;int count=calls;assert(configure()==ESP_ERR_INVALID_STATE && calls==count);
    }
    reset();allocation_failure=true;
    /* Regulatory read precedes allocation; no SDK mutation is permitted. */
    assert(configure()==ESP_ERR_NO_MEM && calls==1 && !writes && !alloc_live);
    assert(!strcmp(result.stage,"config-allocate"));restored();
    reset();s_radio.leases[1].identity=9;assert(configure()==ESP_ERR_INVALID_STATE && !calls);
    reset();esp32_mquickjs_wifi_radio_lifecycle_t stale=s_radio.lifecycle;stale.generation++;
    assert(esp32_mquickjs_wifi_radio_configure_lifecycle(&stale,WIFI_MODE_STA,WIFI_STORAGE_RAM,
        NULL,NULL,NULL,NULL,NULL,&result)==ESP_ERR_INVALID_STATE && !calls);
    reset();controls.protocols[0].ghz_2g=WIFI_PROTOCOL_11N;
    assert(configure()==ESP_ERR_INVALID_ARG && !calls);
    reset();controls.bandwidths[0].ghz_2g=(wifi_bandwidth_t)999;
    assert(configure()==ESP_ERR_INVALID_ARG && !calls);
    reset();controls.protocol_bands[0]=2;controls.protocols[0].ghz_5g=WIFI_PROTOCOL_11A;
#if CONFIG_SOC_WIFI_SUPPORT_5G
    assert(configure()==ESP_ERR_INVALID_STATE && !writes);
#else
    assert(configure()==ESP_ERR_NOT_SUPPORTED && !calls);
#endif
    reset();controls.protocols[0].ghz_2g|=WIFI_PROTOCOL_11AX;
#if CONFIG_SOC_WIFI_HE_SUPPORT
    assert(configure()==ESP_ERR_INVALID_ARG && !calls); /* explicit AX + HT40 */
    controls.bandwidth_bands[0]=0;native_bandwidths[0].ghz_2g=WIFI_BW40;
    assert(configure()==ESP_ERR_INVALID_ARG && !writes); /* inherited HT40 */
#else
    assert(configure()==ESP_ERR_NOT_SUPPORTED && !calls);
#endif
    reset();corrupt_ps=true;assert(configure()==ESP_ERR_INVALID_RESPONSE && result.rollback_complete);restored();
    /* Country persistence is recorded even with RAM storage, including a
     * failed setter. RAM restoration does not establish the old NVS image. */
    reset();controls.country_set=controls.country_by_code=true;
    controls.country=(wifi_country_t){.cc="TW",.policy=WIFI_COUNTRY_POLICY_MANUAL};
    assert(configure()==ESP_OK && result.persistent_mutation_possible);int country_total=calls;
    for(int nth=1;nth<=country_total;nth++) {
        reset();controls.country_set=controls.country_by_code=true;
        controls.country=(wifi_country_t){.cc="TW",.policy=WIFI_COUNTRY_POLICY_MANUAL};fail_at=nth;
        assert(configure()==-77 && !alloc_live && !depth);
        if(result.persistent_mutation_possible)assert(s_radio.fault_stage);
        if(result.rollback_complete) { restored();assert(native_country.cc[0]=='U' && native_country.nchan==11); }
    }
    reset();controls.country_set=true;controls.country=native_country;
    assert(configure()==ESP_ERR_INVALID_ARG && !writes); /* max power is read-only */
    controls.country.max_tx_power=0;controls.country.nchan=13;requested[1].ap.channel=13;
    assert(configure()==0 && native_country.nchan==13); /* validate AP using NEW country */
    reset();reenable_pmf=true;corrupt_ps=true;
    native_config[0].sta.threshold.authmode=WIFI_AUTH_WPA2_PSK;
    native_config[0].sta.disable_wpa3_compatible_mode=true;
    native_config[0].sta.pmf_cfg.capable=false;
    before[0]=native_config[0];requested[0]=before[0];requested[0].sta.ssid[0]='n';
    assert(configure()==ESP_ERR_INVALID_RESPONSE && result.rollback_complete);
    restored(); /* Dedicated disable restores the PMF state observed before the failed transaction. */
    for(int failure=0;failure<2;++failure) {
        reset();reenable_pmf=true;pmf_error=failure ? -79 : 0;
        native_config[1].ap.authmode=WIFI_AUTH_WPA2_PSK;native_config[1].ap.pmf_cfg.capable=true;
        before[1]=native_config[1];requested[1]=before[1];requested[1].ap.pmf_cfg.capable=false;
        requested[1].ap.ssid[0]='b';
        int err=esp32_mquickjs_wifi_radio_configure_lifecycle(&s_radio.lifecycle,WIFI_MODE_APSTA,WIFI_STORAGE_RAM,
            NULL,NULL,&requested[1],accept_config,NULL,&result);
        assert(err==(failure ? -79 : ESP_OK) && pmf_calls==1 && !s_radio.started);
        assert(!memcmp(&native_config[0],&before[0],sizeof(before[0])));
        if(failure) { assert(result.rollback_complete && !strcmp(result.stage,"ap-pmf-config"));restored(); }
        else assert(!native_config[1].ap.pmf_cfg.capable && !native_config[1].ap.pmf_cfg.required);
    }
    reset();requested[1].ap.authmode=WIFI_AUTH_WPA3_PSK;
    assert(configure()==ESP_ERR_INVALID_ARG && !calls && !alloc_live);
    reset();requested[1].ap.pmf_cfg.required=true;
    assert(configure()==ESP_ERR_INVALID_ARG && !calls && !alloc_live);
    for(int failure=0;failure<2;++failure) {
        reset();reenable_pmf=true;pmf_error=failure ? -79 : 0;
        native_config[0].sta.threshold.authmode=WIFI_AUTH_WPA2_PSK;
        native_config[0].sta.disable_wpa3_compatible_mode=true;
        before[0]=native_config[0];requested[0]=before[0];requested[0].sta.pmf_cfg.capable=false;
        requested[0].sta.ssid[0]='n';
        int err=esp32_mquickjs_wifi_radio_configure_lifecycle(&s_radio.lifecycle,WIFI_MODE_STA,WIFI_STORAGE_RAM,
            &requested[0],accept_config,NULL,NULL,NULL,&result);
        assert(err==(failure ? -79 : ESP_OK) && pmf_calls==1 && !s_radio.started);
        assert(!memcmp(&native_config[1],&before[1],sizeof(before[1])));
        if(failure) { assert(result.rollback_complete && !strcmp(result.stage,"station-pmf-config"));restored(); }
        else assert(!native_config[0].sta.pmf_cfg.capable && !native_config[0].sta.pmf_cfg.required);
    }
    reset();requested[0].sta.pmf_cfg.capable=false;
    assert(configure()==ESP_ERR_INVALID_ARG && !calls && !alloc_live); /* Compatible policy not disabled. */
    return 0;
}
'''


class WiFiConfigControls(unittest.TestCase):
    def test_production_transaction_boundaries(self):
        source, header = RADIO.read_text(), HEADER.read_text()
        local = '\n'.join(structure(header, name) for name in (
            'esp32_mquickjs_wifi_radio_lifecycle_t', 'esp32_mquickjs_wifi_radio_config_result_t',
            'esp32_mquickjs_wifi_radio_config_controls_t'))
        local += '\ntypedef bool (*esp32_mquickjs_wifi_config_accept_fn)(const wifi_config_t *,const wifi_config_t *);\n'
        helpers = source[source.index('/* These helpers run inside'):source.index('/* Caller has already validated')]
        # Public driver wrappers have their own admission fixtures; keep this
        # transaction fixture focused on the production composite helpers.
        for name in ('esp32_mquickjs_wifi_radio_read_phy', 'esp32_mquickjs_wifi_radio_write_phy'):
            helpers = helpers.replace(extract(source, name), '')
        production = extract(source, 'esp32_mquickjs_wifi_radio_5ghz_channel_bit')
        production += extract(source, 'wifi_radio_validate_regulatory_channel')
        production += extract(source, 'esp32_mquickjs_wifi_radio_pmf_disable_allowed')
        production += extract(source, 'wifi_radio_restore_disabled_pmf')
        production += extract(source, 'wifi_radio_config_equal') + helpers
        production += extract(source, 'wifi_radio_configure_locked')
        production += extract(source, 'esp32_mquickjs_wifi_radio_configure_lifecycle')
        zero = extract((ROOT / 'components/esp32_mquickjs/src/core/esp32_mquickjs_wireless_core.c').read_text(),
                       'esp32_mquickjs_wireless_secure_zero')
        for target, he, five in [('esp32c3', 0, 0), ('esp32c5', 1, 1)]:
            with self.subTest(target=target):
                gates = (f'\n#define CONFIG_SOC_WIFI_SUPPORT_5G {five}\n'
                         f'#define CONFIG_SOC_WIFI_HE_SUPPORT {he}\n#define CONFIG_ESP_WIFI_SOFTAP_SUPPORT 1\n')
                compile_run(self, PRELUDE + gates + sdk_types(target + '/representative') + local +
                            BOUNDARIES + zero + production + MAIN)
