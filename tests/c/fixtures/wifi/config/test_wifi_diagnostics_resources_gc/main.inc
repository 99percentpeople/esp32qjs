
static uint64_t number(JSContext *ctx,JSValue object,const char *name){
    double value;assert(!JS_ToNumber(ctx,&value,JS_GetPropertyStr(ctx,object,name)));return (uint64_t)value;
}
static void string_is(JSContext *ctx,JSValue object,const char *name,const char *expected){
    JSCStringBuf buf;const char *value=JS_ToCString(ctx,JS_GetPropertyStr(ctx,object,name),&buf);
    assert(value && !strcmp(value,expected));
}
int main(int argc,char **argv){
    assert(argc==3);state=atoi(argv[1]);bool moving=atoi(argv[2]);
    for(int kind=0;kind<2;kind++){
        int total=0;
        for(int nth=0;nth<=total;nth++){
            void *heap=malloc(256*1024);JSContext *ctx=JS_NewContext(heap,256*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
            memset(&s_wifi_csi,0,sizeof(s_wifi_csi));s_wifi_csi.generation=71;
            s_wifi_csi.lifecycle=state==0?WIFI_CSI_RUNNING:WIFI_CSI_CLOSED;
            s_wifi_csi.close_requested=state!=0;observations=locked=0;
            JSGCRef result_ref,array_ref,item_ref;
            JSValue *result=JS_PushGCRef(ctx,&result_ref),*array=JS_PushGCRef(ctx,&array_ref),*item=JS_PushGCRef(ctx,&item_ref);
            inject=true;collect=moving;fail_at=nth;calls=0;
            *result=kind?esp32_mquickjs_wifi_csi_diagnostics(ctx):esp32_mquickjs_wifi_monitor_diagnostics(ctx);
            inject=false;collect=false;assert(!locked);
            if(!nth)total=calls;
            if(nth){assert(JS_IsException(*result)&&JS_HasException(ctx));JS_GetException(ctx);}
            else {
                assert(!JS_IsException(*result));
                if(kind){
                    assert(observations==1 && number(ctx,*result,"generation")==71);
                    string_is(ctx,*result,"state",state==0?"running":"closed");
                    assert(number(ctx,*result,"reservedSlots")== (state==3?0:32));
                    assert(number(ctx,*result,"storageBytes")== (state==3?0:4124));
                    assert(number(ctx,*result,"activeStorageBytes")== (state==0?512:0));
                    assert(number(ctx,*result,"retainedStorageBytes")== (state==3?0:state==1?4124:3612));
                    assert(number(ctx,*result,"pendingStorageBytes")== (state==2||state==4?512:0));
                    assert(number(ctx,*result,"controlBytes")==sizeof(s_wifi_csi)+sizeof(s_wifi_csi_store)+sizeof(s_wifi_csi_store_lock)+128);
                    *array=JS_GetPropertyStr(ctx,*result,"generations");assert(number(ctx,*array,"length")== (state==3?0:8));
                    for(unsigned i=0;state!=3 && i<8;i++){
                        *item=JS_GetPropertyUint32(ctx,*array,i);
                        assert(number(ctx,*item,"generation")==71+i && number(ctx,*item,"storageBytes")==512+i);
                        bool unavailable=i==0 && (state==2||state==4);
                        if(unavailable){assert(JS_IsNull(JS_GetPropertyStr(ctx,*item,"freeSlots")));assert(JS_IsNull(JS_GetPropertyStr(ctx,*item,"callbacks")));}
                        else {assert(number(ctx,*item,"freeSlots")==3 && number(ctx,*item,"callbacks")==8+i);}
                    }
                } else {
                    assert(observations==1 && number(ctx,*result,"unavailableSessions")==1);
                    *array=JS_GetPropertyStr(ctx,*result,"sessions");assert(number(ctx,*array,"length")==8);
                    for(unsigned i=0;i<8;i++){
                        *item=JS_GetPropertyUint32(ctx,*array,i);
                        assert(number(ctx,*item,"generation")==100+i && number(ctx,*item,"poolBytes")==512+i);
                        assert(number(ctx,*item,"callbacks")== (UINT64_C(1)<<40)+i);
                        assert(JS_GetPropertyStr(ctx,*item,"closed")==JS_NewBool(i>0));
                    }
                }
            }
            JS_PopGCRef(ctx,&item_ref);JS_PopGCRef(ctx,&array_ref);JS_PopGCRef(ctx,&result_ref);
            assert(!root_count&&!native_live);JS_FreeContext(ctx);free(heap);
        }
    }
}
