
int main(void){
 services[0]=(struct own_svc_info){.svc_id=1,.svc_hash={1},.pairing={true,true,true}};
 services[1]=(struct own_svc_info){.svc_id=2,.svc_hash={2},.pairing={true,true,true}};
 credential_meta[0].identity=101;credential_meta[1].identity=102;
 s_nan_ctx.num_peer_creds=2;
 for(unsigned i=0;i<2;i++)s_nan_ctx.peer_creds[i]=(wifi_nan_peer_creds_t){.is_valid=true,.service_hash={i?2:1},.npk={0x61+i},.peer_nik={i+1}};
 esp32_mquickjs_wifi_nan_pairing_config_t config={.service_identity=44,.service_id=2,.peer_service_id=9,
  .peer={2,1,2,3,4,5},.verification=true,.initiator=true,.credential_id=101};
 esp32_mquickjs_wifi_nan_pasn_status_t result;
 assert(esp32_mquickjs_wifi_nan_pairing_start(&config,&result)==ESP_ERR_INVALID_ARG&&!allocations&&!native_calls);
 config.credential_id=104;
 assert(esp32_mquickjs_wifi_nan_pairing_start(&config,&result)==ESP_ERR_INVALID_ARG&&!allocations&&!native_calls);
 config.credential_id=102;peer_busy=true;
 assert(esp32_mquickjs_wifi_nan_pairing_start(&config,&result)==ESP_ERR_INVALID_STATE&&!allocations&&!native_calls);
 peer_busy=false;fail_alloc=true;
 assert(esp32_mquickjs_wifi_nan_pairing_start(&config,&result)==ESP_ERR_NO_MEM&&!allocations&&!native_calls);
 fail_alloc=false;fail_native=true;snapshot.identity=29;snapshot.closing=true;
 assert(esp32_mquickjs_wifi_nan_pairing_start(&config,&result)==ESP_ERR_NO_MEM&&!allocations&&!result.identity);
 fail_native=false;
 assert(!esp32_mquickjs_wifi_nan_pairing_start(&config,&result)&&result.identity&&allocations==1);
 assert(esp32_mquickjs_wifi_nan_pairing_service_idle(2)==ESP_ERR_INVALID_STATE);
 assert(esp32_mquickjs_wifi_nan_pairing_service_idle(1)==ESP_OK);
 s_nan_ctx.peer_creds[1].npk[0]=0xff; /* Mutable cache is not the operation's key. */
 wifi_task=true;uint8_t npk[32];size_t len=0;int akmp=0;
 assert(!nan_global_peer_npk_lookup(npk,&len,&akmp)&&npk[0]==0x62);
 assert(!esp32_mquickjs_wifi_nan_pairing_services((uint8_t[6]){2,9},NULL,NULL));
 uint8_t nd_pmk[32]={0x81};
 assert(!nan_app_register_paired_peer(config.peer,0,1,nd_pmk,sizeof(nd_pmk),1));
 NAN_DATA_LOCK();
 assert(esp32_mquickjs_wifi_nan_pairing_cached_peer(2,config.peer)==&s_nan_ctx.paired_peers[0]);
 assert(!esp32_mquickjs_wifi_nan_pairing_cached_peer(1,config.peer));
 native_clock+=1000000;assert(!esp32_mquickjs_wifi_nan_pairing_cached_peer(2,config.peer));
 s_nan_ctx.paired_peers[1]=s_nan_ctx.paired_peers[0];cache_services[1]=1;
 esp32qjs_nan_pairing_reset_service(2);
 assert(!s_nan_ctx.paired_peers[0].valid&&s_nan_ctx.paired_peers[1].valid&&!cache_services[0]);
 s_nan_ctx.paired_peers[0]=s_nan_ctx.paired_peers[1];cache_services[0]=1;
 NAN_DATA_UNLOCK();
 assert(nan_app_register_paired_peer(config.peer,0,1,nd_pmk,sizeof(nd_pmk),1)==ESP_ERR_NO_MEM);
 assert(cache_services[0]==1&&cache_services[1]==1);wifi_task=false;
 unsigned calls=native_calls;
 assert(esp32_mquickjs_wifi_nan_pairing_start(&config,&result)==ESP_ERR_INVALID_STATE&&native_calls==calls);
 release();assert(!allocations&&esp32_mquickjs_wifi_nan_pairing_service_idle(2)==ESP_OK);
 config.verification=false;config.credential_id=0;config.pincode=123456;
 assert(!esp32_mquickjs_wifi_nan_pairing_start(&config,&result));
 assert(!s_esp32qjs_pairing_binding->config.pincode);
 uint8_t nik2[16]={2},new_npk[32]={0x71},hash2[6]={2};uint32_t committed=0;
 wifi_task=true;NAN_DATA_LOCK();nan_app_update_peer_creds(nik2,new_npk,hash2,1);NAN_DATA_UNLOCK();wifi_task=false;
 assert(s_nan_ctx.peer_creds[1].npk[0]==0xff); /* Staging must not overwrite a previous good key. */
 assert(esp32_mquickjs_wifi_nan_pairing_commit(snapshot.identity,&committed)==ESP_ERR_INVALID_STATE&&!committed);
 snapshot.paired=true;snapshot.traffic_pending=true;
 assert(esp32_mquickjs_wifi_nan_pairing_commit(snapshot.identity,&committed)==ESP_ERR_INVALID_STATE&&!committed);
 snapshot.traffic_pending=false;
 next_credential=0;assert(esp32_mquickjs_wifi_nan_pairing_commit(snapshot.identity,&committed)==ESP_ERR_NO_MEM);
 assert(s_nan_ctx.peer_creds[1].npk[0]==0xff);next_credential=201;
 assert(!esp32_mquickjs_wifi_nan_pairing_commit(snapshot.identity,&committed)&&committed==201);
 assert(s_nan_ctx.peer_creds[1].npk[0]==0x71&&!esp32_mquickjs_wifi_nan_pairing_commit(snapshot.identity,&committed)&&committed==201);
 esp32_mquickjs_wifi_nan_credentials_t list;
 assert(!esp32_mquickjs_wifi_nan_pairing_credentials(2,&list)&&list.count==1&&list.entries[0].identity==201);
 assert(!esp32_mquickjs_wifi_nan_pairing_credentials(1,&list)&&list.count==1&&list.entries[0].identity==101);
 release();assert(!allocations);
 config.verification=true;config.pincode=0;config.credential_id=102;
 assert(esp32_mquickjs_wifi_nan_pairing_start(&config,&result)==ESP_ERR_INVALID_ARG); /* Replaced ID. */
 native_clock+=1000000;
 assert(!esp32_mquickjs_wifi_nan_pairing_credentials(2,&list)&&!list.count);
 config.credential_id=201;assert(esp32_mquickjs_wifi_nan_pairing_start(&config,&result)==ESP_ERR_INVALID_ARG); /* Expired. */
 /* Exercise the production cleanup attributes without synthesizing secrets. */
 uint8_t nik[16]={1},key[32]={2},plain[64]={3};
 esp32qjs_pairing_clear_nik(&nik);esp32qjs_pairing_clear_npk(&key);esp32qjs_pairing_clear_plain(&plain);
 assert(!nik[0]&&!key[0]&&!plain[0]);return 0;
}
