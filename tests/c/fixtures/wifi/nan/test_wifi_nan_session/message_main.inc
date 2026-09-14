
static void tick(void){clock_us+=100000;assert(esp32_mquickjs_wifi_nan_service());run_queued(NULL);}
int main(void){
 assert(!esp32_mquickjs_wifi_nan_open_runtime());wifi_nan_sync_config_t cfg={.op_channel=6};
 esp32_mquickjs_wifi_nan_session_t*s=NULL;assert(!esp32_mquickjs_wifi_nan_session_create(&cfg,1000,&s));
 assert(!esp32_mquickjs_wifi_nan_session_activate(s));tick();
 esp32_mquickjs_wifi_nan_service_config_t config={.publish={.service_name="test"}};
 esp32_mquickjs_wifi_nan_discovery_t*d=NULL;assert(!esp32_mquickjs_wifi_nan_discovery_create(s,true,&config,1000,&d));
 esp32_mquickjs_event_queue_t q={.references=1};assert(!esp32_mquickjs_wifi_nan_discovery_activate(d,&q));tick();
 uint8_t bytes[3]={'a','b','c'};wifi_nan_followup_params_t params={.peer_inst_id=3,.peer_mac={2,3,4,5,6,7},.ssi_len=3,.ssi=bytes};
 esp32_mquickjs_wifi_nan_message_t*m=NULL,*next=NULL;
 assert(!esp32_mquickjs_wifi_nan_message_create(d,&params,1000,&m));
 esp32_mquickjs_wifi_nan_message_cancel(m,false);
 assert(esp32_mquickjs_wifi_nan_message_activate(m)==ESP_ERR_INVALID_STATE&&!message_sends);
 esp32_mquickjs_wifi_nan_message_release(m);m=NULL;
 assert(!esp32_mquickjs_wifi_nan_message_create(d,&params,1000,&m));
 bytes[0]='z';assert(m->params.ssi[0]=='a');message_inline=true;
 assert(!esp32_mquickjs_wifi_nan_message_activate(m));tick();
 assert(m->status.done&&!m->status.error&&!m->status.retired&&m->status.tx.tx_done);
 assert(!d->status.send_pending&&d->status.send_cleanup_pending&&message_sends==1);
 assert(!esp32_mquickjs_wifi_nan_message_create(d,&params,1000,&next));
 assert(esp32_mquickjs_wifi_nan_message_activate(next)==ESP_ERR_INVALID_STATE);
 esp32_mquickjs_wifi_nan_message_release(next);next=NULL;
 message_tx.buffer_retired=true;tick();assert(m->status.retired&&!s->message&&!d->status.send_identity);
 esp32_mquickjs_wifi_nan_message_release(m);m=NULL;
 /* Completion time belongs to the native callback; a late cleanup worker
  * must not turn an already timely success into a timeout. */
 message_inline=false;assert(!esp32_mquickjs_wifi_nan_message_create(d,&params,1000,&m));
 assert(!esp32_mquickjs_wifi_nan_message_activate(m));tick();
 message_tx.tx_done=message_tx.tx_succeeded=true;message_tx.completed_us=clock_us;
 clock_us=m->deadline_us+1;esp32_mquickjs_wifi_nan_message_status_t snapshot;
 esp32_mquickjs_wifi_nan_message_status(m,&snapshot);
 assert(snapshot.done&&!snapshot.error&&!snapshot.timed_out&&!snapshot.retired);
 message_tx.buffer_retired=true;tick();esp32_mquickjs_wifi_nan_message_release(m);m=NULL;
 message_inline=false;assert(!esp32_mquickjs_wifi_nan_message_create(d,&params,1000,&m));
 assert(!esp32_mquickjs_wifi_nan_message_activate(m));tick();
 message_tx.tx_done=true;message_tx.tx_succeeded=false;message_tx.completed_us=clock_us;
 esp32_mquickjs_wifi_nan_message_status(m,&snapshot);
 assert(snapshot.done && !snapshot.error && snapshot.tx.tx_done && !snapshot.tx.tx_succeeded && !snapshot.retired);
 message_tx.buffer_retired=true;tick();esp32_mquickjs_wifi_nan_message_release(m);m=NULL;
 message_inline=false;assert(!esp32_mquickjs_wifi_nan_message_create(d,&params,1000,&m));
 assert(!esp32_mquickjs_wifi_nan_message_activate(m));tick();
 esp32_mquickjs_wifi_nan_message_cancel(m,true);assert(m->status.timed_out&&m->status.done&&!m->status.retired);
 esp32_mquickjs_wifi_nan_message_release(m);m=NULL;assert(s_nan_message_handles==1);
 message_tx.tx_done=message_tx.tx_succeeded=message_tx.buffer_retired=true;tick();
 assert(!s_nan_message_handles&&!s->message&&s->status.ready&&d->status.ready);
 assert(!esp32_mquickjs_wifi_nan_message_create(d,&params,1000,&m));
 assert(!esp32_mquickjs_wifi_nan_message_activate(m));tick();
 esp32_mquickjs_wifi_nan_session_close(s,false);tick();
 assert(m->status.retired&&m->status.done&&m->status.cancelled&&m->status.tx.buffer_retired&&!s->message);
 assert(d->status.retired&&!d->parent&&q.closed);
 esp32_mquickjs_wifi_nan_discovery_queue_closed(d);
 esp32_mquickjs_wifi_nan_session_release(s);esp32_mquickjs_wifi_nan_discovery_release(d);
 esp32_mquickjs_wifi_nan_message_release(m);
 assert(!s_nan_message_handles&&!s_nan_discovery_handles&&!s_nan_handles&&!allocations);
}
