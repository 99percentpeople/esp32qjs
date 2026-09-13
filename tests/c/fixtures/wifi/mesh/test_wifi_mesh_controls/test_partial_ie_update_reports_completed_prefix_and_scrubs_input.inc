
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
