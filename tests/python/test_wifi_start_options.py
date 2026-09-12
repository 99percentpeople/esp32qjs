"""Production start capture, atomic resolution and shared executor; phase run deferred."""
import re
import tempfile
import unittest
import test_wifi_configuration_selection as selection_fixture
import test_wifi_configuration_executor as executor_fixture
from test_wifi_config_controls import PRELUDE, sdk_types, structure
from test_wireless_control_regression import compile_run
from wireless_vm_fixture import ROOT, CORE, build, extract, run

RADIO=selection_fixture.RADIO
HEADER=selection_fixture.HEADER


class WiFiStartCapture(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory();cls.addClassCleanup(cls.temp.cleanup)
        options=(CORE/'esp32_mquickjs_options.c').read_text().replace(
            '#include "esp32_mquickjs_options.h"',
            (ROOT/'components/esp32_mquickjs/internal/esp32_mquickjs_options.h').read_text().replace(
                '#include "esp32_mquickjs_types.h"',''))
        config=(ROOT/'components/esp32_mquickjs/src/modules/wifi/esp32_mquickjs_wifi_config.c').read_text()
        types='typedef int wifi_mode_t,wifi_storage_t;\n#define WIFI_MODE_STA 1\n#define WIFI_MODE_AP 2\n#define WIFI_MODE_APSTA 3\n#define WIFI_STORAGE_RAM 1\n#define WIFI_STORAGE_FLASH 0\n'
        cls.binary=build(cls.temp.name,types+structure(HEADER.read_text(),'esp32_mquickjs_wifi_radio_configuration_selection_t')+
                         options+extract(config,'esp32_mquickjs_wifi_capture_start'),CAPTURE_MAIN)

    def test_capture_defaults_choices_strict_input_gc_and_allocation_failures(self):
        for expression,expected,mode,storage in [
            ('undefined',1,0,-1),('({})',1,0,-1),('({mode:"station"})',1,1,-1),
            ('({mode:"ap",storage:"flash"})',1,2,0),('({mode:"apsta",storage:"ram"})',1,3,1),
            ('null',0,0,-1),('[]',0,0,-1),('({mode:1})',0,0,-1),('({storage:true})',0,0,-1),
            ('({mode:"station\\u0000"})',0,0,-1),('({storage:"flash\\u0000"})',0,0,-1),
            ('({allowDisconnect:true})',0,0,-1),('({"mode\\u0000":"ap"})',0,0,-1),
            ('({start_only:true})',0,0,-1),
            ('({get mode(){throw 12345;}})',0,0,-1),
            ('({get storage(){throw 12345;}})',0,0,-1),
            ('({get mode(){gc();return "apsta";},get storage(){gc();return "ram";}})',1,3,1)]:
            run([str(self.binary),expression,str(expected),str(mode),str(storage)])


CAPTURE_MAIN = r'''
int main(int argc,char **argv) {
    assert(argc==5);int expected=atoi(argv[2]),mode=atoi(argv[3]),storage=atoi(argv[4]),total=1;
    for(int nth=0;nth<=total;nth++) {
        void *heap=malloc(128*1024);JSContext *ctx=JS_NewContext(heap,128*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
        JSGCRef ref;JSValue *root=JS_PushGCRef(ctx,&ref);
        *root=JS_Eval(ctx,argv[1],strlen(argv[1]),"start",JS_EVAL_RETVAL);assert(!JS_IsException(*root));
        esp32_mquickjs_wifi_radio_configuration_selection_t s;memset(&s,0x55,sizeof(s));
        calls=0;fail_at=nth;inject=true;collect=true;
        bool ok=esp32_mquickjs_wifi_capture_start(ctx,*root,&s);
        if(!nth){total=calls;assert(ok==expected);}
        if(ok) {
            assert(s.start_only && s.start && s.start_set && !s.allow_disconnect && !s.station_set && !s.access_point_set);
            assert(s.mode_set==(mode!=0) && (!mode || s.mode==mode));
            assert(s.storage_set==(storage>=0) && (storage<0 || s.storage==storage));
        } else {
            for(size_t i=0;i<sizeof(s);i++)assert(!((unsigned char *)&s)[i]);
            assert(JS_HasException(ctx));JSValue error=JS_GetException(ctx);
            if(!nth && strstr(argv[1],"throw 12345"))assert(error==JS_NewInt32(ctx,12345));
        }
        inject=false;collect=false;JS_PopGCRef(ctx,&ref);assert(!root_count && !native_live);JS_FreeContext(ctx);free(heap);
    }
}
'''


class WiFiStartResolution(unittest.TestCase):
    def code(self,softap=True):
        header,radio=HEADER.read_text(),RADIO.read_text()
        enums=re.search(r'typedef enum \{[^}]*\} esp32_mquickjs_wifi_radio_client_t;',header).group(0)
        structs='typedef unsigned esp32_mquickjs_wifi_promiscuous_token_t;\n'+structure(radio,'wifi_radio_live_lease_t')+''.join(structure(header,n) for n in ['esp32_mquickjs_wifi_radio_lease_t',
            'esp32_mquickjs_wifi_radio_lifecycle_t','esp32_mquickjs_wifi_radio_configuration_selection_t'])
        functions=''.join(extract(radio,n) for n in ['wifi_radio_lease_valid','wifi_radio_acquire_locked','wifi_radio_begin_lifecycle_with_dependents_locked', 'wifi_radio_begin_lifecycle_locked',
                                                    'esp32_mquickjs_wifi_radio_begin_start_lifecycle'])
        boundaries=selection_fixture.BOUNDARIES.replace(
            'struct {unsigned identity;esp32_mquickjs_wifi_radio_client_t client;} leases[WIFI_RADIO_MAX_LEASES];',
            'wifi_radio_live_lease_t leases[WIFI_RADIO_MAX_LEASES];\n    unsigned clients[ESP32_MQUICKJS_WIFI_RADIO_CLIENT_COUNT],next_lease_identity;int fault_error;')
        boundaries=boundaries.replace('s_radio.generation=7;', 's_radio.generation=7;s_radio.next_lease_identity=20;')
        return PRELUDE+f'#define CONFIG_ESP_WIFI_SOFTAP_SUPPORT {int(softap)}\n'+sdk_types('esp32c5/representative')+enums+structs+boundaries+START_BOUNDARIES+functions

    def test_stopped_defaults_preserve_mode_storage_and_running_requests_do_not_reconfigure(self):
        compile_run(self,self.code()+r'''
int main(void) {
    for(int mode=WIFI_MODE_STA;mode<=WIFI_MODE_APSTA;mode++) {
        reset_fixture();s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STOPPED;
        s_radio.storage_configured=true;s_radio.effective_mode=mode;s_radio.storage=WIFI_STORAGE_FLASH;
        esp32_mquickjs_wifi_radio_configuration_selection_t request={.start_only=true};
        esp32_mquickjs_wifi_radio_lifecycle_t token={0};bool running=true;
        assert(esp32_mquickjs_wifi_radio_begin_start_lifecycle(NULL,NULL,NULL,&request,&token,&running)==0);
        assert(!running && token.identity==11 && request.mode==mode && request.storage==WIFI_STORAGE_FLASH && request.start);
        assert(!mode_queries && !s_radio.started);
    }
    reset_fixture();esp32_mquickjs_wifi_radio_configuration_selection_t request={.start_only=true};
    esp32_mquickjs_wifi_radio_lifecycle_t token={0};bool running=true;
    assert(esp32_mquickjs_wifi_radio_begin_start_lifecycle(NULL,NULL,NULL,&request,&token,&running)==0);
    assert(!running && request.mode==WIFI_MODE_STA && request.storage==WIFI_STORAGE_RAM);
    reset_fixture();request=(esp32_mquickjs_wifi_radio_configuration_selection_t){.start_only=true};token.identity=0;
    esp32_mquickjs_wifi_radio_lease_t ap={7,6,ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_AP,true};
    s_radio.leases[0].identity=6;s_radio.leases[0].client=ap.client;change_at_lock=true;
    assert(esp32_mquickjs_wifi_radio_begin_start_lifecycle(NULL,NULL,&ap,&request,&token,&running)==0);
    assert(running && !token.identity && request.mode==WIFI_MODE_AP && request.storage==WIFI_STORAGE_FLASH && ap.acquired);
    assert(s_radio.next_lifecycle_identity==11 && mode_queries==1);
    request.mode_set=true;request.mode=WIFI_MODE_STA;
    assert(esp32_mquickjs_wifi_radio_begin_start_lifecycle(NULL,NULL,&ap,&request,&token,&running)==ESP_ERR_INVALID_STATE);
    assert(!running && !token.identity && mode_queries==1 && ap.acquired);
    request.mode=WIFI_MODE_AP;request.storage_set=true;request.storage=WIFI_STORAGE_RAM;
    assert(esp32_mquickjs_wifi_radio_begin_start_lifecycle(NULL,NULL,&ap,&request,&token,&running)==ESP_ERR_INVALID_STATE && mode_queries==1);
    request.storage=WIFI_STORAGE_FLASH;ap.generation--;
    assert(esp32_mquickjs_wifi_radio_begin_start_lifecycle(NULL,NULL,&ap,&request,&token,&running)==ESP_ERR_INVALID_STATE && mode_queries==1);
}
''')

    def test_running_station_gets_application_owner_before_mutex_release(self):
        compile_run(self,self.code()+r'''
int main(void) {
    reset_fixture();s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STARTED;
    s_radio.storage_configured=true;s_radio.started=s_radio.stop_required=true;
    s_radio.effective_mode=WIFI_MODE_STA;s_radio.storage=WIFI_STORAGE_FLASH;
    s_radio.leases[0]=(wifi_radio_live_lease_t){.identity=9,.client=ESP32_MQUICKJS_WIFI_RADIO_CLIENT_CSI,.required_mode=WIFI_MODE_STA};
    s_radio.clients[ESP32_MQUICKJS_WIFI_RADIO_CLIENT_CSI]=1;
    esp32_mquickjs_wifi_radio_lease_t app={0};
    esp32_mquickjs_wifi_radio_configuration_selection_t request={.start_only=true};
    esp32_mquickjs_wifi_radio_lifecycle_t token={0};bool running=false;
    assert(esp32_mquickjs_wifi_radio_begin_start_lifecycle(&app,NULL,NULL,&request,&token,&running)==0);
    assert(running && !token.identity && app.acquired && app.identity==20 && app.generation==7);
    assert(s_radio.leases[1].identity==20 && s_radio.clients[app.client]==1 && request.storage==WIFI_STORAGE_FLASH);
    assert(s_radio.leases[0].identity==9 && s_radio.next_lease_identity==21);
    assert(esp32_mquickjs_wifi_radio_begin_start_lifecycle(&app,NULL,NULL,&request,&token,&running)==0);
    assert(app.identity==20 && s_radio.next_lease_identity==21 && !locked);
}
''')

    def test_softap_gate_precedes_admission_and_driver_access(self):
        compile_run(self,self.code(False)+r'''
int main(void) {
    reset_fixture();esp32_mquickjs_wifi_radio_configuration_selection_t request={.mode_set=true,.mode=WIFI_MODE_APSTA,.start_only=true};
    esp32_mquickjs_wifi_radio_lifecycle_t token={0};bool running=true;
    assert(esp32_mquickjs_wifi_radio_begin_start_lifecycle(NULL,NULL,NULL,&request,&token,&running)==ESP_ERR_NOT_SUPPORTED);
    assert(!running && !token.identity && !mode_queries && s_radio.next_lifecycle_identity==11);
}
''')


START_BOUNDARIES = r'''
#define WIFI_RADIO_NAN_PENDING false
#define WIFI_RADIO_MESH_PENDING false
static int mode_queries,mode_error;
static int esp_wifi_get_mode(wifi_mode_t *mode) { assert(locked && !critical);mode_queries++;*mode=s_radio.effective_mode;return mode_error; }
'''


class WiFiStartExecutor(unittest.TestCase):
    def test_configured_apsta_start_reuses_helpers_and_never_rewrites_ap_credentials(self):
        compile_run(self,executor_fixture.WiFiConfigurationExecutor().code()+r'''
int main(void) {
    esp32_mquickjs_wifi_radio_configuration_selection_t request={.start_only=true};
    expect_no_config_inputs=true;
    assert(wifi_configure_selected_interfaces(&request,NULL,NULL,NULL,NULL,NULL,NULL,NULL)==0);
    assert(request.mode==WIFI_MODE_APSTA && request.storage==WIFI_STORAGE_FLASH);
    assert(calls[7]==1 && calls[8]==1 && calls[10]==1 && calls[11]==1 && stored_ap_reads==1);
    assert(s_wifi_application==1 && s_wifi_state.radio_lease==2 && ap_lease==3 && !live_allocation);
    assert(!s_wifi_configuration_cleanup && !s_wifi_lifecycle.identity);
    reset_fixture();start_already_running=true;
    assert(wifi_configure_selected_interfaces(&request,NULL,NULL,NULL,NULL,NULL,NULL,NULL)==0);
    assert(running_starts==1 && !s_wifi_lifecycle.identity && !s_wifi_configuration_cleanup);
    for(int i=0;i<12;i++)assert(!calls[i]);
    start_already_running=false;
    for(int fail=0;fail<3;fail++) {
        reset_fixture();allocation_failure=fail==0;stored_ap_error=fail==1?-77:0;fail_at=fail==2?11:0;
        assert(wifi_configure_selected_interfaces(&request,NULL,NULL,NULL,NULL,NULL,NULL,NULL)!=0);
        assert(s_wifi_configuration_cleanup && s_wifi_lifecycle.identity==7 && !live_allocation);
        if(fail==0)assert(!calls[3] && !strcmp(s_wifi_state.cleanup_stage,"start-ap-allocate"));
        if(fail==1)assert(!calls[11] && !strcmp(s_wifi_state.cleanup_stage,"start-ap-config"));
        assert(wifi_finish_configuration_cleanup()==0);
    }
}
''')


class WiFiStartStoredConfig(unittest.TestCase):
    def test_exact_stopped_admission_and_failed_snapshot_are_zeroed(self):
        radio=RADIO.read_text()
        body=extract(radio,'wifi_radio_check_stopped_lifecycle_locked')
        body+=extract((CORE/'esp32_mquickjs_wireless_core.c').read_text(),'esp32_mquickjs_wireless_secure_zero')
        body+=extract(radio,'wifi_radio_validate_saved_ap_config')
        body+=extract(radio,'esp32_mquickjs_wifi_radio_copy_stopped_ap_configuration')
        compile_run(self,STORED_BOUNDARIES+body+r'''
int main(void) {
    for(int scenario=0;scenario<6;scenario++) {
        s_radio.lifecycle=(esp32_mquickjs_wifi_radio_lifecycle_t){1,7};s_radio.driver_state=2;
        s_radio.driver_owned=s_radio.storage_configured=true;s_radio.leases[0].identity=0;
        getter_error=scenario==1;validation_error=scenario==2;regulatory_error=scenario==3;gets=validations=regulations=0;
        esp32_mquickjs_wifi_radio_lifecycle_t token={1,7};
        if(scenario==4)token.generation=2;
        if(scenario==5)s_radio.leases[0].identity=9;
        wifi_config_t config;memset(&config,0x33,sizeof(config));
        int err=esp32_mquickjs_wifi_radio_copy_stopped_ap_configuration(&token,&config);
        if(!scenario)assert(err==0 && config.ap.channel==6 && config.secret[0]==0x55 && gets==1 && regulations==1);
        else {
            assert(err!=0);for(size_t i=0;i<sizeof(config);i++)assert(!((unsigned char *)&config)[i]);
            if(scenario>=4)assert(!gets && !validations && !regulations);
        }
        assert(s_radio.lifecycle.identity==7 && !depth);
        esp32_mquickjs_wireless_secure_zero(&config,sizeof(config));
    }
}
''')


STORED_BOUNDARIES = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1
#define CONFIG_ESP_WIFI_SOFTAP_SUPPORT 1
#define WIFI_RADIO_MAX_LEASES 2
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG -1
#define ESP_ERR_INVALID_STATE -2
#define ESP32_MQUICKJS_WIFI_RADIO_UNINITIALIZED 0
#define ESP32_MQUICKJS_WIFI_RADIO_STOPPED 2
#define ESP32_MQUICKJS_WIFI_RADIO_FAULTED 3
#define ESP32_MQUICKJS_WIFI_RADIO_CLEANUP_PENDING 4
#define WIFI_IF_AP 1
typedef int esp_err_t;
typedef struct {unsigned generation,identity;} esp32_mquickjs_wifi_radio_lifecycle_t;
typedef int wifi_sae_pwe_method_t;
#define WIFI_AUTH_WPA3_PSK 6
#define WIFI_AUTH_WPA2_WPA3_PSK 7
#define WPA3_SAE_PWE_UNSPECIFIED 0
#define WPA3_SAE_PWE_HUNT_AND_PECK 1
#define WPA3_SAE_PWE_HASH_TO_ELEMENT 2
#define WPA3_SAE_PWE_BOTH 3
typedef struct {struct {uint8_t channel; int authmode,sae_pwe_h2e; bool wpa3_compatible_mode;} ap;uint8_t secret[64];} wifi_config_t;
static struct {
    esp32_mquickjs_wifi_radio_lifecycle_t lifecycle;
    bool started,stop_required,promiscuous_claimed,restart_required,driver_owned,storage_configured;
    struct {unsigned identity;} operation,leases[2];
    unsigned wake_locks;int driver_state;const char *fault_stage,*cleanup_stage;
} s_radio;
static int depth,gets,validations,regulations;
static bool getter_error,validation_error,regulatory_error;
static void wifi_radio_operation_lock(void) {assert(!depth);depth=1;}
static void wifi_radio_operation_unlock(void) {assert(depth);depth=0;}
static int esp_wifi_get_config(int iface,wifi_config_t *config) {
    assert(depth && iface==WIFI_IF_AP);gets++;memset(config,0x55,sizeof(*config));config->ap.channel=6;
    config->ap.authmode=3;config->ap.sae_pwe_h2e=WPA3_SAE_PWE_BOTH;config->ap.wpa3_compatible_mode=false;
    return getter_error?-7:0;
}
static int esp32_mquickjs_wifi_radio_validate_ap_config(const wifi_config_t *config) {
    assert(depth && config->secret[0]==0x55);validations++;return validation_error?-8:0;
}
static int wifi_radio_validate_regulatory_channel(uint8_t channel) {assert(depth && channel==6);regulations++;return regulatory_error?-9:0;}
'''
