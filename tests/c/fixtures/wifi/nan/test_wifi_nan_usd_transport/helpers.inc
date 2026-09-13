
void esp32_mquickjs_wifi_nan_usd_sdk_status(esp32_mquickjs_wifi_nan_usd_sdk_status_t*status){
 *status=(esp32_mquickjs_wifi_nan_usd_sdk_status_t){.identity=1};esp32qjs_nan_usd_transport_snapshot(status);}
static void begin_message(uint32_t id){
 esp32qjs_usd_transport_t*t=s_esp32qjs_usd_transport;assert(!t->message.identity);
 t->message=(esp32_mquickjs_wifi_nan_message_tx_status_t){.identity=id,.entered=true,.buffer_retired=true};t->message_scope=true;}
static int send_frame(uint8_t service,uint8_t byte){
 wifi_action_tx_req_t*request=calloc(1,sizeof(*request)+1);assert(request);
 request->ifx=0;request->type=WIFI_OFFCHAN_TX_REQ;request->channel=6;request->rx_cb=esp_nan_de_rx_action;
 request->data_len=1;request->data[0]=byte;
 uint8_t previous=esp32qjs_nan_usd_tx_service_scope(service);
 int result=esp32qjs_nan_usd_transport_tx(request,2437);esp32qjs_nan_usd_tx_service_scope(previous);
 request->data[0]=0;free(request);return result;
}
