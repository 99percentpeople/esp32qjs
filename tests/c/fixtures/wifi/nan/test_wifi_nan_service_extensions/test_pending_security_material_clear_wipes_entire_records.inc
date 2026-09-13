
int main(void){
 memset(&s_pending_m1,0x67,sizeof(s_pending_m1));memset(&s_pending_scia,0x78,sizeof(s_pending_scia));
 esp32_mquickjs_wifi_nan_sdk_security_clear_pending();
 for(size_t i=0;i<sizeof(s_pending_m1);i++)assert(!((uint8_t*)&s_pending_m1)[i]);
 for(size_t i=0;i<sizeof(s_pending_scia);i++)assert(!((uint8_t*)&s_pending_scia)[i]);
}
