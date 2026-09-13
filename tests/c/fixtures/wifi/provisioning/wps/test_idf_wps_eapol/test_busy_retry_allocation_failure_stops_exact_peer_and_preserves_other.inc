
int main(void){
 setup();assert(esp32qjs_wps_eapol_arm(&b.sm,0)==0);
 assert(esp32qjs_wps_eapol_arm(&a.sm,0)==0);
 assert(esp32qjs_wps_eapol_arm(&a.sm,1)==0);struct queued pending=last;
 sems[0]=false;register_error=-91;
 pending.fn(pending.high,pending.low);
 assert(a.stopped && a.error==-91 && !a.timers[0] && !a.timers[1]);
 assert(!b.stopped && b.timers[0] && errors==1 && !activity && !locked);
 return 0;
}
