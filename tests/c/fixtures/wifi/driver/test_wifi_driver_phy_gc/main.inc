
int main(int argc,char **argv) {
    assert(argc==2);
    bool protocol=!strncmp(argv[1],"protocol",8);
    bool plural=strstr(argv[1],"protocols") || strstr(argv[1],"bandwidths");
    unsigned bands=strstr(argv[1],"Both") ? 3 : strstr(argv[1],"5") ? 2 : 1;
    esp32_mquickjs_wifi_phy_query_t query=protocol ? (plural ? ESP32_MQUICKJS_WIFI_PHY_PROTOCOLS : ESP32_MQUICKJS_WIFI_PHY_PROTOCOL)
        : (plural ? ESP32_MQUICKJS_WIFI_PHY_BANDWIDTHS : ESP32_MQUICKJS_WIFI_PHY_BANDWIDTH);
    esp32_mquickjs_wifi_phy_readback_t snapshot={.bands=bands,
        .protocols={.ghz_2g=WIFI_PROTOCOL_11B|WIFI_PROTOCOL_11G|WIFI_PROTOCOL_11N|WIFI_PROTOCOL_LR,
                    .ghz_5g=WIFI_PROTOCOL_11A|WIFI_PROTOCOL_11AC|WIFI_PROTOCOL_11AX},
        .bandwidths={.ghz_2g=WIFI_BW40,.ghz_5g=WIFI_BW20}};
    int total=1;
    for(int nth=0;nth<=total;++nth) {
        void *heap=malloc(192*1024);JSContext *ctx=JS_NewContext(heap,192*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
        JSGCRef result_ref,item_ref;
        JSValue *result=JS_PushGCRef(ctx,&result_ref),*item=JS_PushGCRef(ctx,&item_ref);
        calls=0;fail_at=nth;inject=true;collect=true;
        *result=driver_phy_to_js(ctx,query,&snapshot);
        inject=false;collect=false;
        if(!nth){total=calls;assert(!JS_IsException(*result));}
        if(JS_IsException(*result)){assert(JS_HasException(ctx));JS_GetException(ctx);}
        else if(!plural) {
            if(protocol) {*item=JS_GetPropertyStr(ctx,*result,"length");int32_t n;assert(!JS_ToInt32(ctx,&n,*item) && n==4);}
            else {int32_t n;assert(!JS_ToInt32(ctx,&n,*result) && n==40);}
        } else for(unsigned band=1;band<=2;band<<=1) {
            const char *key=protocol ? (band==1 ? "ghz2" : "ghz5") : (band==1 ? "ghz2MHz" : "ghz5MHz");
            *item=JS_GetPropertyStr(ctx,*result,key);
            if(!(bands & band))assert(JS_IsUndefined(*item));
            else {
                int32_t n;
                if(protocol)*item=JS_GetPropertyStr(ctx,*item,"length");
                assert(!JS_ToInt32(ctx,&n,*item));
                assert(n==(protocol ? (band==1 ? 4 : 3) : (band==1 ? 40 : 20)));
            }
        }
        wifi_interface_t iface;
        *item=JS_NewString(ctx,"station");assert(tx_rate_interface(ctx,*item,&iface) && iface==WIFI_IF_STA);
        *item=JS_NewString(ctx,"access-point");assert(tx_rate_interface(ctx,*item,&iface) && iface==WIFI_IF_AP);
        *item=JS_NewStringLen(ctx,"station\0",8);assert(!tx_rate_interface(ctx,*item,&iface));JS_GetException(ctx);
        assert(!tx_rate_interface(ctx,JS_NewInt32(ctx,0),&iface));JS_GetException(ctx);
        JS_PopGCRef(ctx,&item_ref);JS_PopGCRef(ctx,&result_ref);
        JS_FreeContext(ctx);assert(!root_count && !native_live);free(heap);
    }
    return 0;
}
