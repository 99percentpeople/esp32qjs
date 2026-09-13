
int main(void){
 setup();error=-81;
 assert(esp32qjs_wps_ap_peer_remove_locked(&h,&a.sta)==-81);
 assert(a.remove_needed && a.error==-81 && !a.tickets[0]);
 available=false;assert(esp32qjs_wps_ap_peer_delays_stop(&h,identity)==ESP_ERR_INVALID_STATE);
 assert(a.sta.remove_pending && !removed);
 available=true;assert(esp32qjs_wps_ap_peer_delays_stop(&h,identity)==0);
 assert(removed==1 && !table_lock);return 0;
}
