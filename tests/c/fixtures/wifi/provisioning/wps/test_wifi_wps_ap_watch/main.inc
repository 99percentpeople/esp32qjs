
static void observe(esp32_mquickjs_wifi_wps_ap_session_t *s){
 assert(!s_wps_watch_handles && !s_wps_watch_sources[0]);memset(&q,0,sizeof(q));
 wps_watch_source_t *source=heap_caps_calloc(1,sizeof(*source),1);assert(source);
 assert(esp32_mquickjs_wifi_wps_ap_session_retain(s));
 source->session=s;source->queue=&q;source->context_bound=true;q.source=source;
 s_wps_watch_sources[0]=source;s_wps_watch_handles++;
}
static void destroy_queue(void){
 assert(q.closed && !q.refs);q.disposed=true;wps_watch_destroyed(q.source);q.source=NULL;
 assert(!s_wps_watch_handles && !s_wps_watch_sources[0]);
}
int main(void){
 reset();esp32_mquickjs_wifi_wps_ap_session_t *s=create();assert(!esp32_mquickjs_wifi_wps_ap_session_activate(s));tick();observe(s);
 assert(esp32_mquickjs_wifi_wps_ap_poll_observations(false) && q.sends==1 && q.last.sequence==1);
 assert(!esp32_mquickjs_wifi_wps_ap_poll_observations(false) && q.sends==1);
 s->status.worker_busy=true;assert(!esp32_mquickjs_wifi_wps_ap_poll_observations(false));s->status.worker_busy=false;
 assert(esp32_mquickjs_wifi_wps_ap_session_wait_begin(s,false));
 assert(!esp32_mquickjs_wifi_wps_ap_poll_observations(false));
 assert(!esp32_mquickjs_wifi_wps_ap_session_pin(s,NULL,true));
 esp32_mquickjs_wifi_wps_ap_session_wait_end(s,false);
 q.full=true;assert(esp32_mquickjs_wifi_wps_ap_poll_observations(false) && q.drops==1);
 assert(!esp32_mquickjs_wifi_wps_ap_poll_observations(false));
 capture_ready=true;tick();tick();assert(s->status.result_ready);
 assert(esp32_mquickjs_wifi_wps_ap_session_wait_begin(s,false));
 assert(!esp32_mquickjs_wifi_wps_ap_poll_observations(false));
 assert(!esp32_mquickjs_wifi_wps_ap_session_registered(s,NULL,true));
 esp32_mquickjs_wifi_wps_ap_session_wait_end(s,false);q.full=false;
 assert(esp32_mquickjs_wifi_wps_ap_poll_observations(false) && q.last.sequence==3 && q.last.status.result_consumed);
 assert(esp32_mquickjs_wifi_wps_ap_session_wait_begin(s,true));
 esp32_mquickjs_wifi_wps_ap_session_close(s,false);
 for(unsigned i=0;i<8 && !s->status.retired;i++)tick();assert(s->status.retired);
 assert(!esp32_mquickjs_wifi_wps_ap_poll_observations(false));
 esp32_mquickjs_wifi_wps_ap_session_wait_end(s,true);
 assert(esp32_mquickjs_wifi_wps_ap_poll_observations(false) && q.last.status.retired);
 assert(esp32_mquickjs_wifi_wps_ap_poll_observations(true) && q.closed && s_wps_watch_handles==1);
 destroy_queue();finish(s);

 reset();s=create();observe(s);esp32_mquickjs_wifi_wps_ap_session_release(s);
 assert(s_wps_handles==1);assert(esp32_mquickjs_wifi_wps_ap_poll_observations(true));
 assert(!s_wps_handles && s_wps_watch_handles==1);destroy_queue();

 reset();s=create();observe(s);q.disposed=true;
 (void)esp32_mquickjs_wifi_wps_ap_poll_observations(true);
 assert(!s_wps_watch_sources[0] && !q.source->session && s_wps_watch_handles==1);
 assert(esp32_mquickjs_event_queue_close(&q));destroy_queue();finish(s);

 reset();s=create();observe(s);q.source->last.sequence=UINT32_MAX;s->status.error=7;
 (void)esp32_mquickjs_wifi_wps_ap_poll_observations(false);
 assert(q.closed && !q.sends && !s_wps_watch_sources[0]);destroy_queue();finish(s);
 assert(allocs==frees && !s_wps_handles && !s_wps_workers);return 0;
}
