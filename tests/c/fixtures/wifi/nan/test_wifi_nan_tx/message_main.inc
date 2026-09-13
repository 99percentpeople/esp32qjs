
int main(void){
 assert(!esp32_mquickjs_wifi_nan_tx_open());uint32_t context=0;
 assert(!esp32_mquickjs_wifi_nan_tx_message_begin(10,&context));
 assert(!__wrap_nan_send_followup_msg(&context,&context));
 esp32_mquickjs_wifi_nan_message_tx_status_t status;
 assert(esp32_mquickjs_wifi_nan_tx_message_status(10,&status)&&status.entered&&status.allocated&&!status.buffer_retired);
 uint32_t ticket=esp32_mquickjs_wifi_nan_tx_callback_enter(message_buffer);assert(ticket==status.ticket);
 uint32_t freeing=esp32_mquickjs_wifi_nan_tx_recycling(message_buffer);assert(freeing==ticket);
 esp32_mquickjs_wifi_nan_tx_recycled(freeing);
 assert(esp32_mquickjs_wifi_nan_tx_message_status(10,&status)&&!status.buffer_retired&&!status.tx_done);
 assert(esp32_mquickjs_wifi_nan_tx_message_release(10)==ESP_ERR_TIMEOUT);
 esp32_mquickjs_wifi_nan_tx_callback_leave(ticket,true);
 assert(esp32_mquickjs_wifi_nan_tx_message_status(10,&status)&&status.buffer_retired&&status.tx_done&&status.tx_succeeded&&status.retired_us>=status.completed_us);
 assert(!esp32_mquickjs_wifi_nan_tx_message_release(10));
 next_buffer=0;context=0;assert(!esp32_mquickjs_wifi_nan_tx_message_begin(11,&context));
 assert(!__wrap_nan_send_followup_msg(&context,&context));
 assert(esp32_mquickjs_wifi_nan_tx_message_status(11,&status)&&status.ticket!=ticket&&!status.tx_done);
 esp32_mquickjs_wifi_nan_tx_callback_leave(ticket,true);esp32_mquickjs_wifi_nan_tx_recycled(ticket);
 assert(esp32_mquickjs_wifi_nan_tx_message_status(11,&status)&&!status.tx_done&&!status.buffer_retired);
 freeing=esp32_mquickjs_wifi_nan_tx_recycling(message_buffer);esp32_mquickjs_wifi_nan_tx_recycled(freeing);
 assert(esp32_mquickjs_wifi_nan_tx_message_status(11,&status)&&!status.tx_done&&status.buffer_retired);
 assert(!esp32_mquickjs_wifi_nan_tx_message_release(11));
 context=0;fail_native=true;assert(!esp32_mquickjs_wifi_nan_tx_message_begin(12,&context));
 assert(__wrap_nan_send_followup_msg(&context,&context)==-1);
 assert(esp32_mquickjs_wifi_nan_tx_message_status(12,&status)&&status.entered&&!status.allocated&&status.buffer_retired);
 assert(!esp32_mquickjs_wifi_nan_tx_message_release(12));
 assert(!esp32_mquickjs_wifi_nan_tx_close()&&!heap_live);
}
