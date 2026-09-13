
int main(void){
 assert(!esp32qjs_nan_usd_transport_open());assert(!send_frame(1,0xa1));
 begin_message(22);assert(!send_frame(2,0xb2));
 esp32qjs_usd_transport_t*t=s_esp32qjs_usd_transport;t->message_scope=false;
 assert(submits==1&&t->pending&&t->pending->data[0]==0xb2&&!t->message.buffer_retired);
 assert(!esp32qjs_nan_usd_service_drain(3)&&t->pending&&submits==1);
 assert(esp32qjs_nan_usd_transport_service(false)==ESP_ERR_NOT_FINISHED);
 physical.buffer_present=false;physical.recycled=true;early=true;
 assert(!esp32qjs_nan_usd_transport_service(false)&&submits==2&&last_byte==0xb2&&!t->pending);
 assert(t->message.ticket==t->ticket&&t->message.tx_done);
 busy=false;physical.buffer_present=false;physical.recycled=true;
 assert(!esp32qjs_nan_usd_transport_service(false));
 assert(!esp32_mquickjs_wifi_nan_usd_message_release(22));
 assert(!esp32qjs_nan_usd_transport_free()&&allocations==frees);
}
