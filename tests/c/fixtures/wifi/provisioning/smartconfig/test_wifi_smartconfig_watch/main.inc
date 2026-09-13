
static void observe(esp32_mquickjs_wifi_smartconfig_session_t *s){
 assert(!s_sc_watch_handles && !s_sc_watch_sources[0]);memset(&q,0,sizeof(q));
 sc_watch_source_t *source=heap_caps_calloc(1,sizeof(*source),1);assert(source);
 assert(esp32_mquickjs_wifi_smartconfig_session_retain(s));
 source->session=s;source->queue=&q;source->context_bound=true;q.source=source;
 s_sc_watch_sources[0]=source;s_sc_watch_handles++;
}
static void destroy_queue(void){
 assert(q.closed && !q.refs);q.disposed=true;sc_watch_destroyed(q.source);q.source=NULL;
 assert(!s_sc_watch_handles && !s_sc_watch_sources[0]);
}
int main(void){
 reset();esp32_mquickjs_wifi_smartconfig_session_t *s=create();activate(s);tick();observe(s);
 assert(esp32_mquickjs_wifi_smartconfig_poll_observations(false) && q.sends==1 && q.last.sequence==1);
 assert(!esp32_mquickjs_wifi_smartconfig_poll_observations(false));
 s->status.worker_busy=true;assert(!esp32_mquickjs_wifi_smartconfig_poll_observations(false));s->status.worker_busy=false;
 assert(esp32_mquickjs_wifi_smartconfig_session_wait_begin(s,false));capture_ready=true;tick();
 assert(s->status.credentials_ready && !esp32_mquickjs_wifi_smartconfig_poll_observations(false) && q.sends==1);
 /* Even the worker's early credentials-ready write cannot bypass the gate. */
 s->status.worker_busy=true;assert(!esp32_mquickjs_wifi_smartconfig_poll_observations(false));s->status.worker_busy=false;
 esp32_mquickjs_wifi_smartconfig_session_wait_end(s,false);
 assert(esp32_mquickjs_wifi_smartconfig_poll_observations(false) && q.last.sequence==2 && !s->status.credentials_consumed);
 assert(esp32_mquickjs_wifi_smartconfig_session_wait_begin(s,true));
 q.full=true;esp32_mquickjs_wifi_smartconfig_session_close(s,false);pump();assert(s->status.retired);
 assert(!esp32_mquickjs_wifi_smartconfig_poll_observations(false));
 esp32_mquickjs_wifi_smartconfig_session_wait_end(s,true);
 assert(esp32_mquickjs_wifi_smartconfig_poll_observations(false) && q.drops==1 && q.source->last.sequence==3);
 q.full=false;assert(!esp32_mquickjs_wifi_smartconfig_poll_observations(false)); /* No terminal replay. */
 assert(esp32_mquickjs_event_queue_close(&q) && s_sc_watch_handles==1 && !s_sc_watch_sources[0]);
 esp32_mquickjs_wifi_smartconfig_session_release(s);assert(!s_sc_handles);destroy_queue();

 reset();s=create();activate(s);tick();observe(s);esp32_mquickjs_wifi_smartconfig_session_release(s);
 assert(!s->status.closing);assert(esp32_mquickjs_event_queue_close(&q));assert(s->status.closing);
 pump();assert(!s_sc_handles);destroy_queue();

 reset();s=create();activate(s);tick();observe(s);q.disposed=true;
 (void)esp32_mquickjs_wifi_smartconfig_poll_observations(true);
 assert(!s_sc_watch_sources[0] && !q.source->session && s_sc_watch_handles==1 && !s->status.closing);
 assert(esp32_mquickjs_event_queue_close(&q));destroy_queue();stop(s);

 reset();s=create();activate(s);tick();observe(s);q.source->last.sequence=UINT32_MAX;
 s->status.connection_reason=7;(void)esp32_mquickjs_wifi_smartconfig_poll_observations(false);
 assert(q.closed && !q.sends && !s_sc_watch_sources[0]);destroy_queue();stop(s);
 assert(allocs==frees && !s_sc_handles && !s_sc_workers);
 return 0;
}
