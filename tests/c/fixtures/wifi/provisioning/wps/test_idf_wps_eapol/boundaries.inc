
static struct esp32qjs_wps_eapol_record a,b;
static void setup(void){
 memset(&a,0,sizeof(a));memset(&b,0,sizeof(b));memset(peers,0,sizeof(peers));
 a.identity=b.identity=owner_id;peers[0].next=&peers[1];h.sta_list=peers;
 peers[0].eapol_sm=&a.sm;peers[1].eapol_sm=&b.sm;
 for(unsigned i=0;i<2;i++){sems[i]=true;peers[i].lock=&sems[i];peers[i].addr[0]=i+1;atomic_init(&peers[i].remove_pending,false);}
}
static void eapol_port_timers_tick(void *ctx,void *arg){
 assert(!ctx && activity && !locked);ticks++;
 struct eapol_state_machine *sm=arg;assert((sm==&a.sm && !sems[0]) || (sm==&b.sm && !sems[1]));
 esp32qjs_wps_eapol_arm(sm,0);
}
static void eapol_sm_step_cb(void *ctx,void *arg){
 assert(ctx==&a.sm && !arg && !sems[0] && activity && !locked);steps++;
 if(action==1)atomic_store(&peers[0].remove_pending,true);
}
static void ap_free_sta(struct hostapd_data *hapd,struct sta_info *sta){
 assert(hapd==&h && sta==&peers[0] && !sems[0] && activity && !locked);
 h.sta_list=sta->next;esp32qjs_wps_eapol_stop_record(sta->eapol_sm);sta->eapol_sm=NULL;deleted++;
}
