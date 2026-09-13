
int main(int argc,char **argv){
    assert(argc==4);bool expected=atoi(argv[2]),sentinel=atoi(argv[3]);int total=1;
    for(int nth=0;nth<=total;nth++){
        void *heap=malloc(192*1024);JSContext *ctx=JS_NewContext(heap,192*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
        JSGCRef view_ref,input_ref,error_ref;
        JSValue *view=JS_PushGCRef(ctx,&view_ref),*input=JS_PushGCRef(ctx,&input_ref),*error=JS_PushGCRef(ctx,&error_ref);
        uint8_t *data=heap_caps_malloc(4,1);data[0]=0x30;data[1]=2;data[2]=1;data[3]=0;
        *view=esp32_mquickjs_new_owned_byte_view(ctx,data,4);assert(!JS_IsException(*view));
        assert(!JS_IsException(JS_SetPropertyStr(ctx,JS_GetGlobalObject(ctx),"view",*view)));
        *input=JS_Eval(ctx,argv[1],strlen(argv[1]),"enterprise-capture",JS_EVAL_RETVAL);assert(!JS_IsException(*input));
        esp32_mquickjs_wifi_eap_profile_t *profile=NULL;
        calls=0;fail_at=nth;inject=collect=true;
        bool ok=esp32_mquickjs_wifi_eap_capture(ctx,*input,&profile);
        inject=collect=false;
        if(!nth){total=calls;assert(ok==expected);}else assert(!ok);
        if(!ok){assert(!profile && JS_HasException(ctx));*error=JS_GetException(ctx);if(!nth && sentinel)assert(*error==JS_NewInt32(ctx,12345));}
        else {
            esp32_mquickjs_wifi_eap_input_t native;
            assert(profile && esp32_mquickjs_wifi_eap_profile_view(profile,&native));
            assert(!native.policy.disable_time_check && !native.policy.okc);
            /* Native copy survives subsequent source close and a moving GC. */
            assert(!JS_IsException(js_byte_view_close(ctx,view,0,NULL)));JS_GC(ctx);
            if(native.fields[ESP32_MQUICKJS_WIFI_EAP_CA_CERT].length)
                assert(native.fields[ESP32_MQUICKJS_WIFI_EAP_CA_CERT].data[0]==0x30);
        }
        esp32_mquickjs_wifi_eap_profile_release(profile);
        assert(!profile_allocation && !s_eap_profile_counts.profiles && !s_eap_profile_counts.reserved_bytes);
        assert(!JS_IsException(js_byte_view_close(ctx,view,0,NULL)));
        JS_PopGCRef(ctx,&error_ref);JS_PopGCRef(ctx,&input_ref);JS_PopGCRef(ctx,&view_ref);
        JS_FreeContext(ctx);free(heap);assert(!root_count && !native_live && !critical);
    }
    return 0;
}
