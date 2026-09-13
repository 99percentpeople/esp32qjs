
int main(int argc,char **argv){
    assert(argc==3);int enabled=atoi(argv[1]);mode=atoi(argv[2]);int total=0;
    const char *names[]={"memory","wifi","monitor","vendorIe","csi","ftmInitiator","ftmResponder","twt","enterprise","smartConfig","wpsStation","wpsAccessPoint","dpp"};
    for(int nth=0;nth<=total;nth++){
        void *heap=malloc(256*1024);JSContext *ctx=JS_NewContext(heap,256*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
        memset(provider_calls,0,sizeof(provider_calls));queue_calls=0;clock_us=0;
        JSGCRef result_ref,item_ref;JSValue *r=JS_PushGCRef(ctx,&result_ref),*item=JS_PushGCRef(ctx,&item_ref);
        inject=true;collect=true;calls=0;fail_at=nth;
        *r=js_wifi_diagnostics_snapshot(ctx,NULL,mode==1?1:0,NULL);
        inject=false;collect=false;
        if(!nth){total=calls;assert(JS_IsException(*r)==(mode!=0));}
        if(JS_IsException(*r)){assert(JS_HasException(ctx));JS_GetException(ctx);}
        else {
            assert(queue_calls==1);
            for(unsigned i=0;i<13;i++){
                *item=JS_GetPropertyStr(ctx,*r,names[i]);
                if(i>=4&&!enabled){assert(JS_IsNull(*item)&&!provider_calls[i]);continue;}
                assert(provider_calls[i]==1 && JS_GetClassID(ctx,*item)==JS_CLASS_OBJECT);
                uint32_t n;assert(!JS_ToUint32(ctx,&n,JS_GetPropertyStr(ctx,*item,"source"))&&n==i);
                JSCStringBuf b;const char *text=JS_ToCString(ctx,JS_GetPropertyStr(ctx,*item,"marker"),&b);assert(text&&!strcmp(text,"live"));
            }
            *item=JS_GetPropertyStr(ctx,*r,"runtimeQueues");uint32_t n;
            assert(!JS_ToUint32(ctx,&n,JS_GetPropertyStr(ctx,*item,"dropped"))&&n==7);
            assert(!JS_ToUint32(ctx,&n,JS_GetPropertyStr(ctx,*item,"highWater"))&&n==5);
            *item=JS_GetPropertyStr(ctx,*r,"counterReset");
            assert(!JS_ToUint32(ctx,&n,JS_GetPropertyStr(ctx,*item,"count"))&&n==0);
            assert(JS_IsNull(JS_GetPropertyStr(ctx,*item,"startedUs")));
            assert(!JS_ToUint32(ctx,&n,JS_GetPropertyStr(ctx,*r,"finishedUs"))&&n==2);
        }
        if(mode){assert(!queue_calls);for(unsigned i=0;i<14;i++)assert(!provider_calls[i]);}
        JS_PopGCRef(ctx,&item_ref);JS_PopGCRef(ctx,&result_ref);assert(!root_count&&!native_live);
        JS_FreeContext(ctx);free(heap);
    }
}
