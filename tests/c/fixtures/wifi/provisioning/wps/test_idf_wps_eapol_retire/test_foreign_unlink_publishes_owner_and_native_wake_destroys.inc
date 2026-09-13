
int main(void){
 struct esp32qjs_wps_eapol_record *a=make_record();struct sta_info peer={&a->sm};
 native=false;esp32qjs_wps_ap_eapol_detach_peer(&peer);
 assert(!peer.eapol_sm && !destroyed && esp32qjs_wps_eapol_children==1);
 assert(!esp32qjs_wps_ap_eapol_drained(identity));
 native=true;queued(queued_arg,NULL);
 assert(destroyed==1 && !esp32qjs_wps_eapol_children);
 assert(esp32qjs_wps_ap_eapol_drained(identity) && !critical);
 return 0;
}
