
int main(void) {
    slot.pairing_action=BLE_SM_IOACT_NUMCMP;
    JSValue self=1,args[]={7,9};
    assert(js_ble_connection_respond_pairing(NULL,&self,2,args)==JS_EXCEPTION);
    assert(!injected && slot.pairing_request_id==7);
    args[1]=JS_FALSE;
    assert(js_ble_connection_respond_pairing(NULL,&self,2,args)==JS_TRUE);
    assert(injected==1 && zeroed==1 && !slot.pairing_request_id && !locks);
}
