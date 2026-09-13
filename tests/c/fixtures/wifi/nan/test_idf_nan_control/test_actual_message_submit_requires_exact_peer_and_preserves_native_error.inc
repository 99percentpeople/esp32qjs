
int main(void){
 uint32_t context=0;wifi_nan_followup_params_t p={.inst_id=7,.peer_inst_id=3,.peer_mac={2,3,4,5,6,7}};
 assert(esp32_mquickjs_wifi_nan_sdk_send(&p,&context)==ESP_ERR_NOT_FOUND&&!calls);
 peer_exists=true;native_error=-71;assert(esp32_mquickjs_wifi_nan_sdk_send(&p,&context)==-71&&calls==1&&!depth);
 native_error=0;assert(!esp32_mquickjs_wifi_nan_sdk_send(&p,&context)&&context==1234&&calls==2);
 assert(esp32_mquickjs_wifi_nan_sdk_send(&p,&context)==ESP_ERR_INVALID_ARG&&calls==2);
 context=0;p.peer_inst_id=0;assert(esp32_mquickjs_wifi_nan_sdk_send(&p,&context)==ESP_ERR_INVALID_ARG&&calls==2);
 p.peer_inst_id=3;p.peer_mac[0]=1;assert(esp32_mquickjs_wifi_nan_sdk_send(&p,&context)==ESP_ERR_INVALID_ARG);
 p.peer_mac[0]=2;p.ssi_len=2049;assert(esp32_mquickjs_wifi_nan_sdk_send(&p,&context)==ESP_ERR_INVALID_ARG);
 p.ssi_len=0;uint8_t body[255]={7};nan_vendor_ie_t vendor={.body_len=256,.body=body};p.vendor_ie=&vendor;
 assert(esp32_mquickjs_wifi_nan_sdk_send(&p,&context)==ESP_ERR_INVALID_ARG&&calls==2);
 vendor.body_len=255;vendor.body=NULL;
 assert(esp32_mquickjs_wifi_nan_sdk_send(&p,&context)==ESP_ERR_INVALID_ARG&&calls==2);
 vendor.body=body;
 assert(!esp32_mquickjs_wifi_nan_sdk_send(&p,&context)&&context==1234&&calls==3);
 assert(!depth&&p.vendor_ie==&vendor&&vendor.body[0]==7);
}
