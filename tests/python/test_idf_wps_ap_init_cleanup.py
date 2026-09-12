"""Deferred production AP initialization failure cleanup; AST only."""
from pathlib import Path
import sys
import unittest
from test_idf_wps_ap_result import WpsAPResult
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts'))
from patch_idf_wps_ap_init_cleanup import FAILURE


class WpsAPInitCleanup(unittest.TestCase):
    run_case=WpsAPResult.run_case

    def test_every_failed_cleanup_stage_retains_result_and_retries_only_suffix(self):
        self.run_case(BOUNDARIES+FAILURE+r'''
int main(void){
 struct hostapd_data h={0};
 for(int stage=0;stage<ESP32QJS_WPS_AP_CLOSE_PREPARED;stage++){
  memset(cleanup_calls,0,sizeof(cleanup_calls));releases=0;fail_stage=stage;
  assert(esp32qjs_wps_ap_result_bind(&h)==0);
  uint32_t identity=esp32qjs_wps_ap_result_identity(&h);
  post_error=ESP_ERR_TIMEOUT; /* Observers cannot block failure retention. */
  esp32qjs_wps_ap_init_failed(&h,-81);
  assert(!releases && live==1 && s_wps_ap_result->status.sdk_attached);
  assert(s_wps_ap_result->status.error==-81 && s_wps_ap_result->status.terminal);
  assert(s_wps_ap_result->status.closing && s_wps_ap_result->status.cleanup_error==cleanup_failure);
  assert(!esp32qjs_wps_ap_result_can_bind());
  assert(!esp32qjs_wps_ap_result_context(identity));
  fail_stage=-1;esp32qjs_wps_ap_init_failed(&h,-99);
  assert(releases==1 && !live);
  for(int i=0;i<ESP32QJS_WPS_AP_CLOSE_PREPARED;i++)assert(cleanup_calls[i]==(unsigned)(i==stage?2:1));
 }
 return 0;
}
''')

    def test_managed_initializer_error_keeps_result_and_cleanup_obligation(self):
        self.run_case(BOUNDARIES+FAILURE+r'''
int main(void){
 struct hostapd_data h={0};uint32_t identity=0;
 assert(esp32qjs_wps_ap_result_reserve(&identity)==0);
 assert(esp32qjs_wps_ap_result_bind(&h)==0);
 esp32qjs_wps_ap_init_failed(&h,-74);
 assert(!posts && !releases && live==1 && s_wps_ap_result->status.sdk_attached);
 assert(s_wps_ap_result->status.close_prepared && s_wps_ap_result->status.error==-74);
 esp32qjs_wps_ap_result_cleanup_error(&h,-95);
 assert(s_wps_ap_result->status.error==-74 && s_wps_ap_result->status.cleanup_error==-95);
 int foreign=0;esp32qjs_wps_ap_result_cleanup_error(&foreign,-96);
 assert(s_wps_ap_result->status.cleanup_error==-95);
 /* Fixture teardown only; managed native release remains a separate task. */
 esp32qjs_wps_ap_result_free();return 0;
}
''')

BOUNDARIES=r'''
#define ETH_ALEN 6
struct hostapd_data{int unused;};
static int releases;
static int wifi_ap_wps_deinit(void){
 assert(s_wps_ap_result && s_wps_ap_result->status.close_prepared);
 assert(s_wps_ap_result->status.error==-81 && !s_wps_ap_result->status.retained);
 releases++;
 esp32qjs_wps_ap_result_detach(s_wps_ap_result->context,ESP_OK);
 return ESP_OK;
}
'''
