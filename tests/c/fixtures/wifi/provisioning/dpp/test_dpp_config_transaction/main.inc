
int main(void){
 esp_dpp_config_data_t row={.connector="JWS",.connector_len=3,.net_access_key_len=2,.c_sign_key_len=2,.akm=1,.curr_chan=6,.net_access_key_expiry=99};
 row.net_access_key[0]=11;row.c_sign_key[0]=22;
 assert(!esp_supp_dpp_set_config(&row)&&driver_ready&&live==4&&s_dpp_config_revision==1);
 struct dpp_conf *old=store.conf;int old_calls=calls;
 assert(!esp_supp_dpp_set_config(&row)&&store.conf==old&&calls==old_calls&&s_dpp_config_revision==1);
 row.net_access_key[0]++;
 for(int nth=1;nth<=4;nth++){
  calls=0;fail_at=nth;assert(esp_supp_dpp_set_config(&row)==ESP_ERR_NO_MEM);
  assert(store.conf==old&&driver_ready&&live==4&&s_dpp_config_revision==1);
 }
 fail_at=0;assert(!esp_supp_dpp_set_config(&row)&&store.conf!=old&&s_dpp_config_revision==2);
 assert(((const uint8_t*)wpabuf_head(store.conf->net_access_key))[0]==12);
 row.c_sign_key[1]=33;assert(!esp_supp_dpp_set_config(&row)&&s_dpp_config_revision==3);
 row.net_access_key_expiry++;assert(!esp_supp_dpp_set_config(&row)&&s_dpp_config_revision==4);
 old=store.conf;row.connector[3]='x';assert(esp_supp_dpp_set_config(&row)==ESP_ERR_INVALID_ARG&&store.conf==old);row.connector[3]=0;
 row.net_access_key_len=129;assert(esp_supp_dpp_set_config(&row)==ESP_ERR_INVALID_ARG&&store.conf==old);row.net_access_key_len=2;
 s_dpp_ctx.dpp_deinit_pending=true;assert(esp_supp_dpp_set_config(NULL)==ESP_ERR_INVALID_STATE&&store.conf==old);s_dpp_ctx.dpp_deinit_pending=false;
 s_dpp_config_revision=UINT64_MAX;row.c_sign_key[0]++;assert(esp_supp_dpp_set_config(&row)==ESP_ERR_NO_MEM&&store.conf==old&&driver_ready);
 s_dpp_config_revision=4;assert(!esp_supp_dpp_set_config(NULL)&&!store.conf&&!live&&!driver_ready);
 uint8_t material[129]={1};struct wpabuf *key=wpabuf_alloc_copy(material,sizeof(material));assert(key);
 struct dpp_authentication auth={.net_access_key=key,.curr_chan=6};
 struct dpp_config_obj conf={.ssid={1},.ssid_len=1,.connector="JWS",.akm=DPP_AKM_DPP,.c_sign_key=key};
 memset(&row,0xff,sizeof(row));assert(esp_dpp_handle_config_obj(&auth,&conf,&row)<0);
 for(size_t i=0;i<sizeof(row);i++)assert(!((uint8_t*)&row)[i]);
 key->n=128;assert(!esp_dpp_handle_config_obj(&auth,&conf,&row)&&row.net_access_key_len==128&&row.c_sign_key_len==128);
 key->n=129;wpabuf_clear_free(key);assert(!live&&!locked);return 0;
}
