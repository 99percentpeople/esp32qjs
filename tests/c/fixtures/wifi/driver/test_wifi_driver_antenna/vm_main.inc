
static void string_is(JSContext *ctx,JSValue value,const char *expected) {
    JSCStringBuf b;const char *text=JS_ToCString(ctx,value,&b);assert(text && !strcmp(text,expected));
}
static void number_is(JSContext *ctx,JSValue value,int expected) {
    int32_t number;assert(!JS_ToInt32(ctx,&number,value) && number==expected);
}
int main(int argc,char **argv) {
    assert(argc==3);bool gpio=atoi(argv[1]),failed=!strcmp(argv[2],"sdk-error"),arity=!strcmp(argv[2],"arity");
    bool admission=!strcmp(argv[2],"admission"),decode=!strcmp(argv[2],"decode");int total=1;
    for(int nth=0;nth<=total;++nth) {
        void *heap=malloc(96*1024);JSContext *ctx=JS_NewContext(heap,96*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
        JSGCRef result_ref,value_ref,item_ref;
        JSValue *result=JS_PushGCRef(ctx,&result_ref),*value=JS_PushGCRef(ctx,&value_ref),*item=JS_PushGCRef(ctx,&item_ref);
        reset();sdk_error=failed ? 77 : 0;if(admission)s_radio.operation.identity=3;if(decode)stored_ant.rx_ant_mode=99;
        unsigned char saved_radio[sizeof(s_radio)],saved_ant[sizeof(stored_ant)],saved_gpio[sizeof(stored_gpio)];
        memcpy(saved_radio,&s_radio,sizeof(s_radio));memcpy(saved_ant,&stored_ant,sizeof(stored_ant));memcpy(saved_gpio,&stored_gpio,sizeof(stored_gpio));
        calls=0;fail_at=nth;collect=true;inject=true;
        *result=(gpio ? js_wifi_driver_get_antenna_gpio : js_wifi_driver_get_antenna)(ctx,NULL,arity ? 1 : 0,value);
        inject=false;collect=false;
        if(!nth){total=calls;assert(JS_IsException(*result)==(failed || arity || admission || decode));}
        else assert(JS_IsException(*result));
        assert(reads==((arity || admission) ? 0 : 1));
        assert(!memcmp(saved_radio,&s_radio,sizeof(s_radio)) && !memcmp(saved_ant,&stored_ant,sizeof(stored_ant)) && !memcmp(saved_gpio,&stored_gpio,sizeof(stored_gpio)));
        if(JS_IsException(*result)) {
            assert(JS_HasException(ctx));*result=JS_GetException(ctx);
            if(!nth && !arity) {
                *value=JS_GetPropertyStr(ctx,*result,"code");string_is(ctx,*value,"WIFI_DRIVER_READ_FAILED");
                *value=JS_GetPropertyStr(ctx,*result,"operation");string_is(ctx,*value,gpio ? "wifi.driver.getAntennaGpio" : "wifi.driver.getAntenna");
                *result=JS_GetPropertyStr(ctx,*result,"details");
                *value=JS_GetPropertyStr(ctx,*result,"espCode");number_is(ctx,*value,failed ? 77 : admission ? ESP_ERR_INVALID_STATE : ESP_ERR_INVALID_RESPONSE);
                *value=JS_GetPropertyStr(ctx,*result,"stage");string_is(ctx,*value,admission ? "admission" : decode ? "decode" : gpio ? "antenna-gpio" : "antenna");
                *value=JS_GetPropertyStr(ctx,*result,"interface");assert(JS_IsNull(*value));
            }
        } else if(gpio) {
            *result=JS_GetPropertyStr(ctx,*result,"gpios");*value=JS_GetPropertyStr(ctx,*result,"length");number_is(ctx,*value,4);
            for(unsigned i=0;i<4;++i) {
                *item=JS_GetPropertyUint32(ctx,*result,i);*value=JS_GetPropertyStr(ctx,*item,"selected");assert(*value==(i%2 ? JS_TRUE : JS_FALSE));
                *value=JS_GetPropertyStr(ctx,*item,"gpio");number_is(ctx,*value,127-i);
            }
        } else {
            *value=JS_GetPropertyStr(ctx,*result,"rxMode");string_is(ctx,*value,"auto");
            *value=JS_GetPropertyStr(ctx,*result,"txMode");string_is(ctx,*value,"auto");
            *value=JS_GetPropertyStr(ctx,*result,"rxDefault");string_is(ctx,*value,"ant1");
            *value=JS_GetPropertyStr(ctx,*result,"enabledAnt0");number_is(ctx,*value,15);
            *value=JS_GetPropertyStr(ctx,*result,"enabledAnt1");number_is(ctx,*value,14);
        }
        JS_PopGCRef(ctx,&item_ref);JS_PopGCRef(ctx,&value_ref);JS_PopGCRef(ctx,&result_ref);JS_FreeContext(ctx);
        assert(!root_count && !native_live && !locks);free(heap);
    }
    return 0;
}
