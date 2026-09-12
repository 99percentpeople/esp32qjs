"""Deferred country-control regressions using production Radio and VM helpers.

SDK storage/callback boundaries are injected. These fixtures do not establish
country-specific SDK normalization, flash durability, or RF behavior.
"""
import re
import tempfile
import unittest

from test_wifi_config_controls import PRELUDE, sdk_types, structure
from test_wireless_control_regression import compile_run
from wireless_vm_fixture import CORE, ROOT, build, extract, run

COMPONENT = ROOT / 'components/esp32_mquickjs'
RADIO = COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c'
HEADER = COMPONENT / 'internal/esp32_mquickjs_wifi_radio.h'


def types(target):
    header = HEADER.read_text()
    out = sdk_types(target + '/representative', ('wifi_ap_record_t',))
    for name in ('esp32_mquickjs_wifi_radio_config_controls_t',
                 'esp32_mquickjs_wifi_radio_config_result_t',
                 'esp32_mquickjs_wifi_radio_mutation_t'):
        out += structure(header, name) + '\n'
    return out


def gates(target):
    five = int(target == 'esp32c5')
    return (f'\n#define CONFIG_SOC_WIFI_SUPPORT_5G {five}\n'
            f'#define CONFIG_SOC_WIFI_HE_SUPPORT {five}\n#define CONFIG_ESP_WIFI_SOFTAP_SUPPORT 1\n')


def validators(source):
    return ''.join(extract(source, name) for name in (
        'esp32_mquickjs_wifi_radio_5ghz_channel_bit', 'wifi_radio_validate_protocol',
        'esp32_mquickjs_wifi_radio_validate_config_controls'))


NATIVE_BOUNDARY = r'''
#define ESP_ERR_WIFI_NOT_INIT -7
#define ESP_ERR_WIFI_NOT_CONNECT -8
#define ESP32_MQUICKJS_WIFI_RADIO_STARTED 3
static struct {
    int lock,driver_state,fault_error;unsigned generation,wake_locks;
    bool driver_owned,storage_configured,started,stop_required,restart_required,promiscuous_claimed;
    const char *fault_stage,*cleanup_stage;wifi_mode_t effective_mode;
    struct {unsigned identity;} lifecycle,operation;
    struct {unsigned identity;esp32_mquickjs_wifi_radio_client_t client;} leases[WIFI_RADIO_MAX_LEASES];
    unsigned clients[ESP32_MQUICKJS_WIFI_RADIO_CLIENT_COUNT];
    esp32_mquickjs_wifi_radio_config_result_t configuration;
} s_radio;
static struct {unsigned identity;bool restore_pending;} s_tx_rate_lease;
static int depth,critical,calls,writes,fail_at,fail_second,corrupt_at;
static bool associated;static wifi_country_t native_country;
static void wifi_radio_operation_lock(void){assert(!depth&&!critical);depth=1;}
static void wifi_radio_operation_unlock(void){assert(depth&&!critical);depth=0;}
#define taskENTER_CRITICAL(p) do{(void)(p);assert(!critical);critical=1;}while(0)
#define taskEXIT_CRITICAL(p) do{(void)(p);assert(critical);critical=0;}while(0)
static int step(bool write){assert(depth&&!critical);calls++;writes+=write;return calls==fail_at?-77:calls==fail_second?-88:0;}
static int wifi_radio_record_fault(const char *stage,int e){s_radio.fault_stage=stage;s_radio.fault_error=e;return e;}
static int wifi_radio_cleanup_fault(const char *stage,int e){s_radio.cleanup_stage=stage;return e;}
static int esp_wifi_sta_get_ap_info(wifi_ap_record_t *ap){(void)ap;int e=step(false);return e?e:associated?ESP_OK:ESP_ERR_WIFI_NOT_CONNECT;}
static int esp_wifi_get_country(wifi_country_t *c){int e=step(false);if(!e){*c=native_country;if(calls==corrupt_at)c->cc[0]='Z';}return e;}
static int esp_wifi_set_country(const wifi_country_t *c){
 int e=step(true);int8_t power=native_country.max_tx_power;native_country=*c;
 native_country.max_tx_power=power;if(!native_country.cc[2])native_country.cc[2]=' ';return e;
}
static int esp_wifi_set_country_code(const char *code,bool automatic){
 int e=step(true);native_country.cc[0]=code[0];native_country.cc[1]=code[1];native_country.cc[2]=' ';
 native_country.schan=1;native_country.nchan=13;
 native_country.policy=automatic?WIFI_COUNTRY_POLICY_AUTO:WIFI_COUNTRY_POLICY_MANUAL;return e;
}
/* The production composite helpers contain PHY branches; country-only calls
 * must never enter any of them. */
static int unexpected(void){assert(!"unexpected non-country SDK call");return -99;}
#define esp_wifi_get_band_mode(...) unexpected()
#define esp_wifi_get_protocols(...) unexpected()
#define esp_wifi_get_bandwidths(...) unexpected()
#define esp_wifi_get_protocol(...) unexpected()
#define esp_wifi_get_bandwidth(...) unexpected()
#define esp_wifi_set_protocols(...) unexpected()
#define esp_wifi_set_bandwidths(...) unexpected()
#define esp_wifi_set_protocol(...) unexpected()
#define esp_wifi_set_bandwidth(...) unexpected()
#define esp_wifi_get_ps(...) unexpected()
#define esp_wifi_set_ps(...) unexpected()
'''

NATIVE_RESET = r'''
static wifi_country_t requested,actual,before;
static esp32_mquickjs_wifi_radio_config_result_t result;
static esp32_mquickjs_wifi_radio_lease_t lease;
static esp32_mquickjs_wifi_radio_mutation_t mutation;
static void reset(bool live){
 assert(!depth&&!critical);memset(&s_radio,0,sizeof(s_radio));memset(&s_tx_rate_lease,0,sizeof(s_tx_rate_lease));
 memset(&result,0,sizeof(result));memset(&actual,0,sizeof(actual));calls=writes=fail_at=fail_second=corrupt_at=0;associated=false;
 s_radio.driver_owned=s_radio.storage_configured=true;s_radio.generation=9;s_radio.effective_mode=WIFI_MODE_STA;
 s_radio.started=live;s_radio.driver_state=live?ESP32_MQUICKJS_WIFI_RADIO_STARTED:ESP32_MQUICKJS_WIFI_RADIO_STOPPED;
 native_country=(wifi_country_t){.cc="US",.schan=1,.nchan=11,.max_tx_power=20,.policy=WIFI_COUNTRY_POLICY_MANUAL};before=native_country;
 requested=(wifi_country_t){.cc={'T','W','X'},.schan=1,.nchan=13,.policy=WIFI_COUNTRY_POLICY_MANUAL};
 lease=(esp32_mquickjs_wifi_radio_lease_t){.generation=9,.identity=7,.client=ESP32_MQUICKJS_WIFI_RADIO_CLIENT_WIFI_STA,.acquired=true};
 if(live){s_radio.leases[0].identity=7;s_radio.leases[0].client=lease.client;s_radio.clients[lease.client]=1;}
}
static int details(void){return esp32_mquickjs_wifi_radio_set_country_details(&requested,&actual,&result);}
static int code(void){return esp32_mquickjs_wifi_radio_set_country_code(&lease,"TW",false,&actual,&mutation);}
static void restored(void){assert(wifi_radio_country_equal(&before,&native_country,true));assert(!depth&&!critical);}
'''

DETAILS_MAIN = r'''
int main(void){
 for(int mode=WIFI_MODE_NULL;mode<=WIFI_MODE_APSTA;mode++){
  reset(false);s_radio.effective_mode=mode;assert(details()==0);assert(actual.max_tx_power==20&&actual.cc[2]=='X');
  assert(writes==1&&result.persistent_mutation_possible&&!result.rollback_attempted);
 }
 reset(false);requested=before;requested.max_tx_power=0;assert(details()==0&&!writes&&calls==1);
 reset(false);assert(details()==0);int total=calls;
 for(int nth=1;nth<=total;nth++){
  reset(false);fail_at=nth;assert(details()==-77&&result.error==-77);restored();
  assert(result.rollback_attempted==(nth>1));assert(result.persistent_mutation_possible==(nth>1));
  if(nth>1)assert(result.rollback_complete);
 }
 for(int offset=1;offset<=2;offset++){
  reset(false);fail_at=total;fail_second=total+offset;assert(details()==-77);
  assert(result.rollback_error==-88&&!result.rollback_complete&&s_radio.fault_stage&&s_radio.cleanup_stage);
  int count=calls;fail_at=fail_second=0;assert(details()==ESP_ERR_INVALID_STATE&&calls==count);
 }
 reset(false);corrupt_at=3;assert(details()==ESP_ERR_INVALID_RESPONSE&&result.rollback_complete);restored();
 for(int gate=0;gate<12;gate++){
  reset(false);
  switch(gate){case 0:s_radio.leases[1].identity=44;break;case 1:s_radio.started=true;break;
   case 2:s_radio.lifecycle.identity=1;break;case 3:s_radio.operation.identity=1;break;
   case 4:s_radio.wake_locks=1;break;case 5:s_radio.promiscuous_claimed=true;break;
   case 6:s_radio.stop_required=true;break;case 7:s_radio.restart_required=true;break;
   case 8:s_radio.storage_configured=false;break;case 9:s_tx_rate_lease.restore_pending=true;break;
   case 10:s_radio.cleanup_stage="pending";break;case 11:s_radio.fault_stage="fault";break;}
  assert(details()==ESP_ERR_INVALID_STATE&&!calls);
 }
 reset(false);requested.nchan=15;assert(details()==ESP_ERR_INVALID_ARG&&!calls);
 reset(false);requested.max_tx_power=20;assert(details()==ESP_ERR_INVALID_ARG&&!calls);
 reset(false);s_radio.driver_owned=false;assert(details()==ESP_ERR_WIFI_NOT_INIT&&!calls);
 reset(false);wifi_country_t copy=requested;
 assert(esp32_mquickjs_wifi_radio_set_country_details(&requested,&requested,&result)==ESP_ERR_INVALID_ARG);
 assert(!memcmp(&copy,&requested,sizeof(copy))&&!calls);
}
'''

CODE_MAIN = r'''
int main(void){
 reset(true);assert(code()==0&&mutation.driver_accepted&&actual.cc[0]=='T');int total=calls;
 /* A syntactically valid but different SDK readback used to report success. */
 reset(true);corrupt_at=total;assert(code()==ESP_ERR_INVALID_RESPONSE&&mutation.driver_accepted);
 assert(s_radio.configuration.rollback_complete&&s_radio.configuration.persistent_mutation_possible);restored();
 for(int nth=1;nth<=total;nth++){
  reset(true);fail_at=nth;assert(code()==-77&&s_radio.configuration.error==-77);restored();
  if(s_radio.configuration.mutation_attempted)assert(s_radio.configuration.rollback_complete);
  else assert(!writes);
 }
 reset(true);fail_at=total;fail_second=total+1;assert(code()==-77);
 assert(s_radio.configuration.rollback_error==-88&&s_radio.fault_stage&&s_radio.cleanup_stage);
 for(int gate=0;gate<5;gate++){
  reset(true);if(gate==0)lease.generation--;if(gate==1)lease.identity++;
  if(gate==2){s_radio.leases[1].identity=8;s_radio.leases[1].client=ESP32_MQUICKJS_WIFI_RADIO_CLIENT_ESPNOW;}
  if(gate==3)s_radio.effective_mode=WIFI_MODE_APSTA;if(gate==4)associated=true;
  assert(code()==ESP_ERR_INVALID_STATE&&!writes);
 }
 reset(true);s_radio.leases[1].identity=8;s_radio.leases[1].client=ESP32_MQUICKJS_WIFI_RADIO_CLIENT_APPLICATION;
 s_radio.clients[ESP32_MQUICKJS_WIFI_RADIO_CLIENT_APPLICATION]=1;assert(code()==0&&writes==1);
}
'''


class WiFiCountryDetails(unittest.TestCase):
    def native(self, main):
        source, header = RADIO.read_text(), HEADER.read_text()
        client = re.search(r'typedef enum \{[^}]*\} esp32_mquickjs_wifi_radio_client_t;', header).group(0)
        local = client + structure(header, 'esp32_mquickjs_wifi_radio_lease_t')
        local += structure(source, 'wifi_radio_controls_snapshot_t')
        helpers = validators(source) + ''.join(extract(source, name) for name in (
            'wifi_radio_country_valid', 'wifi_radio_country_equal', 'wifi_radio_read_phy',
            'wifi_radio_phy_equal', 'wifi_radio_write_phy', 'wifi_radio_snapshot_controls',
            'wifi_radio_apply_country', 'wifi_radio_restore_controls', 'wifi_radio_lease_valid',
            'wifi_radio_configuration_owner_locked', 'esp32_mquickjs_wifi_radio_set_country_details',
            'esp32_mquickjs_wifi_radio_set_country_code'))
        for target in ('esp32c3', 'esp32c5'):
            with self.subTest(target=target):
                compile_run(self, PRELUDE + gates(target) + types(target) + local +
                            NATIVE_BOUNDARY + helpers + NATIVE_RESET + main)

    def test_stopped_admission_readback_every_sdk_failure_and_rollback(self):
        self.native(DETAILS_MAIN)

    def test_live_code_identity_requested_readback_and_persistent_failure(self):
        self.native(CODE_MAIN)

    def vm(self, invalid):
        source = RADIO.read_text()
        options = (CORE / 'esp32_mquickjs_options.c').read_text().replace(
            '#include "esp32_mquickjs_options.h"',
            (COMPONENT / 'internal/esp32_mquickjs_options.h').read_text().replace(
                '#include "esp32_mquickjs_types.h"', ''))
        config = (COMPONENT / 'src/modules/wifi/esp32_mquickjs_wifi_config.c').read_text()
        wifi = (COMPONENT / 'src/modules/wifi/esp32_mquickjs_wifi.c').read_text()
        driver = (COMPONENT / 'src/modules/wifi_driver/esp32_mquickjs_wifi_driver.c').read_text()
        capture = ''.join(extract(config, name) for name in (
            'wifi_capture_configuration_country', 'esp32_mquickjs_wifi_capture_country_details'))
        adapter = extract(wifi, 'esp32_mquickjs_wifi_country_to_js')
        adapter += extract((CORE / 'esp32_mquickjs.c').read_text(), 'esp32_mquickjs_throw_native_error')
        adapter += extract(driver, 'driver_phy_write_error') + extract(driver, 'js_wifi_driver_set_country_details')
        for target in ('esp32c3', 'esp32c5'):
            with self.subTest(target=target), tempfile.TemporaryDirectory() as directory:
                body = PRELUDE + gates(target) + types(target) + VM_BOUNDARY + options
                body += validators(source) + capture + adapter
                binary = build(directory, body, VM_MAIN)
                value = '{code:"TW",policy:"manual",startChannel:1,channelCount:13,environment:"X"}'
                if invalid:
                    values = ('null', '[]', '"TW"', '{}', value.replace('startChannel:1,', ''),
                              value.replace('channelCount:13', 'channelCount:13.5'),
                              value.replace('startChannel:1', 'startChannel:14'),
                              value.replace('code:"TW"', 'code:"TWN"'),
                              value.replace('code:"TW"', 'code:"tw"'),
                              value.replace('code:"TW"', 'code:"T\\u0000"'),
                              value[:-1] + ',maxTxPowerDbm:20}', value[:-1] + ',unknown:1}',
                              value[:-1] + ',ghz5ChannelMask:4294967296}',
                              value.replace('manual', 'auto')[:-1] + ',ghz5ChannelMask:2}')
                    for bad in values:
                        run([str(binary), '(' + bad + ')', '0', '0', '1'])
                    for count in ('0', '2'):
                        run([str(binary), '(' + value + ')', '0', '0', count])
                else:
                    for native_error in ('0', '1'):
                        run([str(binary), '(' + value + ')', '1', native_error, '1'])
                    for environment in ('null', '"indoor"', '"outdoor"'):
                        run([str(binary), '(' + value.replace('"X"', environment) + ')', '1', '0', '1'])
                    with_mask = value[:-1] + ',ghz5ChannelMask:2}'
                    run([str(binary), '(' + with_mask + ')', str(int(target == 'esp32c5')), '0', '1'])

    def test_public_capture_gc_oom_and_post_mutation_result_delivery(self):
        self.vm(False)

    def test_public_full_validation_precedes_driver_call(self):
        self.vm(True)


VM_BOUNDARY = r'''
static int native_writes;static bool native_error;
static JSValue read_property(JSContext *ctx,JSValue obj,const char *key){
 JSGCRef ref;JSValue *root=JS_PushGCRef(ctx,&ref);*root=obj;
 JSValue v=step(ctx)?JS_ThrowOutOfMemory(ctx):JS_GetPropertyStr(ctx,*root,key);
 JS_PopGCRef(ctx,&ref);return v;
}
#define JS_GetPropertyStr read_property
static JSValue wifi_configuration_unsupported(JSContext *ctx){return JS_ThrowTypeError(ctx,"unsupported configuration");}
static int esp32_mquickjs_wifi_radio_set_country_details(const wifi_country_t *requested,
 wifi_country_t *actual,esp32_mquickjs_wifi_radio_config_result_t *result){
 assert(native_writes++==0);assert(requested->cc[0]=='T'&&requested->cc[1]=='W');
 assert(requested->schan==1&&requested->nchan==13&&requested->max_tx_power==0);
 *actual=*requested;actual->max_tx_power=17;
 *result=(esp32_mquickjs_wifi_radio_config_result_t){.stage="country-config",.error=native_error?-77:0,
  .mutation_attempted=true,.persistent_mutation_possible=true};return result->error;
}
'''

VM_MAIN = r'''
#undef JS_GetPropertyStr
int main(int argc,char **argv){
 assert(argc==5);bool expected=atoi(argv[2]);native_error=atoi(argv[3]);int count=atoi(argv[4]);
 int total=0;bool delivery_oom=false;
 for(int nth=0;nth<=total;nth++){
  void *heap=malloc(128*1024);JSContext *ctx=JS_NewContext(heap,128*1024,&js_stdlib);assert(ctx);
  JSGCRef input_ref,output_ref;JSValue *input=JS_PushGCRef(ctx,&input_ref);JSValue *output=JS_PushGCRef(ctx,&output_ref);
  *input=JS_Eval(ctx,argv[1],strlen(argv[1]),"country",JS_EVAL_RETVAL);assert(!JS_IsException(*input));
  native_writes=0;calls=0;fail_at=nth;collect=inject=true;
  *output=js_wifi_driver_set_country_details(ctx,NULL,count,input);
  inject=collect=false;
  if(!nth){total=calls;assert(native_writes==(int)expected);assert(JS_IsException(*output)==(!expected||native_error));}
  assert(native_writes<=1);
  if(JS_IsException(*output)){
   assert(JS_HasException(ctx));JS_GetException(ctx);
   if(nth&&native_writes&&!native_error)delivery_oom=true;
  }else{
   assert(expected&&!native_error&&native_writes==1);
   JSValue power=JS_GetPropertyStr(ctx,*output,"maxTxPowerDbm");int32_t value;
   assert(!JS_ToInt32(ctx,&value,power)&&value==17);
  }
  JS_PopGCRef(ctx,&output_ref);JS_PopGCRef(ctx,&input_ref);JS_GC(ctx);
  assert(!root_count&&!native_live);JS_FreeContext(ctx);free(heap);
 }
 if(expected&&!native_error)assert(delivery_oom);
}
'''
