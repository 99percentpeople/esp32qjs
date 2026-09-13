
int main(void) {
    void *heap=malloc(128*1024);JSContext *ctx=JS_NewContext(heap,128*1024,&js_stdlib);assert(ctx);
    assert(JS_IsBool(js_wifi_stop_ap(ctx,NULL,0,NULL)));
    JSValue timeout=JS_NewInt32(ctx,17);
    assert(JS_IsBool(js_wifi_stop_ap(ctx,NULL,1,&timeout)));
    timeout=JS_NULL;assert(JS_IsException(js_wifi_stop_ap(ctx,NULL,1,&timeout)));JS_GetException(ctx);
    assert(JS_IsException(js_wifi_stop_ap(ctx,NULL,2,NULL)));JS_GetException(ctx);
    JS_FreeContext(ctx);free(heap);
}
