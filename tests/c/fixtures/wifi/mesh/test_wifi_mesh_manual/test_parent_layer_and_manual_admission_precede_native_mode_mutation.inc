
int main(void){
 open_manual();esp32_mquickjs_wifi_mesh_control_data_t d={.number=MESH_NODE,.duration=6,
  .data.parent.sta={.ssid={'p'},.channel=6}};
 esp32_mquickjs_wifi_mesh_control_t c={.kind=ESP32_MQUICKJS_MESH_SET_PARENT,.detail=&d};
 assert(esp32_mquickjs_wifi_mesh_sdk_control(&active,&c)==ESP_ERR_INVALID_ARG&&!parent_sets);
 d.duration=2;d.data.parent.sta.ssid[0]='p';d.data.parent.sta.channel=6;native_self_organized=true;
 assert(esp32_mquickjs_wifi_mesh_sdk_control(&active,&c)==ESP_ERR_INVALID_STATE&&!parent_sets);
 native_self_organized=false;d.data.parent.sta.ssid[0]='p';d.data.parent.sta.channel=6;
 assert(!esp32_mquickjs_wifi_mesh_sdk_control(&active,&c)&&parent_sets==1);
 assert(all_zero(&d.data,sizeof(d.data)));close_manual();
}
