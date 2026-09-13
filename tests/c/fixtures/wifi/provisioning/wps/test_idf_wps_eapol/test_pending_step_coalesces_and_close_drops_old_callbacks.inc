
int main(void){
 setup();assert(esp32qjs_wps_eapol_arm(&a.sm,1)==0);struct queued old=last;unsigned before=registrations;
 assert(esp32qjs_wps_eapol_arm(&a.sm,1)==0 && registrations==before);
 closing=true;assert(esp32qjs_wps_ap_eapol_stop(&h,owner_id)==0);
 old.fn(old.high,old.low);assert(!steps && !activity && a.stopped && b.stopped);
 assert(esp32qjs_wps_eapol_arm(&a.sm,0)==ESP_ERR_INVALID_STATE);
 return 0;
}
