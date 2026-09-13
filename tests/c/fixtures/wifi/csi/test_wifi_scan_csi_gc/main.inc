
int main(void){
    for(unsigned mode=0;mode<2;++mode){
        unsigned total=1;
        for(unsigned nth=0;nth<=total;++nth){
            void *heap=malloc(192*1024);JSContext *ctx=JS_NewContext(heap,192*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
            JSGCRef result_ref,item_ref;JSValue *result=JS_PushGCRef(ctx,&result_ref),*item=JS_PushGCRef(ctx,&item_ref);
            wifi_scan_default_params_t scan=WIFI_SCAN_PARAMS_DEFAULT_CONFIG();
            wifi_csi_config_t native={0};
#if CONFIG_SOC_WIFI_HE_SUPPORT
            native.enable=1;native.acquire_csi_vht=1;native.acquire_csi_force_lltf=1;
            native.acquire_csi_he_stbc_mode=2;native.lltf_bit_mode=0;native.val_scale_cfg=7;
#endif
            calls=0;fail_at=nth;inject=true;collect=true;
            *result=mode==0?driver_scan_parameters_to_js(ctx,&scan):wifi_csi_native_config_to_js(ctx,&native,17);
            inject=false;collect=false;
            if(!nth)total=calls;
            if(JS_IsException(*result)){assert(JS_HasException(ctx));JS_GetException(ctx);}
            else if(mode==0){int32_t number;*item=JS_GetPropertyStr(ctx,*result,"activeMaxMs");
                assert(!JS_ToInt32(ctx,&number,*item) && number==120);}
            else {
#if CONFIG_SOC_WIFI_HE_SUPPORT
                *item=JS_GetPropertyStr(ctx,*result,"capture");
                *result=JS_GetPropertyStr(ctx,*item,"lltfBits");int32_t bits;
                assert(!JS_ToInt32(ctx,&bits,*result) && bits==12);
                *result=JS_GetPropertyStr(ctx,*item,"vht");assert(JS_IsBool(*result) && JS_VALUE_GET_INT(*result));
#else
                assert(0); /* Unsupported targets must never fabricate a config. */
#endif
            }
            const char *text="({activeMinMs:121,activeMaxMs:0,passiveMs:0,homeChannelDwellMs:0})";
            *result=JS_Eval(ctx,text,strlen(text),"scan-input.js",JS_EVAL_RETVAL);assert(!JS_IsException(*result));
            assert(!driver_scan_parameters_capture(ctx,*result,&scan));JS_GetException(ctx);
            text="({activeMinMs:0,activeMaxMs:0,passiveMs:0,homeChannelDwellMs:0})";
            *result=JS_Eval(ctx,text,strlen(text),"scan-input.js",JS_EVAL_RETVAL);assert(!JS_IsException(*result));
            calls=0;fail_at=nth;inject=true;collect=true;
            bool captured=driver_scan_parameters_capture(ctx,*result,&scan);
            inject=false;collect=false;
            if(!nth){assert(captured);if(calls>total)total=calls;}
            if(!captured){assert(JS_HasException(ctx));JS_GetException(ctx);}
            else assert(scan.scan_time.active.min==0 && scan.home_chan_dwell_time==0);

            JS_PopGCRef(ctx,&item_ref);JS_PopGCRef(ctx,&result_ref);
            JS_FreeContext(ctx);assert(!root_count && !native_live);free(heap);
        }
    }
    return 0;
}
