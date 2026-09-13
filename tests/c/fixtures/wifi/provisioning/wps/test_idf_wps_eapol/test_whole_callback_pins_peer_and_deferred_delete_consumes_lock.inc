
int main(void){
 setup();action=1;assert(esp32qjs_wps_eapol_arm(&a.sm,1)==0);
 last.fn(last.high,last.low);
 assert(steps==1 && deleted==1 && !peers[0].eapol_sm && !activity && !locked);
 assert(!sems[0]); /* ap_free_sta consumes the held semaphore. */
 assert(peers[1].eapol_sm==&b.sm && !b.stopped);
 return 0;
}
