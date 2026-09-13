
int main(void) {
    for(mutate=1;mutate<=4;mutate++) {
        slot=(ble_connection_slot_t){true,1,7,BLE_SM_IOACT_INPUT,123456,100,9};
        converted=injected=zeroed=0;
        JSValue self=1,args[]={7,123456};
        assert(js_ble_connection_respond_pairing(NULL,&self,2,args)==JS_EXCEPTION);
        assert(!injected && !locks && zeroed==1);
    }
}
