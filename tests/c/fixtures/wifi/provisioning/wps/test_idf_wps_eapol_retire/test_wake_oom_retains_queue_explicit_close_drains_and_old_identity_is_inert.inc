
int main(void){
 struct esp32qjs_wps_eapol_record *a=make_record();wake_error=-77;
 eapol_auth_free(&a->sm);assert(!destroyed && !esp32qjs_wps_eapol_wake_identity);
 assert(!esp32qjs_wps_ap_eapol_drained(identity));
 assert(esp32qjs_wps_ap_eapol_drain(&context,identity+1)==ESP_ERR_INVALID_STATE);
 assert(esp32qjs_wps_ap_eapol_drain(&context,identity)==0);
 assert(destroyed==1 && esp32qjs_wps_ap_eapol_drained(identity));
 uint32_t old=identity++;wake_error=0;a=make_record();eapol_auth_free(&a->sm);
 esp32qjs_wps_eapol_wake((void*)(uintptr_t)old,NULL);
 assert(destroyed==1 && esp32qjs_wps_eapol_wake_identity==identity);
 queued(queued_arg,NULL);assert(destroyed==2 && esp32qjs_wps_ap_eapol_drained(identity));
 return 0;
}
