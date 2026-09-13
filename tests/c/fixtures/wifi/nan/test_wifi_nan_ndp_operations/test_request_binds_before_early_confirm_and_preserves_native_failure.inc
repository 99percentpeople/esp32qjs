
int main(void){
 assert(!esp32_mquickjs_wifi_nan_tx_open());
 wifi_nan_datapath_req_t req={.pub_id=3,.peer_mac={2,3,4,5,6,7}};
 uint32_t identity=0;esp32_mquickjs_wifi_nan_ndp_status_t state;
 peer_found=false;
 assert(esp32_mquickjs_wifi_nan_sdk_ndp_request(7,&req,&identity)==ESP_ERR_NOT_FOUND&&!identity&&!request_calls);
 peer_found=true;
 assert(!esp32_mquickjs_wifi_nan_sdk_ndp_request(7,&req,&identity)&&identity&&request_calls==1&&!depth);
 assert(esp32_mquickjs_wifi_nan_ndp_status(identity,&state)&&state.accepted&&state.host_bound&&state.submitted);
 assert(state.ndp_id==9&&host.ndp_id==9&&host.publisher_id==3);
 assert(!esp32qjs_nan_ndp_peer_service(req.peer_mac,3,&s_nan_ctx.own_svc[1]));
 assert(esp32qjs_nan_ndp_peer_service(req.peer_mac,3,&s_nan_ctx.own_svc[0]));
 assert(esp32_mquickjs_wifi_nan_sdk_ndp_request(7,&req,&identity)==ESP_ERR_INVALID_ARG&&request_calls==1);
 esp32qjs_nan_ndp_delete(native_ndl);
 assert(delete_calls==1&&!host.publisher_id&&disconnected==1&&!depth);
 assert(esp32_mquickjs_wifi_nan_ndp_status(identity,&state)&&state.native_deleted);
 assert(!esp32_mquickjs_wifi_nan_tx_close());
 assert(!esp32_mquickjs_wifi_nan_tx_open());
 identity=0;request_error=-73;
 assert(esp32_mquickjs_wifi_nan_sdk_ndp_request(7,&req,&identity)==-73&&identity&&request_calls==2);
 assert(esp32_mquickjs_wifi_nan_ndp_status(identity,&state)&&state.error==-73&&!state.submitted);
 assert(host.publisher_id==3); /* failed submit cannot prove native retirement */
 assert(!esp32_mquickjs_wifi_nan_tx_close());
}
