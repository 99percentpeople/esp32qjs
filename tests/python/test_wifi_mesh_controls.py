"""Deferred control/secret/role regressions against the production Mesh SDK.

Only AST-checked during implementation; execute with the consolidated Wi-Fi
suite. The fixture injects SDK calls, never a replacement control state machine.
"""
import unittest
from test_wifi_mesh_sdk import source
from test_wifi_mesh_session import source as session_source
from test_wireless_control_regression import compile_run


class WiFiMeshControls(unittest.TestCase):
    def test_configuration_secrets_are_opt_in_and_layer_getter_requires_parent(self):
        compile_run(self, source() + r'''
int main(void){
 esp32_mquickjs_wifi_mesh_config_t c=config();memcpy(c.network.router.password,"password",8);
 assert(!esp32_mquickjs_wifi_mesh_sdk_reserve(4,&c,&active));
 assert(!esp32_mquickjs_wifi_mesh_sdk_start(&active));
 assert(!esp32_mquickjs_wifi_mesh_sdk_inspect(&active)&&!layer_reads);
 esp32_mquickjs_wifi_mesh_control_data_t d={0};
 esp32_mquickjs_wifi_mesh_control_t command={.kind=ESP32_MQUICKJS_MESH_CONFIGURATION,.detail=&d};
 assert(!esp32_mquickjs_wifi_mesh_sdk_control(&active,&command));
 assert(!d.data.configuration.config.network.crypto_funcs);
 assert(all_zero(d.data.configuration.config.network.router.password,64));
 assert(all_zero(d.data.configuration.config.ie_key,64));
 assert(d.data.configuration.config.ie_key_length==8);
 d.include_secrets=true;assert(!esp32_mquickjs_wifi_mesh_sdk_control(&active,&command));
 assert(!memcmp(d.data.configuration.config.network.router.password,"password",8));
 assert(!memcmp(d.data.configuration.config.ie_key,"abcdefgh",8));
 fail_native=native_calls+2;assert(esp32_mquickjs_wifi_mesh_sdk_control(&active,&command)==-77);
 assert(all_zero(d.data.configuration.config.network.router.password,64));
 assert(all_zero(d.data.configuration.config.ie_key,64));
 assert(!esp32_mquickjs_wifi_mesh_sdk_close(&active));
 assert(!esp32_mquickjs_wifi_mesh_sdk_release(&active)&&!live_allocations);
}
''')

    def test_partial_ie_update_reports_completed_prefix_and_scrubs_input(self):
        compile_run(self, source() + r'''
int main(void){
 esp32_mquickjs_wifi_mesh_config_t c=config();
 assert(!esp32_mquickjs_wifi_mesh_sdk_reserve(4,&c,&active));
 assert(!esp32_mquickjs_wifi_mesh_sdk_start(&active));
 esp32_mquickjs_wifi_mesh_control_data_t d={.enabled=true,.data.encryption={.length=8}};
 memcpy(d.data.encryption.key,"new-key!",8);
 esp32_mquickjs_wifi_mesh_control_t command={.kind=ESP32_MQUICKJS_MESH_IE_ENCRYPTION,.detail=&d};
 fail_native=native_calls+2;assert(esp32_mquickjs_wifi_mesh_sdk_control(&active,&command)==-77);
 assert(d.completed_steps==1&&!strcmp(d.stage,"mesh-ie-encryption"));
 assert(all_zero(&d.data,sizeof(d.data))&&!memcmp(native_key,"new-key!",8));
 assert(!s_mesh->status.busy&&s_mesh->ie_key_length==8);
 assert(!esp32_mquickjs_wifi_mesh_sdk_close(&active));
 assert(!esp32_mquickjs_wifi_mesh_sdk_release(&active)&&!live_allocations);
}
''')

    def test_native_prevalidation_and_role_denial_do_not_submit_mutations(self):
        compile_run(self, source() + r'''
int main(void){
 esp32_mquickjs_wifi_mesh_config_t c=config();
 assert(!esp32_mquickjs_wifi_mesh_sdk_reserve(4,&c,&active));
 assert(!esp32_mquickjs_wifi_mesh_sdk_start(&active));unsigned previous=native_calls;
 esp32_mquickjs_wifi_mesh_control_data_t d={.data.vote={.percentage=0.9f,.config.attempts=15}};
 esp32_mquickjs_wifi_mesh_control_t command={.kind=ESP32_MQUICKJS_MESH_WAIVE_ROOT,.detail=&d};
 force_nonroot=true;assert(esp32_mquickjs_wifi_mesh_sdk_control(&active,&command)==ESP_ERR_INVALID_STATE);
 assert(native_calls==previous&&!d.completed_steps&&!s_mesh->status.busy);
 d.data.vote.is_rc_specified=true;
 assert(esp32_mquickjs_wifi_mesh_sdk_control(&active,&command)==ESP_ERR_INVALID_ARG&&native_calls==previous);
 command.kind=ESP32_MQUICKJS_MESH_NETWORK_DUTY;memset(&d,0,sizeof(d));d.number=30;d.duration=-1;
 assert(esp32_mquickjs_wifi_mesh_sdk_control(&active,&command)==ESP_ERR_INVALID_STATE&&native_calls==previous);
 d.duration=1;d.rule=1;
 assert(esp32_mquickjs_wifi_mesh_sdk_control(&active,&command)==ESP_ERR_INVALID_ARG&&native_calls==previous);
 d.rule=0;assert(!esp32_mquickjs_wifi_mesh_sdk_control(&active,&command)&&native_calls==previous+1);
 assert(!esp32_mquickjs_wifi_mesh_sdk_close(&active));
 assert(!esp32_mquickjs_wifi_mesh_sdk_release(&active)&&!live_allocations);
}
''')

    def test_native_truncation_and_false_success_are_not_reported_as_applied(self):
        compile_run(self, source() + r'''
int main(void){
 esp32_mquickjs_wifi_mesh_config_t c=config();
 assert(!esp32_mquickjs_wifi_mesh_sdk_reserve(4,&c,&active));
 assert(!esp32_mquickjs_wifi_mesh_sdk_start(&active));unsigned previous=native_calls;
 esp32_mquickjs_wifi_mesh_control_data_t d={.number=255};
 esp32_mquickjs_wifi_mesh_control_t command={.kind=ESP32_MQUICKJS_MESH_SIGNAL_DUTY,.detail=&d};
 assert(esp32_mquickjs_wifi_mesh_sdk_control(&active,&command)==ESP_ERR_INVALID_ARG&&native_calls==previous);
 d.number=254;assert(!esp32_mquickjs_wifi_mesh_sdk_control(&active,&command));
 command.kind=ESP32_MQUICKJS_MESH_ASSOC_EXPIRY;d.number=60;ignore_expiry=true;
 assert(esp32_mquickjs_wifi_mesh_sdk_control(&active,&command)==ESP_ERR_INVALID_RESPONSE);
 assert(d.completed_steps==1&&!strcmp(d.stage,"mesh-association-expiry-readback"));
 ignore_expiry=false;assert(!esp32_mquickjs_wifi_mesh_sdk_control(&active,&command));
 assert(native_expiry==60&&!d.stage);
 assert(!esp32_mquickjs_wifi_mesh_sdk_close(&active));
 assert(!esp32_mquickjs_wifi_mesh_sdk_release(&active)&&!live_allocations);
}
''')

    def test_extended_job_owns_aligned_copy_and_retains_parent_after_capture(self):
        compile_run(self, session_source() + r'''
int main(void){
 test_session=open_session();
 esp32_mquickjs_wifi_mesh_control_data_t d={.data.router={.ssid_len=2,.ssid={'a','p'},.password={'s','e','c','r','e','t','1','2'}}};
 esp32_mquickjs_wifi_mesh_control_t command={.kind=ESP32_MQUICKJS_MESH_SET_ROUTER,.detail=&d};
 esp32_mquickjs_wifi_mesh_job_t*j=NULL;
 assert(!esp32_mquickjs_wifi_mesh_job_create(test_session,NULL,&command,1000,&j));
 memset(&d,0,sizeof(d));
 assert((uintptr_t)j->control.detail%_Alignof(esp32_mquickjs_wifi_mesh_control_data_t)==0);
 assert(!memcmp(j->control.detail->data.router.password,"secret12",8));
 assert(!esp32_mquickjs_wifi_mesh_job_detail(j));
 esp32_mquickjs_wifi_mesh_session_close(test_session,false);tick();
 assert(test_session->status.retired);
 esp32_mquickjs_wifi_mesh_session_release(test_session);assert(s_mesh_handles==1);
 esp32_mquickjs_wifi_mesh_job_release(j);assert(!s_mesh_handles&&!s_mesh_jobs&&!live_allocations);
}
''')
