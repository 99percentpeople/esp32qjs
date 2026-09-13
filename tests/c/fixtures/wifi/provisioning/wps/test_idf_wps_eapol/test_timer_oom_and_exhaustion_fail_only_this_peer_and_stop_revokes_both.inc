
int main(void){
 setup();assert(esp32qjs_wps_eapol_arm(&b.sm,0)==0);struct queued other=last;
 register_error=-73;assert(esp32qjs_wps_eapol_arm(&a.sm,1)==-73);
 assert(a.stopped && a.error==-73 && a.sm.exit_sm_step_run && errors==1);
 assert(!b.stopped && b.timers[0]);register_error=0;
 other.fn(other.high,other.low);assert(ticks==1);
 esp32qjs_wps_eapol_last_timer=UINT32_MAX;
 assert(esp32qjs_wps_eapol_arm(&b.sm,1)==ESP_ERR_NO_MEM);
 assert(b.error==ESP_ERR_NO_MEM && b.stopped && !b.timers[0]);
 other.fn(other.high,other.low);assert(ticks==1 && !activity);
 assert(esp32qjs_wps_ap_eapol_stop(&h,owner_id)==ESP_OK);
 assert(esp32qjs_wps_ap_eapol_stop(&h,owner_id+1)==ESP_ERR_INVALID_STATE);
 return 0;
}
