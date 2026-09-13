
int main(void){
 setup();assert(esp32qjs_wps_eapol_arm(&a.sm,1)==0);struct queued old=last;
 sems[0]=false;old.fn(old.high,old.low);
 assert(!steps && !ticks && last.seconds==0 && last.useconds==1000);
 assert(last.high==old.high && last.low==old.low && a.timers[1]);
 struct queued retry=last;h.sta_list=&peers[1];peers[0].eapol_sm=NULL;
 memset(&a,0xa5,sizeof(a)); /* Freed storage must not be looked up. */
 retry.fn(retry.high,retry.low);assert(!steps && !activity && !locked);
 return 0;
}
