
static unsigned rx_calls,recycles,null_calls,setup_calls,inactive_calls;
static void inside_native(void){assert(esp32_mquickjs_wifi_nan_sdk_service_quiesce(0)==ESP_ERR_TIMEOUT);}
void __real_nan_rx_naf(void *b,void *f,void *m){assert(b==(void*)1&&f==(void*)2&&m==(void*)3);inside_native();++rx_calls;}
void ic_ebuf_recycle_rx(void *b){assert(b==(void*)1);++recycles;}
void __real_nan_nulldata_txcb(void *b){assert(b==(void*)1);inside_native();++null_calls;}
void __real_nan_ndp_setup_timeout_process(void *p){assert(p==(void*)4);inside_native();++setup_calls;}
void __real_nan_ndp_inactivity_timeout_process(void *p){assert(p==(void*)5);inside_native();++inactive_calls;}
