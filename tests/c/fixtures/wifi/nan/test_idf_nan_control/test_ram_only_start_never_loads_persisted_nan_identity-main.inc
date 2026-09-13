
int main(void){wifi_nan_sync_config_t config={.channel=6};
 (void)esp_wifi_nan_sync_start(&config);
 assert(!credential_loads&&!credential_erases&&!nik_saves&&s_nan_ctx.own_nik[0]==0x52&&!s_nan_ctx.num_peer_creds);
 config.use_nvs_for_caching=true;(void)esp_wifi_nan_sync_start(&config);
 assert(credential_loads==1&&s_nan_ctx.own_nik[0]==0x41&&!credential_erases);}
