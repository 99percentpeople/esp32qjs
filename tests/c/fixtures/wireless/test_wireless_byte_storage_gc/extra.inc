
static int released,destroyed,opened,closed,open_mode;
static uint8_t bytes[]={1,2,3,4};
static void release(void *p) { assert(p==bytes);released++; }
static void destroy(JSContext *ctx,void *p) { (void)ctx;assert(p==bytes);destroyed++; }
static bool next(JSContext *ctx,void *p,esp32_mquickjs_byte_span_t *out) {
    (void)ctx;out->data=p;out->length=4;return true;
}
static void close_inner(JSContext *ctx,void *p) { (void)ctx;assert(p==bytes);closed++; }
static bool open_inner(JSContext *ctx,JSValue value,void *p,esp32_mquickjs_byte_span_source_t *out,JSValue *error) {
    (void)value;opened++;
    if(open_mode==1) { *error=JS_ThrowInternalError(ctx,"opener failed");return false; }
    out->opaque=p;out->next=open_mode==2 ? NULL : next;out->close=close_inner;return true;
}
static size_t length(void *p) { assert(p==bytes);return 4; }
static const esp32_mquickjs_byte_span_source_object_ops_t ops={.open=open_inner,.destroy=destroy,.known_length=length};
