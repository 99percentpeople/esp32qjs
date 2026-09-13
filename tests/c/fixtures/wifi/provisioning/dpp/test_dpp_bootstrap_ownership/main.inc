
static const char *key="30310201010420" "1111111111111111111111111111111111111111111111111111111111111111" "a00a06082a8648ce3d030107";
static int submit(void){return esp_supp_dpp_bootstrap_gen("1,6,11",DPP_BOOTSTRAP_QR_CODE,key,"name key=untrusted type=pkex");}
static void dispatch(void){assert(scheduled);void (*fn)(void*,void*)=scheduled;scheduled=NULL;fn(scheduled_high,scheduled_low);}
int main(void){
 assert(!esp32qjs_dpp_bootstrap_validate("1,6,11",key,"valid"));
 assert(esp32qjs_dpp_bootstrap_validate("1,1",key,NULL)==ESP_ERR_DPP_INVALID_LIST);
 assert(!calls&&!live&&!scheduled&&!locked);
 s_dpp_ctx.dpp_global=&global;s_dpp_ctx.id=7;s_dpp_ctx.bootstrap_done=true;s_dpp_ctx.bootstrap_params.num_chan=1;s_dpp_ctx.bootstrap_params.chan_list[0]=3;
 const char *bad[]={"","1,","1,6,x","1,1","1 6","0","256","12345678901234567890","1,2,3,4,5,6"};
 for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);i++)assert(esp_supp_dpp_bootstrap_gen(bad[i],0,key,NULL)==ESP_ERR_DPP_INVALID_LIST);
 assert(esp_supp_dpp_bootstrap_gen("1",1,key,NULL)==ESP_ERR_NOT_SUPPORTED);
 assert(esp_supp_dpp_bootstrap_gen("1",0,"123",NULL)==ESP_ERR_INVALID_ARG);
 assert(esp_supp_dpp_bootstrap_gen("1",0,key,"bad;I:value")==ESP_ERR_INVALID_ARG);
 assert(!live&&!calls&&!scheduled&&s_dpp_ctx.id==7);
 mac_error=42;assert(submit()==42);mac_error=0;
 calls=0;fail_at=1;assert(submit()==ESP_ERR_NO_MEM&&!live&&!scheduled);fail_at=0;
 queue_fail=true;assert(submit()==ESP_ERR_NO_MEM&&!live&&!scheduled&&s_dpp_ctx.id==7);queue_fail=false;
 assert(!submit()&&s_dpp_bootstrap_job&&live==1&&s_dpp_ctx.id==7);
 assert(submit()==ESP_ERR_INVALID_STATE);
 void *old_high=scheduled_high,*old_low=scheduled_low;
 cancel_fail=true;dpp_api_lock();assert(esp32qjs_dpp_bootstrap_cancel_locked()==ESP_ERR_INVALID_STATE&&s_dpp_bootstrap_job);dpp_api_unlock();
 cancel_fail=false;dpp_api_lock();assert(!esp32qjs_dpp_bootstrap_cancel_locked());dpp_api_unlock();assert(!live&&!scheduled);
 assert(!submit());esp_dpp_bootstrap_gen(old_high,old_low);assert(s_dpp_bootstrap_job&&scheduled&&!starts);
 post_error=43;dispatch();assert(!live&&!s_dpp_bootstrap_job&&s_dpp_ctx.id>100&&s_dpp_ctx.bootstrap_params.chan_list[0]==1);post_error=0;
 calls=0;fail_at=2;assert(!submit());dispatch();assert(!live&&s_dpp_ctx.id>100);fail_at=0;
 assert(!submit());dispatch();assert(!live&&s_dpp_ctx.id>100&&s_dpp_ctx.bootstrap_params.num_chan==3);
 assert(!failures&&result.status.observation_drops==2&&!locked&&!scheduled);
 s_dpp_bootstrap_last_identity=UINT64_MAX;assert(submit()==ESP_ERR_NO_MEM&&!live);
 return 0;
}
