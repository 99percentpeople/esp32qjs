
typedef esp32_mquickjs_wifi_rx_wire_frame_t frame_t;
typedef esp32_mquickjs_wifi_rx_wire_layout_t layout_t;
static uint32_t u32(const uint8_t *p) {return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static uint16_t u16(const uint8_t *p) {return (uint16_t)(p[0]|((uint16_t)p[1]<<8));}
static void unchanged(const uint8_t *p,size_t n) {for(size_t i=0;i<n;i++)assert(p[i]==0xa5);}
static void rejected(int kind,frame_t *frames,uint32_t count) {
    layout_t result,before;memset(&result,0xa5,sizeof(result));memcpy(&before,&result,sizeof(result));
    uint8_t bytes[1024];memset(bytes,0xa5,sizeof(bytes));
    assert(!esp32_mquickjs_wifi_rx_wire_layout(kind,frames,count,&result));
    assert(!memcmp(&result,&before,sizeof(result)));
    assert(!esp32_mquickjs_wifi_rx_wire_write_control(kind,frames,count,bytes,sizeof(bytes)));
    unchanged(bytes,sizeof(bytes));
}
int main(void) {
    const int csi=ESP32_MQUICKJS_WIFI_RX_WIRE_CSI,monitor=ESP32_MQUICKJS_WIFI_RX_WIRE_MONITOR;
    frame_t frames[128]={0};
    frames[0]=(frame_t){.csi_length=3,.packet_length=5,.driver_payload_length=7,
        .packet_readable_length=7,.captured_header_length=2,.flags=19,.sequence=0x01020304};
    frames[1]=(frame_t){.csi_length=4,.driver_payload_length=99};
    layout_t layout;
    esp32_mquickjs_wifi_rx_wire_offsets_t offsets;
    assert(esp32_mquickjs_wifi_rx_wire_layout(csi,frames,2,&layout));
    assert(layout.frame_count==2 && layout.directory_bytes==40 && layout.metadata_base==112);
    assert(layout.control_bytes==624 && layout.total_bytes==640);
    assert(esp32_mquickjs_wifi_rx_wire_offsets(csi,frames,2,0,&offsets));
    assert(offsets.metadata_offset==112 && offsets.csi_offset==624 && offsets.packet_offset==628 && offsets.end_offset==636);
    assert(esp32_mquickjs_wifi_rx_wire_offsets(csi,frames,2,1,&offsets));
    assert(offsets.metadata_offset==368 && offsets.csi_offset==636 && !offsets.packet_offset && offsets.end_offset==640);
    uint8_t bytes[65536];memset(bytes,0xa5,sizeof(bytes));
    assert(esp32_mquickjs_wifi_rx_wire_write_control(csi,frames,2,bytes,sizeof(bytes)));
    assert(!memcmp(bytes,"E32QCSI1",8) && u16(bytes+8)==1 && u16(bytes+10)==32 && u32(bytes+12)==2);
    assert(u16(bytes+16)==40 && u16(bytes+18)==256 && u32(bytes+20)==1 && u32(bytes+24)==640 && !u32(bytes+28));
    assert(u32(bytes+32)==112 && u32(bytes+36)==624 && u32(bytes+40)==3 && u32(bytes+44)==628);
    assert(u32(bytes+48)==5 && u16(bytes+52)==2 && u16(bytes+54)==19 && u32(bytes+56)==7);
    assert(u32(bytes+60)==7 && u32(bytes+64)==0x01020304 && !u32(bytes+68));
    assert(u32(bytes+72)==368 && u32(bytes+76)==636 && !u32(bytes+84));
    for(size_t i=112;i<624;i++)assert(!bytes[i]); /* Metadata storage only; encoder fills it later. */
    unchanged(bytes+624,sizeof(bytes)-624); /* Never writes payload/padding storage. */
    memset(bytes,0xa5,sizeof(bytes));
    assert(!esp32_mquickjs_wifi_rx_wire_write_control(csi,frames,2,bytes,623));unchanged(bytes,sizeof(bytes));
    assert(!esp32_mquickjs_wifi_rx_wire_write_control(csi,frames,2,NULL,sizeof(bytes)));
    assert(!esp32_mquickjs_wifi_rx_wire_offsets(csi,frames,2,2,&offsets));

    frames[0].csi_length=frames[1].csi_length=0;
    assert(esp32_mquickjs_wifi_rx_wire_layout(monitor,frames,2,&layout));
    assert(layout.metadata_base==80 && layout.control_bytes==592 && layout.total_bytes==600);
    assert(esp32_mquickjs_wifi_rx_wire_write_control(monitor,frames,2,bytes,sizeof(bytes)));
    assert(!memcmp(bytes,"E32QMON1",8) && u16(bytes+16)==24 && u32(bytes+24)==600);
    assert(u32(bytes+32)==80 && u32(bytes+36)==592 && u32(bytes+40)==5 && u32(bytes+44)==7);
    assert(u32(bytes+48)==7 && u16(bytes+52)==2 && u16(bytes+54)==19);
    assert(u32(bytes+56)==336 && !u32(bytes+60) && u32(bytes+68)==99); /* Reported length is not a copied length. */
    for(size_t i=80;i<592;i++)assert(!bytes[i]);
    unchanged(bytes+592,sizeof(bytes)-592);
    frames[0].csi_length=1;rejected(monitor,frames,2);frames[0].csi_length=0;
    rejected(0,frames,2);rejected(3,frames,2);rejected(csi,frames,0);rejected(csi,frames,129);rejected(csi,NULL,2);
    frame_t valid=frames[0];
    const uint16_t invalid_flags[]={0,1,17,19|32,19|4,8};
    for(unsigned i=0;i<sizeof(invalid_flags)/sizeof(invalid_flags[0]);i++) {
        frames[0]=valid;frames[0].flags=invalid_flags[i];rejected(csi,frames,1);
    }
    frames[0]=valid;frames[0].captured_header_length=6;rejected(csi,frames,1);
    frames[0]=valid;frames[0].packet_readable_length=4;rejected(csi,frames,1);
    frames[0]=valid;frames[0].flags=1|4|8|16;frames[0].captured_header_length=5;
    assert(esp32_mquickjs_wifi_rx_wire_layout(csi,frames,1,&layout)); /* Intentional complete header-only. */
    frames[0].flags|=2;rejected(csi,frames,1);
    /* One captured byte can come from a fully parsed original header. The
     * directory records its copied prefix, not the uncopied complete header. */
    frames[0]=(frame_t){.packet_length=1,.packet_readable_length=24,
        .captured_header_length=1,.flags=1|2|8|16};
    assert(esp32_mquickjs_wifi_rx_wire_layout(monitor,frames,1,&layout));
    assert(layout.control_bytes==312 && layout.total_bytes==316);
    frames[0].captured_header_length=24;rejected(monitor,frames,1);
    frames[0]=(frame_t){.packet_length=24,.packet_readable_length=24,
        .captured_header_length=24,.flags=1|8|16};
    assert(esp32_mquickjs_wifi_rx_wire_layout(monitor,frames,1,&layout));
    assert(layout.total_bytes==336); /* Complete packet needs no extra padding. */
    frames[0].flags|=2;
    /* The envelope lacks driver packet length. Metadata validation decides
     * whether copying all readable bytes still truncates a native packet. */
    assert(esp32_mquickjs_wifi_rx_wire_layout(monitor,frames,1,&layout));
    frames[0]=(frame_t){.driver_payload_length=UINT32_MAX,.packet_readable_length=100,.flags=8|16};
    assert(esp32_mquickjs_wifi_rx_wire_layout(monitor,frames,1,&layout)); /* Metadata facts without captured packet bytes. */
    assert(layout.total_bytes==312);
    frames[0].flags=8;rejected(monitor,frames,1); /* Parse requires pointer proof. */

    frames[0]=(frame_t){.csi_length=UINT32_MAX-328-3};
    assert(esp32_mquickjs_wifi_rx_wire_layout(csi,frames,1,&layout) && layout.total_bytes==UINT32_MAX-3);
    frames[0].csi_length++;rejected(csi,frames,1); /* Alignment alone would overflow. */
    frames[0].csi_length=UINT32_MAX;rejected(csi,frames,1);
    frames[0].csi_length=frames[1].csi_length=0x80000000U;rejected(csi,frames,2);
    memset(frames,0,sizeof(frames));
    assert(esp32_mquickjs_wifi_rx_wire_layout(csi,frames,128,&layout) && layout.total_bytes==37920);
    assert(esp32_mquickjs_wifi_rx_wire_layout(monitor,frames,128,&layout) && layout.total_bytes==35872);

    union { frame_t input[2];uint8_t output[1024]; } alias;
    memset(&alias,0,sizeof(alias));alias.input[0]=(frame_t){.csi_length=4};
    uint8_t before[sizeof(alias)];memcpy(before,&alias,sizeof(alias));
    assert(!esp32_mquickjs_wifi_rx_wire_write_control(csi,alias.input,1,alias.output,sizeof(alias.output)));
    assert(!memcmp(before,&alias,sizeof(alias)));
    assert(!esp32_mquickjs_wifi_rx_wire_write_control(csi,frames,1,(uint8_t *)(UINTPTR_MAX-31),1024));
}
