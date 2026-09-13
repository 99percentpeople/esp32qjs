
static int esp_wifi_remain_on_channel(wifi_roc_req_t*request){
 assert(!critical&&depth&&native_task&&request->rx_cb==s_action_rx_cb);
 if(request->type==WIFI_ROC_REQ){
  submits++;present=true;native_done=request->done_cb;assert(native_done);
  assert(s_dpp_roc.held&&!s_dpp_roc.returned&&roc_in_progress&&!(bits&DPP_ROC_EVENT_HANDLED));
  request->op_id=driver_id;
  if(synchronous){native_done(0x1234,driver_id,WIFI_ROC_DONE);assert(s_dpp_roc.held&&s_dpp_roc.complete&&!s_dpp_roc.returned);present=false;}
  return submit_error;
 }
 assert(request->type==WIFI_ROC_CANCEL&&request->op_id==driver_id&&request->channel==6);
 cancels++;if(cancel_error)return cancel_error;
 if(complete_cancel){native_done(0x1234,driver_id,WIFI_ROC_FAIL);present=false;}
 return 0;
}
static void dpp_abort_failure_locked(uint32_t error){
 assert(depth&&!critical);abort_error=error;result.status.terminal=true;result.status.error=error;
 s_dpp_ctx.dpp_listen_ongoing=false;esp32qjs_dpp_roc_cancel_locked();dpp_api_unlock();
}
static esp_err_t submit(void){
 wifi_roc_req_t request={.type=WIFI_ROC_REQ,.channel=6,.rx_cb=s_action_rx_cb};
 dpp_api_lock();esp_err_t error=esp32qjs_dpp_roc_submit_locked(&request);dpp_api_unlock();return error;
}
static void tick(void){struct queued q=detach();q.fn(q.high,q.low);assert(!depth&&!critical);}
static int cancel(void){dpp_api_lock();int error=esp32qjs_dpp_roc_cancel_locked();dpp_api_unlock();return error;}
int main(void){
 /* Scheduler reservation failure precedes native ownership or bit changes. */
 bits=1;queue_full=true;assert(submit()==ESP_ERR_NO_MEM&&!submits&&!esp32qjs_dpp_roc_held()&&bits==1);queue_full=false;
 assert(!submit()&&submits==1&&s_dpp_roc.held&&s_dpp_roc.returned);
 native_done(0x5678,driver_id,WIFI_ROC_DONE);native_done(0x1234,driver_id+1,WIFI_ROC_DONE);
 assert(!s_dpp_roc.complete&&s_dpp_roc.ignored==2);
 native_done(0x1234,driver_id,WIFI_ROC_DONE);native_done(0x1234,driver_id,WIFI_ROC_DONE);
 assert(s_dpp_roc.complete&&s_dpp_roc.duplicates==1);
 s_dpp_ctx.dpp_listen_ongoing=true;tick();assert(s_dpp_roc.held&&!next_channels);
 present=false;tick();assert(!s_dpp_roc.held&&next_channels==1&&bits==1&&!roc_in_progress);
 /* A callback during submission is captured, not dispatched before return. */
 synchronous=true;assert(!submit()&&s_dpp_roc.complete&&s_dpp_roc.held);tick();assert(!s_dpp_roc.held&&next_channels==2);synchronous=false;
 /* Detached old dispatch uses its non-reused ticket, even after driver ID
  * wrap/reuse. No old queued status is fed through the callback as if native. */
 assert(!submit());struct queued stale=detach();uint64_t old=s_dpp_roc.ticket;
 assert(!cancel()&&!s_dpp_roc.held);assert(!submit()&&s_dpp_roc.ticket>old);
 stale.fn(stale.high,stale.low);assert(s_dpp_roc.held&&!s_dpp_roc.complete&&roc_in_progress);
 assert(!cancel());
 for(unsigned i=0;i<300;i++){driver_id=(uint8_t)i;assert(!submit());old=s_dpp_roc.ticket;stale.fn(stale.high,stale.low);assert(s_dpp_roc.ticket==old&&s_dpp_roc.held);assert(!cancel());}
 /* Successful cancellation submission is not repeated while the physical
  * record remains present; a failed cancellation is retried exactly. */
 assert(!submit());complete_cancel=false;unsigned n=cancels;
 assert(cancel()==ESP_ERR_TIMEOUT&&cancels==n+1&&s_dpp_roc.held);
 assert(cancel()==ESP_ERR_TIMEOUT&&cancels==n+1);present=false;assert(!cancel()&&cancels==n+1);
 complete_cancel=true;assert(!submit());cancel_error=77;n=cancels;
 assert(cancel()==77&&cancels==n+1&&s_dpp_roc.held);cancel_error=0;assert(!cancel()&&cancels==n+2);
 /* Native submission errors retain exact cleanup obligations. */
 submit_error=78;assert(submit()==78&&s_dpp_roc.held&&result.status.roc_submit_error==78);
 submit_error=0;assert(!cancel());
 /* Lost/silent native completion fails explicitly after physical retirement. */
 assert(!submit());present=false;tick();assert(abort_error==ESP_ERR_INVALID_STATE&&!s_dpp_roc.held);
 result.status.terminal=false;abort_error=0;
 /* A later dispatch admission failure captures failure and cancels native
  * work. Callback capture itself has no queue/allocator dependency. */
 assert(!submit());queue_full=true;tick();assert(abort_error==ESP_ERR_NO_MEM&&!s_dpp_roc.held);queue_full=false;
 result.status.terminal=false;
 /* Result snapshots expose only this SDK generation, without secret data. */
 esp32_mquickjs_wifi_dpp_result_status_t out={0};esp32qjs_dpp_roc_status_locked(&out);
 assert(out.roc_operation==s_dpp_roc.ticket&&!out.roc_held);
 s_dpp_generation++;memset(&out,0,sizeof(out));esp32qjs_dpp_roc_status_locked(&out);assert(!out.roc_operation);
 s_dpp_roc_last_ticket=UINT64_MAX;n=submits;assert(submit()==ESP_ERR_NO_MEM&&submits==n&&!s_dpp_roc.held);
 assert(!depth&&!critical);return 0;
}
