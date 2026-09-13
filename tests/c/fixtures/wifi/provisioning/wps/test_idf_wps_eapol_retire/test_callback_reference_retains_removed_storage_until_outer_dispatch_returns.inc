
int main(void){
 struct esp32qjs_wps_eapol_record *a=make_record();
 assert(esp32qjs_wps_eapol_ref(&a->sm));assert(esp32qjs_wps_eapol_ref(&a->sm));
 eapol_auth_free(&a->sm);eapol_auth_free(&a->sm);
 assert(!esp32qjs_wps_eapol_ref(&a->sm) && esp32qjs_wps_eapol_is_retired(&a->sm));
 queued(queued_arg,NULL);assert(!destroyed && !esp32qjs_wps_ap_eapol_drained(identity));
 esp32qjs_wps_eapol_unref(&a->sm);queued(queued_arg,NULL);assert(!destroyed);
 esp32qjs_wps_eapol_unref(&a->sm);queued(queued_arg,NULL);
 assert(destroyed==1 && esp32qjs_wps_ap_eapol_drained(identity));return 0;
}
