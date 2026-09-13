
int main(void){
 setup();assert(esp32qjs_wps_eapol_arm(&a.sm,0)==0);
 struct queued old=last;
 old.fn(old.high,old.low);assert(ticks==1 && !activity && !locked);
 assert(a.timers[0] && a.timers[0]!=(uint32_t)(uintptr_t)old.low);
 old.fn(old.high,old.low);assert(ticks==1);
 struct queued current=last;
 esp32qjs_wps_eapol_cancel(&a.sm,0);current.fn(current.high,current.low);assert(ticks==1);
 assert(esp32qjs_wps_eapol_arm(&a.sm,1)==0);old=last;
 esp32qjs_wps_eapol_stop_record(&a.sm);
 memset(&a,0,sizeof(a));a.identity=owner_id;peers[0].eapol_sm=&a.sm;
 assert(esp32qjs_wps_eapol_arm(&a.sm,1)==0);current=last;
 old.fn(old.high,old.low);assert(!steps);
 current.fn(current.high,current.low);assert(steps==1 && !a.timers[1]);
 old=current;owner_id++;a.identity=owner_id;
 old.fn(old.high,old.low);assert(steps==1 && !activity);
 return 0;
}
