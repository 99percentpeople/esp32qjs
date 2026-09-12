"""Deferred whole production AP result/native lifecycle; AST only this wave.

SDK enable/start/destructor and timer/driver boundaries are controlled here.
The lifecycle, command authorization, error retention and release are the
production includes, not a separate test state machine. Real worker/RTOS and
full SDK input scheduling remain part of the concentrated Wi-Fi tests.
"""
from pathlib import Path
import os
import sys
import unittest
from test_wireless_control_regression import compile_run
from test_idf_wps_ap_result import PRELUDE, POST, HEADER, SOURCE, declarations

ROOT = Path(__file__).resolve().parents[2]
NATIVE = ROOT / 'components/esp32_mquickjs/src/modules/wifi_wps/esp32_mquickjs_wifi_wps_ap_sdk.inc'
NATIVE_HEADER = ROOT / 'components/esp32_mquickjs/internal/esp32_mquickjs_wifi_wps_ap_sdk.h'
sys.path.insert(0, str(ROOT / 'scripts'))
from patch_idf_wps import function
from patch_idf_wps_registrar import patch_source


class WpsAPNative(unittest.TestCase):
    def run_case(self, main):
        compile_run(self, PRELUDE + TYPES + declarations(HEADER.read_text()) +
                    declarations(NATIVE_HEADER.read_text()) + declarations(SOURCE.read_text()) +
                    POST + BOUNDARIES + declarations(NATIVE.read_text()) + main)

    def test_ordinary_ap_and_station_disable_are_rejected_before_mutation(self):
        sdk = os.environ.get('IDF_PATH')
        if not sdk:
            self.skipTest('Set IDF_PATH to the reviewed ESP-IDF')
        relative = 'esp_supplicant/src/esp_hostpad_wps.c'
        source = patch_source(relative, (Path(sdk) / 'components/wpa_supplicant' / relative).read_bytes()).decode()
        station = (NATIVE.parent / 'esp32_mquickjs_wifi_wps_sdk.inc').read_text()
        self.run_case(r'''
#define CONFIG_WPS_REGISTRAR 1
#define ESP_ERR_WIFI_MODE 0x3006
#define WPS_OWNER_ENROLLEE 1
enum wps_owner{OWNER_UNUSED};
static void *s_wps_native;
static int wps_set_type(int type){(void)type;assert(false);return 0;}
static int wifi_wps_disable_internal(void *a,void *b){(void)a;(void)b;assert(false);return 0;}
''' + function(source, 'wifi_ap_wps_disable_internal') +
            function(station, 'esp32qjs_wps_unmanaged_disable') + r'''
int main(void){
 esp_wps_config_t cfg={WPS_TYPE_PBC};uint32_t id=0,revision;
 assert(esp32qjs_wps_ap_native_begin(&cfg,&id)==0);
 assert(wifi_ap_wps_disable_internal()==ESP_ERR_INVALID_STATE);
 assert(esp32qjs_wps_unmanaged_disable(NULL,NULL)==ESP_ERR_INVALID_STATE);
 assert(!s_wps_ap_result->status.closing && !deinits && !cleanup_calls[0]);
 assert(esp32qjs_wps_ap_native_stop(id)==0 && esp32qjs_wps_ap_native_retire(id)==0);
 assert(esp32qjs_wps_ap_native_checkpoint(id,&revision)==0);
 assert(esp32qjs_wps_ap_native_release(id,revision)==0 && !live);
 return 0;
}
''')

    def test_managed_commands_callback_depth_retirement_and_exact_release(self):
        self.run_case(r'''
int main(void){
 esp_wps_config_t cfg={WPS_TYPE_PBC};uint32_t id=0,revision=0;
 assert(esp32qjs_wps_ap_native_begin(&cfg,&id)==0 && id && enables==1);
 assert(!esp32qjs_wps_ap_command_allowed(0) && esp32qjs_wps_ap_command_allowed(id));
 assert(!esp32qjs_wps_ap_command_allowed(id+1));
 assert(esp32qjs_wps_ap_native_start(id+1)==ESP_ERR_INVALID_STATE && !starts);
 assert(esp32qjs_wps_ap_native_start(id)==0 && starts==1);
 assert(esp32qjs_wps_ap_native_start(id)==ESP_ERR_INVALID_STATE && starts==1);
 assert(esp32qjs_wps_ap_native_retire(id)==ESP_ERR_INVALID_STATE && !deinits);
 assert(esp32qjs_wps_ap_result_activity_enter(id)==&hapd);
 assert(esp32qjs_wps_ap_native_stop(id)==ESP_ERR_INVALID_STATE && !deinits);
 esp32qjs_wps_ap_result_callback_leave(id);
 assert(esp32qjs_wps_ap_native_stop(id)==0);
 assert(!esp32qjs_wps_ap_result_deinit_allowed(&hapd));
 deinit_error=ESP_ERR_NOT_FINISHED;
 assert(esp32qjs_wps_ap_native_retire(id)==ESP_ERR_NOT_FINISHED && live==1);
 assert(!esp32qjs_wps_ap_result_deinit_allowed(&hapd) && !factory_calls);
 deinit_error=0;assert(esp32qjs_wps_ap_native_retire(id)==0 && deinits==2);
 assert(live==1 && !s_wps_ap_result->status.sdk_attached && factory_calls==1);
 assert(esp32qjs_wps_ap_native_checkpoint(id,&revision)==0);
 assert(!esp32qjs_wps_ap_result_activity_enter(id)); /* Rejected late activity. */
 assert(esp32qjs_wps_ap_native_release(id,revision)==ESP_ERR_NOT_FINISHED && live==1);
 assert(esp32qjs_wps_ap_native_checkpoint(id,&revision)==0);
 assert(esp32qjs_wps_ap_native_release(id,revision)==0 && !live);
 uint32_t next=0;assert(esp32qjs_wps_ap_native_begin(&cfg,&next)==0 && next>id);
 assert(!esp32qjs_wps_ap_command_allowed(id));
 assert(esp32qjs_wps_ap_native_stop(id)==ESP_ERR_INVALID_STATE);
 assert(!s_wps_ap_result->status.closing);
 assert(esp32qjs_wps_ap_native_stop(next)==0 && esp32qjs_wps_ap_native_retire(next)==0);
 assert(esp32qjs_wps_ap_native_checkpoint(next,&revision)==0);
 assert(esp32qjs_wps_ap_native_release(next,revision)==0 && !live && !posts);
 return 0;
}
''')

    def test_enable_failure_before_bind_and_factory_cleanup_suffix(self):
        self.run_case(r'''
int main(void){
 esp_wps_config_t cfg={WPS_TYPE_PBC};uint32_t id=0,revision;
 enable_error=-81;
 assert(esp32qjs_wps_ap_native_begin(&cfg,&id)==-81 && id && live==1);
 assert(!s_wps_ap_result->ever_bound && s_wps_ap_result->status.error==-81);
 assert(esp32qjs_wps_ap_native_stop(id)==0);
 factory_error=-82;assert(esp32qjs_wps_ap_native_retire(id)==-82 && !deinits);
 assert(s_wps_ap_result->status.error==-81 && s_wps_ap_result->status.cleanup_error==-82);
 factory_error=0;assert(esp32qjs_wps_ap_native_retire(id)==0 && !deinits);
 assert(esp32qjs_wps_ap_native_checkpoint(id,&revision)==0);
 assert(esp32qjs_wps_ap_native_release(id,revision)==0 && !live);
 enable_error=0;id=0;assert(esp32qjs_wps_ap_native_begin(&cfg,&id)==0);
 assert(esp32qjs_wps_ap_native_stop(id)==0);
 factory_error=-83;assert(esp32qjs_wps_ap_native_retire(id)==-83 && deinits==1);
 assert(!s_wps_ap_result->status.sdk_attached && s_wps_ap_result->heap_retired);
 factory_error=0;assert(esp32qjs_wps_ap_native_retire(id)==0 && deinits==1);
 assert(esp32qjs_wps_ap_native_checkpoint(id,&revision)==0);
 assert(esp32qjs_wps_ap_native_release(id,revision)==0 && !live);
 return 0;
}
''')

    def test_pin_commit_and_terminal_metadata_survive_heap_retirement(self):
        self.run_case(r'''
int main(void){
 esp_wps_config_t cfg={WPS_TYPE_PIN};uint32_t id=0,revision;uint8_t pin[8]={1},copy[8];
 assert(esp32qjs_wps_ap_native_begin(&cfg,&id)==0);
 assert(esp32qjs_wps_ap_result_event(&hapd,WIFI_EVENT_AP_WPS_RG_PIN,pin,8)==0);
 assert(esp32qjs_wps_ap_result_pin_copy(id,copy)==0 && !memcmp(pin,copy,8));
 assert(esp32qjs_wps_ap_result_pin_commit(id)==0 && !s_wps_ap_result->status.pin_available);
 assert(esp32qjs_wps_ap_native_start(id)==0);
 wifi_event_ap_wps_rg_success_t event={{2,0,0,0,0,1}};
 assert(esp32qjs_wps_ap_result_event(&hapd,WIFI_EVENT_AP_WPS_RG_SUCCESS,&event,sizeof(event))==0);
 assert(esp32qjs_wps_ap_native_stop(id)==0 && esp32qjs_wps_ap_native_retire(id)==0);
 assert(s_wps_ap_result->status.terminal && s_wps_ap_result->status.error==0);
 assert(!memcmp(s_wps_ap_result->status.peer,event.peer_macaddr,6) && !posts);
 assert(esp32qjs_wps_ap_native_checkpoint(id,&revision)==0);
 assert(esp32qjs_wps_ap_native_release(id,revision)==0 && !live);
 return 0;
}
''')


TYPES = r'''
#define ESP_ERR_NOT_FINISHED 0x10c
#define WPS_TYPE_DISABLE 0
#define WPS_TYPE_PBC 1
#define WPS_TYPE_PIN 2
#define WPS_STATUS_DISABLE 0
#define WPS_OWNER_REGISTRAR 2
typedef struct {int wps_type;} esp_wps_config_t;
struct hostapd_data{int value;};
static struct hostapd_data hapd;
static int enables,starts,deinits,factory_calls,enable_error,deinit_error,factory_error;
static int wps_get_type(void){return WPS_TYPE_DISABLE;}
static int wps_get_status(void){return WPS_STATUS_DISABLE;}
static bool is_dpp_enabled(void){return false;}
static void wps_set_owner(int owner){native_owner=owner;}
static struct hostapd_data *hostapd_get_hapd_data(void){return &hapd;}
'''

BOUNDARIES = r'''
static int wifi_ap_wps_enable_internal(const esp_wps_config_t *config,uint32_t id){
 assert(config && esp32qjs_wps_ap_command_allowed(id));enables++;
 if(enable_error)return enable_error;
 int error=esp32qjs_wps_ap_result_bind(&hapd);if(error)return error;
 gWpsSm=&hapd;native_owner=WPS_OWNER_REGISTRAR;return ESP_OK;
}
static int wifi_ap_wps_start_internal(const unsigned char *pin,uint32_t id){
 assert(!pin && esp32qjs_wps_ap_command_allowed(id));starts++;return ESP_OK;
}
static int wifi_ap_wps_deinit(void){
 assert(esp32qjs_wps_ap_result_deinit_allowed(&hapd));deinits++;
 if(deinit_error)return deinit_error;
 gWpsSm=NULL;esp32qjs_wps_ap_result_detach(&hapd,ESP_OK);return ESP_OK;
}
esp_err_t esp32qjs_wps_ap_factory_release(uint32_t id){
 assert(esp32qjs_wps_ap_result_exact(id) && !gWpsSm && native_owner==WPS_OWNER_NONE);
 factory_calls++;return factory_error;
}
'''
