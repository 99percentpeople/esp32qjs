
static JSValue twt_get_property(JSContext *ctx,JSValue object,const char *key){
    JSGCRef ref;JSValue *root=JS_PushGCRef(ctx,&ref);*root=object;
    JSValue result=step(ctx)?JS_ThrowOutOfMemory(ctx):JS_GetPropertyStr(ctx,*root,key);
    JS_PopGCRef(ctx,&ref);return result;
}
#define JS_GetPropertyStr twt_get_property
