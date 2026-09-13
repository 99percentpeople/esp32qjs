
int main(void){wifi_nan_sync_config_t config={6};
    assert(esp_wifi_nan_sync_start(&config)==ESP_ERR_TIMEOUT);
    assert(wait_ticks==2000&&start_calls==1&&!depth&&s_nan_ctx.nan_netif==(void *)3);}
