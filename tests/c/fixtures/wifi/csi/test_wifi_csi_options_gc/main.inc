
#undef JS_GetPropertyStr
int main(int argc,char **argv){
    assert(argc==5);bool expected=atoi(argv[2]);uint32_t pool=atoi(argv[3]),queue=atoi(argv[4]);int total=0;
    for(int nth=0;nth<=total;nth++){
        void *heap=malloc(256*1024);JSContext *ctx=JS_NewContext(heap,256*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
        JSGCRef input_ref,result_ref,part_ref;
        JSValue *input=JS_PushGCRef(ctx,&input_ref),*result=JS_PushGCRef(ctx,&result_ref),*part=JS_PushGCRef(ctx,&part_ref);
        *input=JS_Eval(ctx,argv[1],strlen(argv[1]),"options",JS_EVAL_RETVAL);assert(!JS_IsException(*input));
        wifi_csi_options_t options;esp32_mquickjs_wifi_csi_resources_t resources={0};
        esp32_mquickjs_wifi_csi_allocator_t allocator={pool_calloc, pool_malloc, pool_free, NULL, NULL};
        calls=0;fail_at=nth;inject=true;collect=true;
        bool ok=wifi_csi_parse_open_options(ctx,*input,&options);
        if(ok){
            *result=wifi_csi_requested_to_js(ctx,&options);ok=!JS_IsException(*result);
            if(ok){
                wifi_csi_options_t replay;
                ok=wifi_csi_parse_open_options(ctx,*result,&replay);
                if(ok)assert(!memcmp(&options,&replay,sizeof(options)));
            }
            if(ok)ok=esp32_mquickjs_wifi_csi_resources_init(&resources,1,options.pool_capacity,128,esp32_mquickjs_wifi_csi_packet_capacity(&options.packet),&allocator);
        }
        inject=false;collect=false;
        if(!nth){total=calls;assert(ok==expected);}
        else assert(!ok);
        if(ok){
            *part=JS_GetPropertyStr(ctx,*result,"filter");
            JSValue frames=JS_GetPropertyStr(ctx,*part,"frames");
            assert(JS_IsUndefined(frames)==!options.filter.frame_filter);
            if(options.filter.frame_filter){
                uint16_t masks[4]={0};
                assert(esp32_mquickjs_wifi_frame_filter_parse(ctx,frames,masks));
                assert(!memcmp(masks,options.filter.frame_subtype_masks,sizeof(masks)));
                /* Requested descriptors are fresh snapshots, not native storage. */
                assert(!JS_IsException(JS_SetPropertyUint32(ctx,frames,0,JS_NULL)));
                assert(!memcmp(masks,options.filter.frame_subtype_masks,sizeof(masks)));
            }
            assert(JS_IsUndefined(JS_GetPropertyStr(ctx,*part,"frameTypes")));
            assert(JS_IsUndefined(JS_GetPropertyStr(ctx,*part,"frameSubtypes")));
            assert(options.pool_capacity==pool && options.queue_capacity==queue && resources.capacity==pool);
            assert(resources.slots[pool-1].payload==resources.payload_storage+(pool-1)*(128+esp32_mquickjs_wifi_csi_packet_capacity(&options.packet)));
            assert(esp32_mquickjs_native_pool_available(&resources.pool)==pool);
            *part=JS_GetPropertyStr(ctx,*result,"buffering");uint32_t n;
            assert(!JS_ToUint32(ctx,&n,JS_GetPropertyStr(ctx,*part,"poolCapacity"))&&n==pool);
            assert(!JS_ToUint32(ctx,&n,JS_GetPropertyStr(ctx,*part,"queueCapacity"))&&n==queue);
            *part=JS_GetPropertyStr(ctx,*result,"source");assert(JS_GetClassID(ctx,*part)==JS_CLASS_OBJECT);
            if(options.source==WIFI_CSI_SOURCE_ASSOCIATED)assert(JS_IsUndefined(JS_GetPropertyStr(ctx,*part,"channel")));
        }
        if(JS_HasException(ctx))JS_GetException(ctx);
        if(resources.slots)assert(esp32_mquickjs_wifi_csi_resources_deinit(&resources));
        JS_PopGCRef(ctx,&part_ref);JS_PopGCRef(ctx,&result_ref);JS_PopGCRef(ctx,&input_ref);
        assert(!native_live && !root_count);JS_FreeContext(ctx);free(heap);
    }
}
