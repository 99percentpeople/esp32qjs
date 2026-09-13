
static void tick(void){clock_us+=100000;assert(esp32_mquickjs_wifi_nan_service());run_queued(NULL);}
int main(void){
 assert(!esp32_mquickjs_wifi_nan_open_runtime());wifi_nan_sync_config_t config={.op_channel=6};
 esp32_mquickjs_wifi_nan_session_t*s=NULL;assert(!esp32_mquickjs_wifi_nan_session_create(&config,1000,&s));
 assert(!esp32_mquickjs_wifi_nan_session_activate(s));tick();
 esp32_mquickjs_wifi_nan_service_config_t cfg={.publish={.service_name="test"}};
 esp32_mquickjs_wifi_nan_discovery_t*d=NULL,*next=NULL;
 fail_allocation=allocation_calls+1;
 assert(esp32_mquickjs_wifi_nan_discovery_create(s,true,&cfg,1000,&d)==ESP_ERR_NO_MEM&&!d&&!s_nan_discovery_handles);
 fail_allocation=0;assert(!esp32_mquickjs_wifi_nan_discovery_create(s,true,&cfg,1000,&d));
 esp32_mquickjs_event_queue_t q={.references=1};assert(!esp32_mquickjs_wifi_nan_discovery_activate(d,&q));
 tick();assert(d->status.ready&&d->status.native_id==7&&q.sent==1&&q.last.identity==d->status.identity);
 assert(q.last.ssi_len==3&&!memcmp(q.last.ssi,"abc",3));
 esp32_mquickjs_wifi_nan_discovery_close(d);service_cancel_pending=true;tick();
 assert(d->status.cancelled&&!d->status.retired&&service_cancels==1&&d->parent==s);
 assert(!esp32_mquickjs_wifi_nan_discovery_create(s,true,&cfg,1000,&next));
 esp32_mquickjs_event_queue_t other={.references=1};
 assert(esp32_mquickjs_wifi_nan_discovery_activate(next,&other)==ESP_ERR_INVALID_STATE&&other.references==1);
 service_cancel_pending=false;tick();assert(d->status.retired&&!d->parent&&q.closed&&service_cancels==1);
 esp32_mquickjs_wifi_nan_discovery_queue_closed(d);assert(q.references==1);
 assert(!esp32_mquickjs_wifi_nan_discovery_activate(next,&other));tick();
 assert(next->status.ready&&next->status.native_id==7&&next->status.identity!=d->status.identity);
 esp32_mquickjs_wifi_nan_discovery_release(d);
 /* Parent runtime teardown retires child native state; a closed handle keeps
  * its own snapshot but no parent reference or Radio owner. */
 assert(!esp32_mquickjs_wifi_nan_prepare_runtime_destroy());run_queued(NULL);
 assert(next->status.retired&&!next->parent&&other.closed);
 esp32_mquickjs_wifi_nan_discovery_queue_closed(next);
 esp32_mquickjs_wifi_nan_session_release(s);s=NULL;
 assert(!s_nan_handles&&s_nan_discovery_handles==1&&!s_nan_active);
 assert(esp32_mquickjs_wifi_nan_open_runtime()==ESP_ERR_INVALID_STATE);
 esp32_mquickjs_wifi_nan_discovery_release(next);next=NULL;
 assert(!allocations&&!s_nan_discovery_handles&&esp32_mquickjs_wifi_nan_open_runtime()==0);
}
