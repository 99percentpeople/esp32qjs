
static JSValue monitor_get_property(JSContext *ctx,JSValue object,const char *key) {
    JSGCRef r;JSValue *root=JS_PushGCRef(ctx,&r);*root=object;
    JSValue result=step(ctx)?JS_ThrowOutOfMemory(ctx):JS_GetPropertyStr(ctx,*root,key);
    JS_PopGCRef(ctx,&r);return result;
}
static JSValue monitor_get_index(JSContext *ctx,JSValue object,uint32_t index) {
    JSGCRef r;JSValue *root=JS_PushGCRef(ctx,&r);*root=object;
    JSValue result=step(ctx)?JS_ThrowOutOfMemory(ctx):JS_GetPropertyUint32(ctx,*root,index);
    JS_PopGCRef(ctx,&r);return result;
}
#define JS_GetPropertyStr monitor_get_property
#define JS_GetPropertyUint32 monitor_get_index
