"""Deferred production restore admission/error and Driver VM conversion cases.

Only SDK calls and native snapshot providers are injected. These cases do not
prove the binary SDK loader, flash durability or RF behavior.
"""
import re
import tempfile
import unittest

from test_wifi_config_controls import PRELUDE, sdk_types, structure
from test_wireless_control_regression import compile_run
from wireless_vm_fixture import ROOT, build, extract, run

COMPONENT = ROOT / 'components/esp32_mquickjs'
RADIO = COMPONENT / 'src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c'
HEADER = COMPONENT / 'internal/esp32_mquickjs_wifi_radio.h'
WIFI = COMPONENT / 'src/modules/wifi/esp32_mquickjs_wifi.c'
CAPABILITIES = COMPONENT / 'src/modules/wifi/esp32_mquickjs_wifi_capabilities.c'

NATIVE = r'''
#define ESP_ERR_WIFI_NOT_INIT -7
#define RADIO_EVENTS_IDLE 0
#define WIFI_SECOND_CHAN_NONE 0
#if CONFIG_ESP_WIFI_SOFTAP_SUPPORT
#define EXPECTED_MODE WIFI_MODE_APSTA
#else
#define EXPECTED_MODE WIFI_MODE_STA
#endif
static struct {
 int lock,driver_state,fault_error,cleanup_error,channel_observation_error;
 bool driver_owned,storage_configured,started,stop_required,stop_submitted,restart_required,promiscuous_claimed;
 unsigned generation,channel_generation,event_phase,event_live,wake_locks,primary_channel,secondary_channel;
 wifi_storage_t storage;wifi_mode_t effective_mode;
 const char *fault_stage,*cleanup_stage;
 struct {unsigned identity;} lifecycle,operation,leases[WIFI_RADIO_MAX_LEASES];
 esp32_mquickjs_wifi_radio_config_result_t configuration;
} s_radio;
static struct {unsigned identity;bool restore_pending;} s_tx_rate_lease;
static struct {struct {unsigned identity;} owner;bool restore_pending;} s_interval;
static struct {struct {unsigned identity;} start_owner;} s_vendor_ie;
static struct {void *snapshot;} s_config_restart;
static struct {struct {unsigned identity;} owner;} s_policy_restart;
static unsigned s_policies,s_tx_rates,s_inactive_history,s_scan_parameters;
static unsigned pending;
#define WIFI_RADIO_SMARTCONFIG_PENDING (pending&1)
#define WIFI_RADIO_WPS_PENDING (pending&2)
#define WIFI_RADIO_DPP_PENDING (pending&4)
#define WIFI_RADIO_EAP_PENDING (pending&8)
#define WIFI_RADIO_NAN_PENDING (pending&16)
#define WIFI_RADIO_MESH_PENDING (pending&32)
static int depth,critical,calls,fail_at,invalidated;
static bool unchanged;static wifi_mode_t native_mode;
static void wifi_radio_operation_lock(void){assert(!depth&&!critical);depth=1;}
static void wifi_radio_operation_unlock(void){assert(depth&&!critical);depth=0;}
#define taskENTER_CRITICAL(p) do{(void)(p);assert(!critical);critical=1;}while(0)
#define taskEXIT_CRITICAL(p) do{(void)(p);assert(critical);critical=0;}while(0)
static int step(void){assert(depth&&!critical);return ++calls==fail_at?-77:ESP_OK;}
static int native_restore(void){return step();}
static void wifi_radio_invalidate_stop_snapshot_locked(void){assert(depth&&!critical);unchanged=false;}
static int esp_wifi_get_mode(wifi_mode_t *mode){int e=step();*mode=native_mode;return e;}
static int esp_wifi_set_storage(wifi_storage_t storage){assert(storage==s_radio.storage);return step();}
static bool esp32_mquickjs_wifi_interval_invalidate(void *state,unsigned generation){assert(state==&s_interval&&generation==9);invalidated++;return true;}
static void esp32_mquickjs_wifi_policy_invalidate(void *state){assert(state==&s_policies);invalidated++;}
static void esp32_mquickjs_wifi_tx_rate_invalidate(void *state){assert(state==&s_tx_rates);invalidated++;}
#define esp_wifi_restore() (wifi_radio_invalidate_stop_snapshot_locked(),native_restore())
static void reset(void){
 assert(!depth&&!critical);memset(&s_radio,0,sizeof(s_radio));
 memset(&s_tx_rate_lease,0,sizeof(s_tx_rate_lease));memset(&s_interval,0,sizeof(s_interval));
 memset(&s_vendor_ie,0,sizeof(s_vendor_ie));memset(&s_config_restart,0,sizeof(s_config_restart));
 memset(&s_policy_restart,0,sizeof(s_policy_restart));pending=calls=fail_at=invalidated=0;unchanged=true;
 s_radio.driver_owned=s_radio.storage_configured=true;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STOPPED;
 s_radio.generation=9;s_radio.storage=WIFI_STORAGE_FLASH;s_radio.effective_mode=WIFI_MODE_STA;
 native_mode=EXPECTED_MODE;s_inactive_history=99;s_radio.primary_channel=6;
}
'''

NATIVE_MAIN = r'''
int main(void){
 esp32_mquickjs_wifi_radio_config_result_t r;
 reset();assert(esp32_mquickjs_wifi_radio_restore(&r)==0&&calls==3);
 assert(r.mutation_attempted&&r.persistent_mutation_possible&&!r.rollback_attempted);
 assert(!strcmp(r.stage,"restore-sdk-accepted")&&s_radio.generation==9&&s_radio.driver_owned);
 assert(s_radio.effective_mode==EXPECTED_MODE&&!s_radio.started&&s_radio.storage==WIFI_STORAGE_FLASH);
 assert(invalidated==3&&!unchanged&&!s_inactive_history&&!s_radio.primary_channel);
 for(int phase=1;phase<=3;phase++){
  reset();fail_at=phase;assert(esp32_mquickjs_wifi_radio_restore(&r)==-77);
  assert(calls==phase&&invalidated==3&&!unchanged&&!r.rollback_attempted);
  assert(s_radio.fault_error==-77&&r.error==-77&&s_radio.configuration.error==-77);
  assert(phase!=3||!s_radio.storage_configured);
  const char *first=s_radio.fault_stage;int previous=calls;unsigned channel_generation=s_radio.channel_generation;
  assert(s_radio.driver_state==ESP32_MQUICKJS_WIFI_RADIO_CLEANUP_PENDING);
  /* A rejected call may replace configuration, but cannot lose native progress. */
  s_radio.leases[0].identity=5;
  assert(esp32_mquickjs_wifi_radio_restore(&r)==ESP_ERR_INVALID_STATE&&calls==previous);
  assert(s_radio.fault_stage==first&&s_radio.cleanup_stage==first);
  s_radio.leases[0].identity=0;
  /* Repeated SDK failure keeps the first error and retries only the suffix. */
  fail_at=calls+1;assert(esp32_mquickjs_wifi_radio_restore(&r)==-77&&calls==previous+1);
  assert(s_radio.fault_stage==first&&s_radio.fault_error==-77);
  assert(r.mutation_attempted==(phase!=2)&&r.persistent_mutation_possible==(phase==1));
  previous=calls;fail_at=0;assert(esp32_mquickjs_wifi_radio_restore(&r)==ESP_OK&&calls==previous+4-phase);
  assert(!s_radio.fault_stage&&!s_radio.cleanup_stage&&!s_radio.fault_error&&!s_radio.cleanup_error);
  assert(s_radio.driver_state==ESP32_MQUICKJS_WIFI_RADIO_STOPPED&&s_radio.storage_configured);
  assert(s_radio.generation==9&&s_radio.effective_mode==EXPECTED_MODE);
  assert(invalidated==(phase==1?9:3));
  assert(s_radio.channel_generation==channel_generation+(phase==1?2:0));
 }
 const char *allowed[]={"mode-snapshot","mode-write","mode-readback","station-phy-config",
  "ap-phy-config","station-phy-readback","ap-phy-readback"};
 for(unsigned origin=0;origin<sizeof(allowed)/sizeof(*allowed);++origin){
  for(int rollback=0;rollback<2;++rollback){
   reset();s_radio.fault_stage=allowed[origin];s_radio.fault_error=-88;
   s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_FAULTED;
   if(rollback){s_radio.cleanup_stage=origin<3?"mode-rollback":"phy-rollback";
    s_radio.cleanup_error=-99;s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_CLEANUP_PENDING;}
   fail_at=2;assert(esp32_mquickjs_wifi_radio_restore(&r)==-77&&calls==2);
   assert(s_radio.fault_stage==allowed[origin]&&s_radio.fault_error==-88&&s_radio.cleanup_error==-77);
   assert(!strcmp(s_radio.cleanup_stage,"restore-mode-readback"));
   fail_at=0;assert(esp32_mquickjs_wifi_radio_restore(&r)==ESP_OK&&calls==4&&invalidated==3);
   assert(!s_radio.fault_stage&&!s_radio.cleanup_stage&&s_radio.driver_state==ESP32_MQUICKJS_WIFI_RADIO_STOPPED);
  }
 }
 const char *denied[]={"antenna-device-restart-required","configuration-rollback","country-config",
  "power-save-readback","event-mask-write","band-write","he-statistics-readback","init"};
 for(unsigned origin=0;origin<sizeof(denied)/sizeof(*denied);++origin){
  reset();s_radio.fault_stage=denied[origin];s_radio.cleanup_stage="restore-storage";
  s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_CLEANUP_PENDING;
  assert(esp32_mquickjs_wifi_radio_restore(&r)==ESP_ERR_INVALID_STATE&&!calls);
 }
 reset();s_radio.fault_stage="mode-write";s_radio.cleanup_stage="configuration-rollback";
 s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_CLEANUP_PENDING;
 assert(esp32_mquickjs_wifi_radio_restore(&r)==ESP_ERR_INVALID_STATE&&!calls);
 reset();s_radio.storage=WIFI_STORAGE_RAM;
 assert(esp32_mquickjs_wifi_radio_restore(&r)==ESP_OK&&calls==3&&r.persistent_mutation_possible);
 reset();native_mode=(wifi_mode_t)99;assert(esp32_mquickjs_wifi_radio_restore(&r)==ESP_ERR_INVALID_RESPONSE);
 assert(calls==2&&s_radio.effective_mode==WIFI_MODE_STA);
#if !CONFIG_ESP_WIFI_SOFTAP_SUPPORT
 reset();native_mode=WIFI_MODE_AP;
 assert(esp32_mquickjs_wifi_radio_restore(&r)==ESP_ERR_NOT_SUPPORTED&&calls==2);
#endif
 for(unsigned bit=1;bit<=32;bit<<=1){reset();pending=bit;assert(esp32_mquickjs_wifi_radio_restore(&r)==ESP_ERR_INVALID_STATE&&calls==0&&unchanged);}
 for(int recovery=0;recovery<2;++recovery){for(int gate=0;gate<24;gate++){
  reset();if(recovery){s_radio.fault_stage="mode-write";s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_FAULTED;}
  switch(gate){
   case 0:s_radio.started=true;break;case 1:s_radio.stop_required=true;break;
   case 2:s_radio.stop_submitted=true;break;case 3:s_radio.leases[1].identity=99;break;
   case 4:s_radio.operation.identity=3;break;case 5:s_radio.lifecycle.identity=4;break;
   case 6:s_radio.event_phase=1;break;case 7:s_radio.event_live=1;break;
   case 8:s_radio.wake_locks=1;break;case 9:s_radio.promiscuous_claimed=true;break;
   case 10:s_tx_rate_lease.restore_pending=true;break;case 11:s_interval.restore_pending=true;break;
   case 12:s_config_restart.snapshot=&r;break;case 13:s_policy_restart.owner.identity=2;break;
   case 14:s_vendor_ie.start_owner.identity=1;break;case 15:s_radio.fault_stage="prior";break;
   case 16:s_radio.cleanup_stage="retiring";break;case 17:s_radio.restart_required=true;break;
   case 18:s_tx_rate_lease.identity=5;break;case 19:s_interval.owner.identity=6;break;
   case 20:s_radio.storage_configured=false;break;case 21:s_radio.storage=(wifi_storage_t)99;break;
   case 22:s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_UNINITIALIZED;break;
   case 23:s_radio.driver_state=ESP32_MQUICKJS_WIFI_RADIO_STARTED;break;
  }
  assert(esp32_mquickjs_wifi_radio_restore(&r)==ESP_ERR_INVALID_STATE&&calls==0&&!r.mutation_attempted&&unchanged);
 }}
 reset();s_radio.driver_owned=false;assert(esp32_mquickjs_wifi_radio_restore(&r)==ESP_ERR_WIFI_NOT_INIT&&calls==0);
 assert(esp32_mquickjs_wifi_radio_restore(NULL)==ESP_ERR_INVALID_ARG&&!depth&&!critical);
 return 0;
}
'''

VM_MAIN = r'''
int main(int argc,char **argv){
 (void)argc;collect=atoi(argv[1]);int total=1;
 for(int nth=0;nth<=total;nth++){
  void *heap=malloc(256*1024);JSContext *ctx=JS_NewContext(heap,256*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
  JSGCRef root_ref;JSValue *root=JS_PushGCRef(ctx,&root_ref);
  calls=0;fail_at=nth;moved_roots=0;inject=true;
  *root=js_wifi_driver_capabilities(ctx,NULL,0,NULL);inject=false;if(!nth)total=calls;
  if(nth){assert(JS_IsException(*root)&&JS_HasException(ctx));(void)JS_GetException(ctx);}
  else{
   assert(!JS_IsException(*root)&&!JS_HasException(ctx));if(collect)assert(moved_roots>0);
   assert(JS_GetPropertyStr(ctx,*root,"secretReadback")==JS_NewBool(SECRET_READ));
   JSValue operations=JS_GetPropertyStr(ctx,*root,"operations");int32_t n;
   assert(!JS_ToInt32(ctx,&n,JS_GetPropertyStr(ctx,operations,"length"))&&n==sizeof(wifi_driver_operations)/sizeof(wifi_driver_operations[0]));
   bool scan_get_found=false,restore_found=false;
   for(int i=0;i<n;++i){JSValue op=JS_GetPropertyUint32(ctx,operations,i);JSCStringBuf pathbuf;
    const char *path=JS_ToCString(ctx,JS_GetPropertyStr(ctx,op,"jsPath"),&pathbuf);
    if(!strcmp(path,"wifi.driver.restore")){restore_found=true;assert(JS_GetPropertyStr(ctx,op,"available")==JS_TRUE);}
    if(!strcmp(path,"wifi.driver.getScanParameters")){scan_get_found=true;assert(JS_GetPropertyStr(ctx,op,"available")==JS_TRUE);}}
   assert(scan_get_found && restore_found);
  }
  JS_PopGCRef(ctx,&root_ref);JS_GC(ctx);assert(!root_count&&!native_live);JS_FreeContext(ctx);free(heap);
 }
 return 0;
}
'''


class WifiDriverDiscoveryRestore(unittest.TestCase):
    def test_production_restore_admission_and_sdk_failure(self):
        radio, header = RADIO.read_text(), HEADER.read_text()
        declarations = '#undef ESP32_MQUICKJS_WIFI_RADIO_STOPPED\n'
        declarations += re.search(r'typedef enum \{[^}]*\} esp32_mquickjs_wifi_radio_driver_state_t;', header).group(0)
        declarations += re.search(r'typedef enum \{[^}]*\} wifi_radio_restore_phase_t;', radio).group(0)
        declarations += structure(header, 'esp32_mquickjs_wifi_radio_config_result_t')
        functions = ''.join(extract(radio, name) for name in (
            'wifi_radio_record_fault', 'wifi_radio_cleanup_fault',
            'wifi_radio_restore_phase_locked', 'esp32_mquickjs_wifi_radio_restore'))
        for target in ('esp32c3/representative', 'esp32s3/representative-psram', 'esp32c5/representative'):
            for softap in (0, 1):
                with self.subTest(target=target, softap=softap):
                    gates = f'\n#define CONFIG_ESP_WIFI_SOFTAP_SUPPORT {softap}\n'
                    compile_run(self, PRELUDE + gates + sdk_types(target) + declarations + NATIVE + functions + NATIVE_MAIN)

    def test_production_capabilities_gc_and_nth_allocation(self):
        source = CAPABILITIES.read_text()
        table = re.search(r'static const struct \{[^}]*\}\s*wifi_driver_operations\[\] = \{.*?\n\};', source, re.S).group(0)
        constants = '\n#define CONFIG_IDF_TARGET "esp32c5"\n#define ESP32_MQUICKJS_WIFI_AP_BEACON_QUANTUM_TU 100\n#define ESP32_MQUICKJS_WIFI_AP_BEACON_MAX_TU 60000\n#define ESP32_MQUICKJS_WIFI_AP_DTIM_MAX 255\n'
        constants += 'static const char *esp_get_idf_version(void){return "fixture-idf";}\n'
        bodies = table + '\n' + '\n'.join(extract(source, name) for name in (
            'wifi_capability_strings', 'wifi_station_capabilities', 'wifi_ap_capabilities',
            'wifi_driver_capability_list', 'js_wifi_driver_capabilities'))
        keys = (COMPONENT / 'internal/esp32_mquickjs_wifi_config_fields.h').read_text()
        for enabled in (0, 1):
            with tempfile.TemporaryDirectory() as directory:
                gates = f'\n#define SECRET_READ {enabled}\n#define CONFIG_ESP32_MQUICKJS_WIFI_ALLOW_SECRET_READBACK {enabled}\n#define CONFIG_ESP_WIFI_SOFTAP_SUPPORT {enabled}\n#define CONFIG_ESP_COEX_POWER_MANAGEMENT {enabled}\n'
                binary = build(directory, constants + gates + keys + bodies, VM_MAIN)
                for gc in (0, 1):
                    run([str(binary), str(gc)])
