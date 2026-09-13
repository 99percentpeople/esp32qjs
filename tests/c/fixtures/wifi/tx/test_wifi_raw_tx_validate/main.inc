
typedef esp32_mquickjs_wifi_raw_tx_validation_policy_t policy_t;
typedef esp32_mquickjs_wifi_raw_tx_validated_frame_t frame_t;
_Alignas(frame_t) static uint8_t bytes[1501];
static policy_t policy={.interface=ESP32_MQUICKJS_WIFI_RAW_TX_STATION,.driver_sequence=true};
static void control(uint16_t fc) {memset(bytes,0,sizeof(bytes));bytes[0]=fc;bytes[1]=fc>>8;bytes[22]=0x34;bytes[23]=0x12;}
static void rejects(uint16_t fc,size_t length,esp32_mquickjs_wifi_raw_tx_validation_t reason) {
    control(fc);uint8_t before[sizeof(bytes)];memcpy(before,bytes,sizeof(bytes));
    frame_t out,old;memset(&out,0xa5,sizeof(out));memcpy(&old,&out,sizeof(out));
    assert(esp32_mquickjs_wifi_raw_tx_validate(bytes,length,&policy,&out)==reason);
    assert(!memcmp(&old,&out,sizeof(out)) && !memcmp(before,bytes,sizeof(bytes)));
}
static void accepts(uint16_t fc,size_t length,unsigned header,esp32_mquickjs_wifi_raw_tx_frame_type_t type) {
    control(fc);uint8_t before[sizeof(bytes)];memcpy(before,bytes,sizeof(bytes));frame_t out;
    assert(esp32_mquickjs_wifi_raw_tx_validate(bytes,length,&policy,&out)==ESP32_MQUICKJS_WIFI_RAW_TX_VALID);
    assert(out.frame_type==type && out.byte_length==length && out.header.header_length==header);
    assert(out.header.sequence_control==0x1234 && !memcmp(before,bytes,sizeof(bytes)));
}
int main(void) {
    /* All protocol type/subtype combinations; no unsupported null/CF/QoS claim. */
    for(unsigned type=0;type<4;type++)for(unsigned subtype=0;subtype<16;subtype++) {
        bool allowed=(type==0 && (subtype==4 || subtype==5 || subtype==8 || subtype==13)) || (type==2 && subtype==0);
#if defined(ESP32_MQUICKJS_RAW_TX_EXTENDED_MANAGEMENT) && ESP32_MQUICKJS_RAW_TX_EXTENDED_MANAGEMENT
        if(type==0) allowed=subtype!=7 && subtype!=15;
#endif
        frame_t out;control((type<<2)|(subtype<<4));
        assert((esp32_mquickjs_wifi_raw_tx_validate(bytes,64,&policy,&out)==ESP32_MQUICKJS_WIFI_RAW_TX_VALID)==allowed);
    }
    accepts(0x80,24,24,ESP32_MQUICKJS_WIFI_RAW_TX_BEACON);
    accepts(0x40,1500,24,ESP32_MQUICKJS_WIFI_RAW_TX_PROBE_REQUEST);
    accepts(0x50,36,24,ESP32_MQUICKJS_WIFI_RAW_TX_PROBE_RESPONSE);
    accepts(0xd0,24,24,ESP32_MQUICKJS_WIFI_RAW_TX_ACTION);
    accepts(0x08,24,24,ESP32_MQUICKJS_WIFI_RAW_TX_NON_QOS_DATA);
    accepts(0x0308,30,30,ESP32_MQUICKJS_WIFI_RAW_TX_NON_QOS_DATA);
    rejects(0x0308,29,ESP32_MQUICKJS_WIFI_RAW_TX_INVALID_HEADER);
    accepts(0x80d0,28,28,ESP32_MQUICKJS_WIFI_RAW_TX_ACTION);
    rejects(0x80d0,27,ESP32_MQUICKJS_WIFI_RAW_TX_INVALID_HEADER);
    rejects(0x0180,64,ESP32_MQUICKJS_WIFI_RAW_TX_INVALID_HEADER);
    for(unsigned version=1;version<4;version++)rejects(0x08|version,24,ESP32_MQUICKJS_WIFI_RAW_TX_INVALID_HEADER);
    rejects(0x4008,24,ESP32_MQUICKJS_WIFI_RAW_TX_PROTECTED_FRAME);
    rejects(0x4080,64,ESP32_MQUICKJS_WIFI_RAW_TX_PROTECTED_FRAME);
    rejects(0x0088,64,ESP32_MQUICKJS_WIFI_RAW_TX_QOS_FRAME);
    rejects(0x0008,1501,ESP32_MQUICKJS_WIFI_RAW_TX_INVALID_LENGTH);
    rejects(0x0008,SIZE_MAX,ESP32_MQUICKJS_WIFI_RAW_TX_INVALID_LENGTH);
    policy.driver_sequence=false;
    accepts(0x08,24,24,ESP32_MQUICKJS_WIFI_RAW_TX_NON_QOS_DATA);
    policy.connection_active=true;
    rejects(0x08,24,ESP32_MQUICKJS_WIFI_RAW_TX_DRIVER_SEQUENCE_REQUIRED);
    rejects(0x80,24,ESP32_MQUICKJS_WIFI_RAW_TX_DRIVER_SEQUENCE_REQUIRED);
    policy.driver_sequence=true;policy.associated_path=true;
    for(unsigned interface=0;interface<2;interface++) {
        policy.interface=interface;
        for(unsigned ds=0;ds<4;ds++) {
            frame_t out;control(0x08|(ds<<8));
            assert((esp32_mquickjs_wifi_raw_tx_validate(bytes,32,&policy,&out)==ESP32_MQUICKJS_WIFI_RAW_TX_VALID)==(ds==(interface?2:1)));
        }
        for(unsigned flag=0x800;flag<=0x2000;flag<<=1) {
            rejects(0x08|(interface?0x200:0x100)|flag,32,ESP32_MQUICKJS_WIFI_RAW_TX_CONNECTION_FLAGS);
            rejects(0xd0|flag,32,ESP32_MQUICKJS_WIFI_RAW_TX_CONNECTION_FLAGS);
        }
    }
    policy.associated_path=false;
    accepts(0x3808,24,24,ESP32_MQUICKJS_WIFI_RAW_TX_NON_QOS_DATA); /* unrelated addresses */
    policy.connection_active=false;
    frame_t out;control(0x08);
    assert(esp32_mquickjs_wifi_raw_tx_validate(NULL,24,&policy,&out)==ESP32_MQUICKJS_WIFI_RAW_TX_INVALID_ARGUMENT);
    assert(esp32_mquickjs_wifi_raw_tx_validate(bytes,24,NULL,&out)==ESP32_MQUICKJS_WIFI_RAW_TX_INVALID_ARGUMENT);
    assert(esp32_mquickjs_wifi_raw_tx_validate(bytes,24,&policy,NULL)==ESP32_MQUICKJS_WIFI_RAW_TX_INVALID_ARGUMENT);
    assert(esp32_mquickjs_wifi_raw_tx_validate(bytes,24,&policy,(frame_t *)bytes)==ESP32_MQUICKJS_WIFI_RAW_TX_INVALID_ARGUMENT);
    assert(esp32_mquickjs_wifi_raw_tx_validate((const uint8_t *)(UINTPTR_MAX-10),24,&policy,&out)==ESP32_MQUICKJS_WIFI_RAW_TX_INVALID_ARGUMENT);
    policy.associated_path=true;
    rejects(0x08,24,ESP32_MQUICKJS_WIFI_RAW_TX_INVALID_ARGUMENT);
    policy.associated_path=false;policy.interface=99;
    rejects(0x08,24,ESP32_MQUICKJS_WIFI_RAW_TX_INVALID_ARGUMENT);
    policy.interface=ESP32_MQUICKJS_WIFI_RAW_TX_STATION;
    /* Actual guard-page spans: no read before length admission or beyond header. */
    size_t page=(size_t)sysconf(_SC_PAGESIZE);
    uint8_t *memory=mmap(NULL,page*2,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    assert(memory!=MAP_FAILED && !mprotect(memory+page,page,PROT_NONE));
    for(size_t length=0;length<=64;length++) {
        uint8_t *span=memory+page-length;memset(span,0,length);if(length)span[0]=8;
        assert(esp32_mquickjs_wifi_raw_tx_validate(span,length,&policy,&out)==
            (length<24?ESP32_MQUICKJS_WIFI_RAW_TX_INVALID_LENGTH:ESP32_MQUICKJS_WIFI_RAW_TX_VALID));
    }
    assert(!munmap(memory,page*2));return 0;
}
