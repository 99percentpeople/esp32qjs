
static void capture(int status,uint8_t id){
 wifi_event_action_tx_status_t event={.ifx=0,.context=0x1234,.status=status,.op_id=id,.channel=6};
 esp32qjs_dpp_tx_status_capture(&event);
}
static int esp_wifi_action_tx_req(wifi_action_tx_req_t*request){
 assert(!critical&&depth&&native_task&&request->rx_cb==s_action_rx_cb);
 if(request->type==WIFI_OFFCHAN_TX_CANCEL){
  cancels++;assert(request->op_id==driver_id);if(cancel_error)return cancel_error;
  capture(WIFI_ACTION_TX_OP_CANCELLED,driver_id);if(finish_cancel)present=false;return 0;
 }
 submits++;present=true;request->op_id=driver_id;
 assert(request->data_len);submitted_byte=request->data[0];
 physical.posted=true;physical.buffer_present=hold_frames;
 if(early){capture(WIFI_ACTION_TX_DURATION_COMPLETED,driver_id);capture(WIFI_ACTION_TX_DONE,early_wrong_id?driver_id+1:driver_id);}
 return submit_error;
}
static void esp32qjs_dpp_tx_protocol_result(enum dpp_tx_frame_type type,wifi_action_tx_status_type_t status){
 assert(depth&&!critical&&type==DPP_TX_GAS_CONFIG_REQ);protocol_calls++;last_protocol_status=status;
}
static int send(void){
 struct {wifi_action_tx_req_t request;uint8_t payload[8];}input={
  .request={.ifx=0,.type=WIFI_OFFCHAN_TX_REQ,.rx_cb=s_action_rx_cb,.channel=6,.data_len=1},.payload={0xa5}};
 dpp_api_lock();int error=esp32qjs_dpp_tx_submit_locked(&input.request,DPP_TX_GAS_CONFIG_REQ);dpp_api_unlock();
 memset(&input,0xcc,sizeof(input));return error;
}
static void poll(void){struct queued q=detach();q.fn(q.high,q.low);assert(!depth&&!critical);}
static int cancel(void){dpp_api_lock();int e=esp32qjs_dpp_tx_cancel_locked();dpp_api_unlock();return e;}
static void deferred_poll(void){
 for(unsigned i=0;i<4;i++)if(queue[i].fn==esp32qjs_dpp_tx_deferred_poll){
  struct queued q=queue[i];queue[i].fn=NULL;q.fn(q.high,q.low);assert(!depth&&!critical);return;
 }assert(0);
}
int main(void){
 queue_full=true;assert(send()==ESP_ERR_NO_MEM&&!submits&&!esp32qjs_dpp_tx_held());queue_full=false;
 roc_held=true;assert(send()==ESP_ERR_NOT_FINISHED&&!submits);roc_held=false;
 early=true;assert(!send()&&s_dpp_tx.completed&&s_dpp_tx.duration_completed&&!protocol_calls);early=false;
 uint64_t first=s_dpp_tx.ticket;capture(WIFI_ACTION_TX_FAILED,driver_id);assert(s_dpp_tx.duplicates==1);
 capture(WIFI_ACTION_TX_DONE,driver_id+1);assert(s_dpp_tx.ignored==1);
 native_task=false;capture(WIFI_ACTION_TX_DONE,driver_id);native_task=true;assert(s_dpp_tx.ignored==2);
 poll();assert(protocol_calls==1&&last_protocol_status==WIFI_ACTION_TX_DONE&&esp32qjs_dpp_tx_held());
 struct queued stale=detach();assert(!cancel()&&!esp32qjs_dpp_tx_held());
 /* Driver ID wraps/repeats, but an already detached numeric poll cannot act
  * on its successor. Duration alone never consumes the TX result. */
 assert(!send()&&s_dpp_tx.ticket>first);stale.fn(stale.high,stale.low);assert(protocol_calls==1);
 capture(WIFI_ACTION_TX_DURATION_COMPLETED,driver_id);poll();assert(protocol_calls==1);
 capture(WIFI_ACTION_TX_FAILED,driver_id);present=false;poll();assert(protocol_calls==2&&!esp32qjs_dpp_tx_held());
 assert(!send());capture(WIFI_ACTION_TX_DURATION_COMPLETED,driver_id);present=false;poll();
 assert(abort_error==ESP_ERR_INVALID_STATE&&protocol_calls==2&&!esp32qjs_dpp_tx_held());abort_error=0;
 /* Cancellation success is not retirement. Retry only the unfinished suffix. */
 assert(!send());finish_cancel=false;unsigned before=cancels;
 assert(cancel()==ESP_ERR_TIMEOUT&&cancels==before+1&&esp32qjs_dpp_tx_held());
 assert(cancel()==ESP_ERR_TIMEOUT&&cancels==before+1);present=false;assert(!cancel()&&!esp32qjs_dpp_tx_held());finish_cancel=true;
 submit_error=55;assert(send()==55&&esp32qjs_dpp_tx_held());assert(!cancel());submit_error=0;
 assert(!send());cancel_error=66;assert(cancel()==66&&esp32qjs_dpp_tx_held());cancel_error=0;assert(!cancel());
 /* Capture remains available under observer/dispatch pressure; no delivery
  * allocation can erase the first native result. */
 assert(!send());queue_full=true;capture(WIFI_ACTION_TX_DONE,driver_id);poll();
 assert(s_dpp_tx.completed&&s_dpp_tx.delivered&&abort_error==ESP_ERR_NO_MEM);queue_full=false;assert(!cancel());
 early=true;early_wrong_id=true;assert(send()==ESP_ERR_INVALID_STATE);early=false;early_wrong_id=false;assert(!cancel());
 /* Native record retirement and recycler entry both precede actual release.
  * No Future/protocol completion may free or reuse the pending EB. */
 abort_error=0;hold_frames=true;assert(!send());present=false;capture(WIFI_ACTION_TX_DONE,driver_id);
 before=protocol_calls;poll();assert(protocol_calls==before&&esp32qjs_dpp_tx_held());
 physical.recycling=true;poll();assert(protocol_calls==before&&esp32qjs_dpp_tx_held());
 physical.buffer_present=false;physical.recycling=false;physical.recycled=true;
 poll();assert(protocol_calls==before+1&&!esp32qjs_dpp_tx_held());
 /* Valid RX can enqueue one copied successor before the old EB is returned.
  * Admission must retain bytes after the caller destroys its request. */
 assert(!send());before=submits;assert(!send()&&submits==before&&s_dpp_tx_deferred);
 assert(s_dpp_tx_deferred->request.data[0]==0xa5&&send()==ESP_ERR_NOT_FINISHED);
 esp32_mquickjs_wifi_dpp_result_status_t pending_status={0};esp32qjs_dpp_tx_status_locked(&pending_status);
 assert(pending_status.tx_queued&&pending_status.tx_queued_bytes==allocated_size&&pending_status.reserved_bytes==allocated_size);
 deferred_poll();assert(submits==before&&s_dpp_tx_deferred&&allocations==frees+1);
 physical.buffer_present=false;physical.recycled=true;deferred_poll();
 assert(submits==before+1&&submitted_byte==0xa5&&!s_dpp_tx_deferred&&allocations==frees);
 assert(cancel()==ESP_ERR_NOT_FINISHED);physical.buffer_present=false;assert(!cancel());
 /* Close cancels queued work, including a callback detached before close. */
 assert(!send());assert(!send());struct queued old_deferred={0};
 for(unsigned i=0;i<4;i++)if(queue[i].fn==esp32qjs_dpp_tx_deferred_poll){old_deferred=queue[i];queue[i].fn=NULL;break;}
 assert(old_deferred.fn&&cancel()==ESP_ERR_NOT_FINISHED&&!s_dpp_tx_deferred&&allocations==frees);
 before=submits;old_deferred.fn(old_deferred.high,old_deferred.low);assert(submits==before&&!s_dpp_ctx.pending_tx_op_in_progress);
 physical.buffer_present=false;assert(!cancel());
 /* Copy allocation or dispatcher admission failure retains the old native
  * owner, releases only the new copy, and never starts a second frame. */
 assert(!send());before=submits;allocation_fails=true;assert(send()==ESP_ERR_NO_MEM);
 allocation_fails=false;assert(submits==before&&esp32qjs_dpp_tx_held()&&!s_dpp_tx_deferred);
 queue_full=true;assert(send()==ESP_ERR_NO_MEM&&allocations==frees);queue_full=false;
 assert(submits==before);physical.buffer_present=false;assert(!cancel());hold_frames=false;
 allocated_ticket=UINT64_MAX;before=submits;assert(send()==ESP_ERR_NO_MEM&&submits==before);
 esp32_mquickjs_wifi_dpp_result_status_t status={0};esp32qjs_dpp_tx_status_locked(&status);
 assert(status.tx_operation&&!status.tx_held&&!depth&&!critical);return 0;
}
