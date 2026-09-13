/* Two distinct SDK pool descriptors: initial Raw TX and HMAC replacement. */
static u32 replacement_eb[64], replacement_tx_info[64], initial_lldesc[3], replacement_lldesc[3];
static u8 replacement_bytes[1500];
static unsigned pool_allocations, cache_recycles, identity_transfers;
void *ic_ebuf_alloc(const void *bytes,int kind,int length) {
    if(kind!=1 || length<24 || length>1500) __builtin_trap();
    unsigned replacement=pool_allocations++&1;
    u32 *descriptor=replacement ? replacement_eb : eb;
    u32 *native_info=replacement ? replacement_tx_info : tx_info;
    u32 *lldesc=replacement ? replacement_lldesc : initial_lldesc;
    u8 *payload=replacement ? replacement_bytes : copied;
    memset(descriptor,0,64*sizeof(u32));memset(native_info,0,64*sizeof(u32));
    for(int i=0;i<length;i++) payload[i]=((const u8 *)bytes)[i];
    lldesc[0]=(u32)length;lldesc[1]=(u32)payload;
    descriptor[1]=(u32)lldesc;descriptor[4]=(u32)payload;descriptor[EB_TXINFO_WORD]=(u32)native_info;
    copied_length=length;
    return descriptor;
}
