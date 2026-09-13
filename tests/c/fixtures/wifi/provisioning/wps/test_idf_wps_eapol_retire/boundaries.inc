
static struct esp32qjs_wps_eapol_record *make_record(void){
 struct esp32qjs_wps_eapol_record *p=calloc(1,sizeof(*p));assert(p);
 p->identity=identity;assert(esp32qjs_wps_eapol_adopt(identity));return p;
}
static void esp32qjs_wps_eapol_destroy(struct eapol_state_machine *sm){
 assert(native && !critical && esp32qjs_wps_eapol_children);
 assert(!esp32qjs_wps_ap_eapol_drained(identity));
 destroyed++;free(sm);
}
static int eloop_register_timeout(unsigned sec,unsigned us,void(*fn)(void*,void*),void*a,void*b){
 (void)sec;(void)us;assert(!b && !critical);queued=fn;queued_arg=a;
 if(schedule_interleave){
  schedule_interleave=false;bool was=native;native=true;
  fn(a,b);assert(destroyed==1 && !esp32qjs_wps_ap_eapol_drained(identity));native=was;
 }
 return wake_error;
}
static int eloop_cancel_timeout(void(*fn)(void*,void*),void*a,void*b){
 (void)fn;(void)a;(void)b;assert(!critical);return 0;
}
