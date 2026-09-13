
static void *message_buffer;
int __real_nan_send_followup_msg(uint32_t *context,void *params){
 assert(!locked&&context&&params);message_buffer=esp32qjs_wifi_nan_alloc_sdf(1,2,3,4,5);
 if(!message_buffer)return -1;
 *context=(uint32_t)(uintptr_t)message_buffer;return 0;
}
