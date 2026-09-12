"""Deferred production interface-config admission, transaction and VM regressions."""
import tempfile
import unittest

from test_wifi_config_controls import BOUNDARIES, HEADER, MAIN, PRELUDE, RADIO, sdk_types, structure
from test_wifi_driver_capture import BOUNDARIES as CAPTURE_BOUNDARIES
from test_wireless_control_regression import compile_run
from wireless_vm_fixture import CORE, ROOT, build, extract, run

COMPONENT = ROOT / 'components/esp32_mquickjs'
DRIVER = COMPONENT / 'src/modules/wifi_driver/esp32_mquickjs_wifi_driver.c'


READ_BOUNDARY = r'''
#define ESP32_MQUICKJS_WIFI_RADIO_STARTED 3
#define ESP_ERR_WIFI_NOT_INIT -7
static struct {
 int lock,driver_state;bool driver_owned,storage_configured,restart_required;
 const char *fault_stage,*cleanup_stage;struct{unsigned identity;}lifecycle,operation;
}s_radio;
static int depth,critical,reads,native_error;
static void wifi_radio_operation_lock(void){assert(!depth&&!critical);depth=1;}
static void wifi_radio_operation_unlock(void){assert(depth&&!critical);depth=0;}
static int esp_wifi_get_config(wifi_interface_t interface,wifi_config_t *out){
 assert(depth&&!critical);reads++;memset(out,0,sizeof(*out));
 if(interface==WIFI_IF_STA){memcpy(out->sta.ssid,"station",7);memcpy(out->sta.password,"native-secret",13);memcpy(out->sta.sae_h2e_identifier,"private",7);}
 else{out->ap.ssid[0]=255;out->ap.ssid[1]=0;out->ap.ssid[2]=65;out->ap.ssid_len=3;memcpy(out->ap.password,"ap-secret",9);}
 return native_error; /* Can fill the buffer before returning failure. */
}
'''

READ_MAIN = r'''
int main(void){
 const char *stage;wifi_config_t config;
 s_radio.driver_owned=s_radio.storage_configured=true;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STOPPED;
 for(int interface=0;interface<2;interface++){
  assert(esp32_mquickjs_wifi_radio_read_interface_config(interface,false,&config,&stage)==0&&!stage);
  const uint8_t *secret=interface==WIFI_IF_STA?config.sta.password:config.ap.password;
  for(unsigned i=0;i<64;i++)assert(secret[i]==0);
  if(interface==WIFI_IF_STA)for(unsigned i=0;i<sizeof(config.sta.sae_h2e_identifier);i++)assert(!config.sta.sae_h2e_identifier[i]);
  int before=reads;memset(&config,0x55,sizeof(config));
  int err=esp32_mquickjs_wifi_radio_read_interface_config(interface,true,&config,&stage);
#if CONFIG_ESP32_MQUICKJS_WIFI_ALLOW_SECRET_READBACK
  assert(err==0&&reads==before+1&&secret[0]);
#else
  assert(err==ESP_ERR_NOT_ALLOWED&&reads==before&&!strcmp(stage,"secret-readback"));
  for(unsigned i=0;i<sizeof(config);i++)assert(!((uint8_t*)&config)[i]);
#endif
 }
 native_error=-77;assert(esp32_mquickjs_wifi_radio_read_interface_config(WIFI_IF_STA,false,&config,&stage)==-77);
 for(unsigned i=0;i<sizeof(config);i++)assert(!((uint8_t*)&config)[i]);native_error=0;
 for(int gate=0;gate<6;gate++){
  s_radio.lifecycle.identity=s_radio.operation.identity=0;s_radio.restart_required=false;s_radio.fault_stage=s_radio.cleanup_stage=NULL;s_radio.driver_owned=true;
  if(gate==0)s_radio.lifecycle.identity=1;if(gate==1)s_radio.operation.identity=1;if(gate==2)s_radio.restart_required=true;
  if(gate==3)s_radio.fault_stage="fault";if(gate==4)s_radio.cleanup_stage="cleanup";if(gate==5)s_radio.driver_owned=false;
  int count=reads;assert(esp32_mquickjs_wifi_radio_read_interface_config(WIFI_IF_STA,false,&config,&stage)!=0&&reads==count);
 }
 assert(!depth&&!critical);
}
'''

WRITE_MAIN = r'''
static void prepare(void){reset();s_radio.lifecycle.identity=0;memset(&s_tx_rate_lease,0,sizeof(s_tx_rate_lease));}
static int write_config(void){return esp32_mquickjs_wifi_radio_write_interface_config(WIFI_IF_STA,&requested[0],&result);}
int main(void){
 prepare();assert(write_config()==0);int total=calls;assert(!memcmp(&native_config[0],&requested[0],sizeof(wifi_config_t)));
 assert(native_mode==WIFI_MODE_STA&&native_storage==WIFI_STORAGE_RAM&&!s_radio.started&&!alloc_live);
 for(int nth=1;nth<=total;nth++){
  prepare();fail_at=nth;assert(write_config()==-77&&result.error==-77);
  assert(!alloc_live&&!depth&&!critical&&native_mode==WIFI_MODE_STA&&native_storage==WIFI_STORAGE_RAM);
  assert(!memcmp(native_config,before,sizeof(before)));
  if(result.mutation_attempted)assert(result.rollback_complete);else assert(!writes);
 }
 prepare();fail_at=total;rollback_fail=1;assert(write_config()==-77&&result.rollback_error==-88&&s_radio.fault_stage&&s_radio.cleanup_stage);
 for(int gate=0;gate<7;gate++){
  prepare();if(gate==0)s_radio.leases[1].identity=1;if(gate==1)s_radio.lifecycle.identity=1;
  if(gate==2)s_radio.operation.identity=1;if(gate==3)s_tx_rate_lease.restore_pending=true;
  if(gate==4)s_radio.wake_locks=1;if(gate==5)s_radio.started=true;if(gate==6)s_radio.effective_mode=WIFI_MODE_AP;
  assert(write_config()==ESP_ERR_INVALID_STATE&&!calls&&!alloc_live);
 }
 prepare();allocation_failure=true;assert(write_config()==ESP_ERR_NO_MEM&&!calls&&!alloc_live);
}
'''


class WiFiInterfaceConfig(unittest.TestCase):
    def test_public_read_write_snapshots_secret_gate_gc_and_every_allocation_failure(self):
        radio, driver = RADIO.read_text(), DRIVER.read_text()
        header = HEADER.read_text()
        local = structure(header, 'esp32_mquickjs_wifi_radio_config_result_t')
        options = (CORE / 'esp32_mquickjs_options.c').read_text().replace(
            '#include "esp32_mquickjs_options.h"',
            (COMPONENT / 'internal/esp32_mquickjs_options.h').read_text().replace(
                '#include "esp32_mquickjs_types.h"', ''))
        capture = (COMPONENT / 'src/modules/wifi/esp32_mquickjs_wifi_config.c').read_text()
        capture = ''.join(extract(capture, n) for n in ('wifi_capture_config_ssid',
            'wifi_driver_config_unsupported', 'esp32_mquickjs_wifi_parse_driver_config_for_operation'))
        adapter = ''.join(extract(driver, n) for n in (
            'tx_rate_interface', 'driver_read_error', 'driver_phy_write_error', 'driver_config_bytes',
            'driver_config_text', 'driver_config_enum', 'driver_config_scan_name', 'driver_config_sort_name',
            'driver_config_auth_name', 'driver_config_pwe_name', 'driver_config_pk_name', 'driver_config_cipher_name',
            'driver_interface_config_to_js', 'driver_config_free', 'js_wifi_driver_get_interface_config',
            'js_wifi_driver_set_interface_config'))
        zero = extract((CORE / 'esp32_mquickjs_wireless_core.c').read_text(), 'esp32_mquickjs_wireless_secure_zero')
        address = extract((CORE / 'esp32_mquickjs_wireless_core.c').read_text(), 'esp32_mquickjs_wireless_format_address')
        throw = extract((CORE / 'esp32_mquickjs.c').read_text(), 'esp32_mquickjs_throw_native_error')
        for secret in (0, 1):
            with self.subTest(secret=secret), tempfile.TemporaryDirectory() as directory:
                flags = (f'\n#define CONFIG_ESP32_MQUICKJS_WIFI_ALLOW_SECRET_READBACK {secret}\n'
                    '#define CONFIG_ESP32_MQUICKJS_FEATURE_WIFI 1\n#define CONFIG_SOC_WIFI_SUPPORT_5G 1\n'
                    '#define CONFIG_SOC_WIFI_HE_SUPPORT 1\n#define CONFIG_ESP_WIFI_SOFTAP_SUPPORT 1\n'
                    '#define ESP32_MQUICKJS_WIFI_AP_BEACON_QUANTUM_TU 100\n#define ESP32_MQUICKJS_WIFI_AP_BEACON_MAX_TU 60000\n')
                body = '#include "cutils.h"\n' + PRELUDE + flags + sdk_types('esp32c5/representative') + local
                body += READ_BOUNDARY + VM_BOUNDARY + CAPTURE_BOUNDARIES[CAPTURE_BOUNDARIES.index('static int policy_calls'):]
                body += options + zero + address + throw
                body += extract(radio, 'esp32_mquickjs_wifi_radio_read_interface_config')
                body += extract(radio, 'esp32_mquickjs_wifi_radio_5ghz_channel_bit')
                body += (COMPONENT / 'internal/esp32_mquickjs_wifi_config_fields.h').read_text()
                body += (COMPONENT / 'internal/esp32_mquickjs_wifi_config_observations.inc').read_text() + capture + adapter
                binary = build(directory, body, VM_MAIN,
                    globals_extra='JS_CFUNC_DEF("getConfig",1,js_wifi_driver_get_interface_config),JS_CFUNC_DEF("setConfig",2,js_wifi_driver_set_interface_config),',
                    declarations='JSValue js_wifi_driver_get_interface_config(JSContext*,JSValue*,int,JSValue*);\nJSValue js_wifi_driver_set_interface_config(JSContext*,JSValue*,int,JSValue*);\n')
                cases = [
                    ('getConfig("station")', 1, 1, 0), ('getConfig("access-point")', 1, 1, 0),
                    ('getConfig("station",{includeSecrets:true})', secret, secret, secret),
                    ('getConfig("access-point",{includeSecrets:true})', secret, secret, secret),
                    ('getConfig("station",{includeSecrets:1})', 0, 0, 0),
                    ('getConfig("station",{unknown:1})', 0, 0, 0),
                    ('setConfig("station",{ssid:"A",password:"12345678"})', 1, 1, 0),
                    ('setConfig("access-point",{ssid:[255,0,65],password:"12345678"})', 1, 1, 0),
                    ('setConfig("station",{ssid:"A",unknown:1})', 0, 0, 0),
                    ('setConfig("bad",{ssid:"A"})', 0, 0, 0),
                ]
                for script, dispatched, success, included in cases:
                    run([str(binary), script, str(dispatched), str(success), str(included), '0'])
                run([str(binary), 'getConfig("station")', '1', '0', '0', '1'])

    def test_native_read_redaction_secret_gate_failure_zeroing_and_admission(self):
        radio = RADIO.read_text()
        zero = extract((CORE / 'esp32_mquickjs_wireless_core.c').read_text(), 'esp32_mquickjs_wireless_secure_zero')
        getter = extract(radio, 'esp32_mquickjs_wifi_radio_read_interface_config')
        for secret in (0, 1):
            with self.subTest(secret=secret):
                compile_run(self, PRELUDE + f'\n#define CONFIG_ESP_WIFI_SOFTAP_SUPPORT 1\n#define CONFIG_ESP32_MQUICKJS_WIFI_ALLOW_SECRET_READBACK {secret}\n'
                            + sdk_types('esp32c5/representative') + READ_BOUNDARY + zero + getter + READ_MAIN)

    def test_native_write_uses_production_transaction_rollback_and_secure_release(self):
        source, header = RADIO.read_text(), HEADER.read_text()
        local = '\n'.join(structure(header, name) for name in (
            'esp32_mquickjs_wifi_radio_lifecycle_t', 'esp32_mquickjs_wifi_radio_config_result_t',
            'esp32_mquickjs_wifi_radio_config_controls_t'))
        local += '\ntypedef bool (*esp32_mquickjs_wifi_config_accept_fn)(const wifi_config_t *,const wifi_config_t *);\n'
        # Interface semantic normalization is exercised by existing Station/AP
        # accept fixtures; this boundary deliberately requires exact bytes.
        local += ('static bool accept_config(const wifi_config_t *,const wifi_config_t *);\n'
                  '#define esp32_mquickjs_wifi_radio_accept_station_config accept_config\n'
                  '#define esp32_mquickjs_wifi_radio_accept_ap_config accept_config\n')
        boundaries = BOUNDARIES.replace('started,stop_required,promiscuous_claimed', 'started,stop_required,restart_required,promiscuous_claimed')
        boundaries += '\nstatic struct{unsigned identity;bool restore_pending;}s_tx_rate_lease;\n#define ESP_ERR_WIFI_NOT_INIT -7\n'
        helpers = source[source.index('/* These helpers run inside'):source.index('/* Caller has already validated')]
        for name in ('esp32_mquickjs_wifi_radio_read_phy', 'esp32_mquickjs_wifi_radio_write_phy'):
            helpers = helpers.replace(extract(source, name), '')
        production = ''.join(extract(source, name) for name in (
            'esp32_mquickjs_wifi_radio_5ghz_channel_bit', 'wifi_radio_validate_regulatory_channel',
            'esp32_mquickjs_wifi_radio_pmf_disable_allowed', 'wifi_radio_restore_disabled_pmf', 'wifi_radio_config_equal'))
        production += helpers + extract(source, 'wifi_radio_configure_locked')
        production += extract(source, 'esp32_mquickjs_wifi_radio_write_interface_config')
        zero = extract((CORE / 'esp32_mquickjs_wireless_core.c').read_text(), 'esp32_mquickjs_wireless_secure_zero')
        for target, he, five in [('esp32c3', 0, 0), ('esp32c5', 1, 1)]:
            with self.subTest(target=target):
                flags = f'\n#define CONFIG_SOC_WIFI_SUPPORT_5G {five}\n#define CONFIG_SOC_WIFI_HE_SUPPORT {he}\n#define CONFIG_ESP_WIFI_SOFTAP_SUPPORT 1\n'
                compile_run(self, PRELUDE + flags + sdk_types(target + '/representative') + local + boundaries + zero + production
                            + MAIN[:MAIN.index('int main(void)')].replace(extract(MAIN, 'configure'), '') + WRITE_MAIN)


VM_BOUNDARY = r'''
static int writes;
static void checked_config_free(void *p){
 if(p)for(size_t i=0;i<sizeof(wifi_config_t);i++)assert(!((uint8_t*)p)[i]);
 esp32_mquickjs_memory_payload_free(p);
}
#define esp32_mquickjs_memory_payload_free checked_config_free
static int esp32_mquickjs_wifi_radio_write_interface_config(wifi_interface_t interface,
 wifi_config_t *config,esp32_mquickjs_wifi_radio_config_result_t *result){
 assert(writes++==0);assert(config->sta.password[0]);
 if(interface==WIFI_IF_STA)config->sta.listen_interval=3;
 *result=(esp32_mquickjs_wifi_radio_config_result_t){.stage="complete",.mutation_attempted=true};return 0;
}
'''

VM_MAIN = r'''
#undef JS_GetPropertyStr
#undef JS_GetPropertyUint32
int main(int argc,char **argv){
 assert(argc==6);int expected=atoi(argv[2]),success=atoi(argv[3]),secrets=atoi(argv[4]);int total=0;bool delivery_failure=false;
 for(int nth=0;nth<=total;nth++){
  void *heap=malloc(256*1024);JSContext *ctx=JS_NewContext(heap,256*1024,&js_stdlib);assert(ctx);
  JSGCRef out_ref;JSValue *out=JS_PushGCRef(ctx,&out_ref);
  memset(&s_radio,0,sizeof(s_radio));s_radio.driver_owned=s_radio.storage_configured=true;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STOPPED;
  reads=writes=0;native_error=atoi(argv[5])?-77:0;calls=0;fail_at=nth;collect=inject=true;
  *out=JS_Eval(ctx,argv[1],strlen(argv[1]),"driver-config",JS_EVAL_RETVAL);inject=collect=false;
  if(!nth){total=calls;assert(reads+writes==expected);assert(JS_IsException(*out)==!success);}
  assert(reads+writes<=1);
  if(JS_IsException(*out)){assert(JS_HasException(ctx));JS_GetException(ctx);if(nth&&writes)delivery_failure=true;}
  else{
   assert(success);JSValue flag=JS_GetPropertyStr(ctx,*out,"secretsIncluded");assert(flag==(secrets?JS_TRUE:JS_FALSE));
   JSValue password=JS_GetPropertyStr(ctx,*out,"password");assert(secrets?JS_IsString(ctx,password):JS_IsNull(password));
   JSValue bytes=JS_GetPropertyStr(ctx,*out,"passwordBytes");assert(secrets?JS_IsArray(ctx,bytes):JS_IsNull(bytes));
   if(strstr(argv[1],"station")){
    JSValue secret=JS_GetPropertyStr(ctx,*out,"saeH2eIdentifierBytes");assert(secrets?JS_IsArray(ctx,secret):JS_IsNull(secret));
    if(writes){int32_t interval;assert(!JS_ToInt32(ctx,&interval,JS_GetPropertyStr(ctx,*out,"listenInterval"))&&interval==3);}
   }else{
    assert(JS_IsNull(JS_GetPropertyStr(ctx,*out,"ssid"))); /* Binary AP SSID stays lossless. */
    bytes=JS_GetPropertyStr(ctx,*out,"ssidBytes");int32_t length;assert(!JS_ToInt32(ctx,&length,JS_GetPropertyStr(ctx,bytes,"length"))&&length==3);
   }
  }
  JS_PopGCRef(ctx,&out_ref);JS_GC(ctx);assert(!root_count&&!native_live&&!depth&&!critical);JS_FreeContext(ctx);free(heap);
 }
 if(success&&strstr(argv[1],"setConfig"))assert(delivery_failure);
}
'''
