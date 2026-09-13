
static void check_counts(JSContext *ctx,JSValue value,const uint32_t *expected) {
    for(unsigned i=0;i<15;i++) {
        uint32_t n;assert(!JS_ToUint32(ctx,&n,JS_GetPropertyStr(ctx,value,s_wifi_coverage_fields[i]))&&n==expected[i]);
    }
}
int main(void) {
    int total=0;
    for(int nth=0;nth<=total;nth++) {
        void *heap=malloc(512*1024);JSContext *ctx=JS_NewContext(heap,512*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
        JSGCRef rr,ar,ir;JSValue *r=JS_PushGCRef(ctx,&rr),*a=JS_PushGCRef(ctx,&ar),*item=JS_PushGCRef(ctx,&ir);
        capability_calls=0;inject=true;collect=true;calls=0;fail_at=nth;
        *r=js_wifi_diagnostics_idf_api_coverage(ctx,NULL,0,NULL);
        inject=false;collect=false;if(!nth){total=calls;assert(!JS_IsException(*r));}
        else assert(JS_IsException(*r));
        if(JS_IsException(*r)){assert(JS_HasException(ctx));JS_GetException(ctx);}
        else {
            assert(capability_calls==1);
            *item=JS_GetPropertyStr(ctx,*r,"total");check_counts(ctx,*item,s_wifi_coverage_total);
            const char *names[]={"headers","tasks","referenceVariants"};
            const wifi_coverage_row_t *rows[]={s_wifi_coverage_headers,s_wifi_coverage_tasks,s_wifi_coverage_variants};
            unsigned lengths[]={sizeof(s_wifi_coverage_headers)/sizeof(*s_wifi_coverage_headers),sizeof(s_wifi_coverage_tasks)/sizeof(*s_wifi_coverage_tasks),sizeof(s_wifi_coverage_variants)/sizeof(*s_wifi_coverage_variants)};
            for(unsigned group=0;group<3;group++) {
                *a=JS_GetPropertyStr(ctx,*r,names[group]);uint32_t length;
                assert(!JS_ToUint32(ctx,&length,JS_GetPropertyStr(ctx,*a,"length"))&&length==lengths[group]);
                for(unsigned i=0;i<length;i++) {
                    *item=JS_GetPropertyUint32(ctx,*a,i);JSCStringBuf b;
                    const char *name=JS_ToCString(ctx,JS_GetPropertyStr(ctx,*item,"name"),&b);assert(name&&!strcmp(name,rows[group][i].name));
                    *item=JS_GetPropertyStr(ctx,*item,"counts");check_counts(ctx,*item,rows[group][i].counts);
                }
            }
        }
        assert(capability_calls<=1);
        *r=js_wifi_diagnostics_idf_api_coverage(ctx,NULL,1,NULL);assert(JS_IsException(*r));JS_GetException(ctx);
        JS_PopGCRef(ctx,&ir);JS_PopGCRef(ctx,&ar);JS_PopGCRef(ctx,&rr);assert(!root_count&&!native_live);
        JS_FreeContext(ctx);free(heap);
    }
}
