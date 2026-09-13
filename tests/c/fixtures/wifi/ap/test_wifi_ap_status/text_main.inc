
int main(void) {
    void *heap=malloc(128*1024);JSContext *ctx=JS_NewContext(heap,128*1024,&js_stdlib);assert(ctx);
    static const struct { uint8_t data[6];size_t length;bool valid; } cases[]={
        {{0},0,true},{{'A',0,'B'},3,true},{{0xc3,0xa9},2,true},
        {{0xf0,0x9f,0x98,0x80},4,true},{{0xff},1,false},
        {{0xc0,0x80},2,false},{{0xe2,0x82,0xac},2,false},
        {{0xed,0xa0,0x80},3,false},{{0xf4,0x90,0x80,0x80},4,false},
        {{0xe2,0x28,0xa1},3,false},{{0x80},1,false},{{0xe2,0x82,0xac,0xff},3,true},
    };
    for(size_t i=0;i<sizeof(cases)/sizeof(cases[0]);i++) {
        JSValue text=esp32_mquickjs_wifi_ssid_text(ctx,cases[i].data,cases[i].length);
        assert(!JS_IsException(text));assert(JS_IsString(ctx,text)==cases[i].valid);
        if(!cases[i].valid)assert(JS_IsNull(text));
    }
    JS_FreeContext(ctx);free(heap);
}
