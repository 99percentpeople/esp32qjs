
static uint64_t binding;
static void (*query_hook)(void);
uint64_t esp32_mquickjs_wifi_radio_eap_identity(void){
    assert(!critical);uint64_t result=binding;
    if(query_hook){void (*hook)(void)=query_hook;query_hook=NULL;hook();}
    return result;
}
