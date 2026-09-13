
#undef JS_GetPropertyStr
#undef JS_GetPropertyUint32
int main(int argc,char **argv) {
    assert(argc==4);bool expected=atoi(argv[2])!=0;int scenario=atoi(argv[3]),total=1;
    for(int nth=0;nth<=total;nth++) {
        void *heap=malloc(128*1024);assert(heap);
        JSContext *ctx=JS_NewContext(heap,128*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
        JSGCRef r;JSValue *root=push_root(ctx,&r);
        *root=JS_Eval(ctx,argv[1],strlen(argv[1]),"options",JS_EVAL_RETVAL);assert(!JS_IsException(*root));
        esp32_mquickjs_wifi_monitor_options_t output,before;
        memset(&output,0xa5,sizeof(output));memcpy(&before,&output,sizeof(output));
        calls=0;fail_at=nth;collect=true;inject=true;
        bool ok=esp32_mquickjs_wifi_monitor_capture_options(ctx,*root,&output);
        inject=false;if(!nth)total=calls;
        assert(ok==(expected && nth==0));
        if(!ok) {
            assert(JS_HasException(ctx) && !memcmp(&output,&before,sizeof(output)));
            (void)JS_GetException(ctx);
        } else {
            assert(!JS_HasException(ctx));
            if(scenario==1) {
                assert(!output.capture.channel && !output.capture.require_power_save_none);
                assert(output.capture.filter.type_mask==15 && output.capture.filter.sample_every==1 && output.capture.filter.valid_only);
                assert(!output.capture.filter.subtype_filter && !output.capture.filter.source.count && !output.require_complete);
                assert(output.pool_capacity==16 && output.queue_capacity==16 && output.snap_length==2048);
            } else if(scenario==2) {
                assert(output.capture.channel==6 && output.capture.require_power_save_none);
                assert(output.capture.filter.type_mask==5 && output.capture.filter.subtype_filter && output.capture.filter.subtype_mask==32769);
                assert(output.capture.filter.source.count==2 && output.capture.filter.source.values[1][5]==2);
                assert(output.capture.filter.destination.count==1 && output.capture.filter.destination.values[0][0]==255);
                assert(output.capture.filter.bssid.count==1 && output.capture.filter.bssid.values[0][0]==170);
                assert(output.capture.filter.minimum_rssi_set && output.capture.filter.minimum_rssi==-128);
                assert(output.capture.filter.sample_every==UINT32_MAX && output.capture.filter.maximum_rate_hz==1000000 && !output.capture.filter.valid_only);
                assert(output.snap_length==16384 && output.pool_capacity==128 && output.queue_capacity==128 && output.require_complete);
            } else if(scenario==3) {
                assert(!output.capture.filter.type_mask && output.capture.filter.subtype_filter && !output.capture.filter.subtype_mask);
            }
        }
        assert(root_count==1 && !native_live);
        pop_root(ctx,&r);assert(!root_count);
        JS_FreeContext(ctx);free(heap);
    }
}
