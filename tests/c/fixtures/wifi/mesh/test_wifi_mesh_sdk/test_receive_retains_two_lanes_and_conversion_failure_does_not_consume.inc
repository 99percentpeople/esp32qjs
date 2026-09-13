
int main(void){
 esp32_mquickjs_wifi_mesh_config_t c=config();assert(!esp32_mquickjs_wifi_mesh_sdk_reserve(4,&c,&active));
 assert(!esp32_mquickjs_wifi_mesh_sdk_start(&active));
 assert(!esp32_mquickjs_wifi_mesh_sdk_receive(&active,false));
 assert(!esp32_mquickjs_wifi_mesh_sdk_receive(&active,false)&&recv_calls==1);
 assert(!esp32_mquickjs_wifi_mesh_sdk_receive(&active,true)&&recv_calls==2);
 esp32_mquickjs_wifi_mesh_message_t self,ds;uint8_t bytes[MESH_MTU];
 assert(esp32_mquickjs_wifi_mesh_sdk_message_copy(&active,false,&self,bytes,2)==ESP_ERR_INVALID_SIZE);
 assert(!esp32_mquickjs_wifi_mesh_sdk_message_copy(&active,false,&self,bytes,sizeof(bytes)));
 assert(self.length==5&&!memcmp(bytes,"hello",5));
 assert(!esp32_mquickjs_wifi_mesh_sdk_message_copy(&active,true,&ds,bytes,sizeof(bytes)));
 assert(ds.to_ds&&ds.sequence!=self.sequence);
 assert(esp32_mquickjs_wifi_mesh_sdk_message_commit(&active,true,self.sequence)==ESP_ERR_INVALID_STATE);
 assert(!esp32_mquickjs_wifi_mesh_sdk_message_commit(&active,false,self.sequence));
 assert(esp32_mquickjs_wifi_mesh_sdk_message_commit(&active,false,self.sequence)==ESP_ERR_INVALID_STATE);
 assert(!esp32_mquickjs_wifi_mesh_sdk_message_commit(&active,true,ds.sequence));
 s_mesh->next_sequence=UINT32_MAX;emit_during_receive=true;
 assert(!esp32_mquickjs_wifi_mesh_sdk_receive(&active,false));
 assert(!esp32_mquickjs_wifi_mesh_sdk_message_copy(&active,false,&self,bytes,sizeof(bytes)));
 assert(self.sequence==UINT32_MAX);assert(!esp32_mquickjs_wifi_mesh_sdk_message_commit(&active,false,self.sequence));
 assert(esp32_mquickjs_wifi_mesh_sdk_receive(&active,false)==ESP_ERR_NO_MEM);
 assert(!esp32_mquickjs_wifi_mesh_sdk_close(&active));assert(!esp32_mquickjs_wifi_mesh_sdk_release(&active));
 assert(!live_allocations);
}
