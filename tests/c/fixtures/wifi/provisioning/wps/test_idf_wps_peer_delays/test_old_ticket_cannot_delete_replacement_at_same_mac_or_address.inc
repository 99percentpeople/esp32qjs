
int main(void){
 setup();assert(esp32qjs_wps_ap_peer_remove_locked(&h,&a.sta)==0);
 struct queued old=last;esp32qjs_wps_peer_cancel(&a.sta,0);
 memset(&a,0,sizeof(a));h.sta_list=&a.sta;a.sta.lock=&available;
 assert(esp32qjs_wps_ap_peer_remove_locked(&h,&a.sta)==0);
 old.fn(old.a,old.b);assert(!removed);
 last.fn(last.a,last.b);assert(removed==1 && !activity && !table_lock);
 return 0;
}
