
int main(void) {
    /* SDK v2 packed buffer, legacy decoder, AirKiss, and small list records. */
    const size_t sizes[]={2824,420,216,16,1};
    for(size_t i=0;i<sizeof(sizes)/sizeof(sizes[0]);i++) {
        live_size=sizes[i];uint8_t *allocation=malloc(live_size+2);assert(allocation);
        live=allocation+1;memset(live,0xa5,live_size);
        live[-1]=0x71;live[live_size]=0x72;
        psni_info=live_size==16 ? live : (void *)&unrelated_owner;
        esp32qjs_smartconfig_secure_free(live);
        assert(psni_info==(live_size==16 ? NULL : (void *)&unrelated_owner));
        release_backing(allocation);live=NULL;
    }
    assert(queries==5 && scrubs==5 && releases==5);
    esp32qjs_smartconfig_secure_free(NULL);
    assert(queries==5 && scrubs==5 && releases==6);
    assert(psni_info==&unrelated_owner);
    return 0;
}
