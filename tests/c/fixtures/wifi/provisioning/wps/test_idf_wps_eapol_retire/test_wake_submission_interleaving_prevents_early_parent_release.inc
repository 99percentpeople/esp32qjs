
int main(void){
 struct esp32qjs_wps_eapol_record *a=make_record();
 schedule_interleave=true;native=false;eapol_auth_free(&a->sm);
 assert(destroyed==1 && !esp32qjs_wps_eapol_producers);
 native=true;assert(esp32qjs_wps_ap_eapol_drained(identity));return 0;
}
