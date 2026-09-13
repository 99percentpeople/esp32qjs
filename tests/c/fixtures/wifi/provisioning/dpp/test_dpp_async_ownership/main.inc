
static void esp_dpp_rx_action(void *p,void*u){(void)u;calls++;assert(esp32qjs_dpp_async_drain_locked()==ESP_ERR_NOT_FINISHED);os_free(p);}
static void esp_dpp_bootstrap_gen(void*p,void*u){(void)p;(void)u;bootstrap_calls++;}
static void nop(void*p,void*u){(void)p;(void)u;calls++;}
int main(void){
 s_dpp_generation=1;
 fail_at=1;assert(esp32qjs_dpp_async_register(0,0,nop,NULL,NULL)==-1&&!live);fail_at=0;
 queue_fail=true;assert(esp32qjs_dpp_async_register(0,0,nop,NULL,NULL)==-1&&!live);queue_fail=false;
 struct action_rx_param *rx=os_zalloc(sizeof(*rx)+4);rx->frm_len=4;
 assert(!esp32qjs_dpp_async_register(0,0,esp_dpp_rx_action,rx,NULL));
 struct queued old=detach_first();
 assert(esp32qjs_dpp_async_cancel(esp_dpp_rx_action,rx,NULL)==0&&live==2&&s_dpp_async_count==1);
 old.fn(old.a,old.b);assert(!calls&&!live&&!s_dpp_async_count);
 assert(!esp32qjs_dpp_async_register(0,0,nop,NULL,NULL));old.fn(old.a,old.b);assert(!calls&&s_dpp_async_count==1);
 struct queued next=detach_first();next.fn(next.a,next.b);assert(calls==1&&!live);
 for(unsigned i=0;i<ESP32QJS_DPP_ASYNC_MAX;i++)assert(!esp32qjs_dpp_async_register(1,0,nop,NULL,NULL));
 assert(esp32qjs_dpp_async_register(0,0,nop,NULL,NULL)==-1);
 assert(esp32qjs_dpp_async_cancel(nop,ELOOP_ALL_CTX,ELOOP_ALL_CTX)==ESP32QJS_DPP_ASYNC_MAX&&!live);
 rx=os_zalloc(sizeof(*rx));assert(!esp32qjs_dpp_async_register(0,0,esp_dpp_rx_action,rx,NULL));
 next=detach_first();next.fn(next.a,next.b);assert(calls==2&&!live&&!s_dpp_async_active);
 rx=os_zalloc(sizeof(*rx));assert(!esp32qjs_dpp_async_register(0,0,esp_dpp_rx_action,rx,NULL));
 next=detach_first();s_dpp_generation++;next.fn(next.a,next.b);assert(calls==2&&!live);
 rx=os_zalloc(sizeof(*rx));assert(!esp32qjs_dpp_async_register(0,0,esp_dpp_rx_action,rx,NULL));
 s_dpp_ctx.dpp_deinit_pending=true;next=detach_first();next.fn(next.a,next.b);assert(!live);
 assert(esp32qjs_dpp_async_register(0,0,nop,NULL,NULL)==-1);s_dpp_ctx.dpp_deinit_pending=false;
 assert(!esp32qjs_dpp_async_register(0,0,esp_dpp_bootstrap_gen,(void*)1,(void*)2));
 s_dpp_ctx.dpp_deinit_pending=true;next=detach_first();next.fn(next.a,next.b);assert(bootstrap_calls==1&&!live);
 s_dpp_ctx.dpp_deinit_pending=false;s_dpp_async_last_ticket=UINT64_MAX;
 assert(esp32qjs_dpp_async_register(0,0,nop,NULL,NULL)==-1&&!live&&!locked);
 return 0;
}
