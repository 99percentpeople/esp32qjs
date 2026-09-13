
static int native_writes;static bool native_error;
static JSValue read_property(JSContext *ctx,JSValue obj,const char *key){
 JSGCRef ref;JSValue *root=JS_PushGCRef(ctx,&ref);*root=obj;
 JSValue v=step(ctx)?JS_ThrowOutOfMemory(ctx):JS_GetPropertyStr(ctx,*root,key);
 JS_PopGCRef(ctx,&ref);return v;
}
#define JS_GetPropertyStr read_property
static JSValue wifi_configuration_unsupported(JSContext *ctx){return JS_ThrowTypeError(ctx,"unsupported configuration");}
static int esp32_mquickjs_wifi_radio_set_country_details(const wifi_country_t *requested,
 wifi_country_t *actual,esp32_mquickjs_wifi_radio_config_result_t *result){
 assert(native_writes++==0);assert(requested->cc[0]=='T'&&requested->cc[1]=='W');
 assert(requested->schan==1&&requested->nchan==13&&requested->max_tx_power==0);
 *actual=*requested;actual->max_tx_power=17;
 *result=(esp32_mquickjs_wifi_radio_config_result_t){.stage="country-config",.error=native_error?-77:0,
  .mutation_attempted=true,.persistent_mutation_possible=true};return result->error;
}
