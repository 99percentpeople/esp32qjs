
typedef esp32_mquickjs_wifi_rx_wire_frame_t frame_t;
typedef esp32_mquickjs_wifi_rx_wire_metadata_t metadata_t;
typedef esp32_mquickjs_wifi_csi_layout_t csi_t;
static uint32_t u32(const uint8_t *p) { return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
static uint16_t u16(const uint8_t *p) { return (uint16_t)(p[0]|((uint16_t)p[1]<<8)); }
static void rejected(int kind,const frame_t *f,const metadata_t *m) {
    uint8_t out[257];memset(out,0xa5,sizeof(out));
    assert(!esp32_mquickjs_wifi_rx_wire_write_metadata(kind,f,m,out,sizeof(out)));
    for(unsigned i=0;i<sizeof(out);i++)assert(out[i]==0xa5);
}
int main(void) {
    const int monitor=ESP32_MQUICKJS_WIFI_RX_WIRE_MONITOR,csi=ESP32_MQUICKJS_WIFI_RX_WIRE_CSI;
    metadata_t m;esp32_mquickjs_wifi_rx_wire_metadata_init(&m);
    frame_t f={0};uint8_t out[257];memset(out,0xa5,sizeof(out));
    uint8_t expected[256]={0};
    memset(expected+12,0xff,4);expected[59]=expected[60]=expected[61]=expected[63]=255;
    expected[87]=2;expected[97]=255;memset(expected+120,0xff,4);
    /* Compare all bytes, including every unused segment slot and reserved byte. */
    assert(esp32_mquickjs_wifi_rx_wire_write_metadata(monitor,&f,&m,out,sizeof(out)));
    assert(!memcmp(out,expected,256) && out[256]==0xa5);
    m.driver_payload_length_available=m.driver_packet_length_available=true;
    assert(esp32_mquickjs_wifi_rx_wire_write_metadata(monitor,&f,&m,out,sizeof(out)));
    assert(!u32(out+104) && !u32(out+108) && u32(out+116)==0x60000); /* Known zero differs from unavailable. */
    m.driver_payload_length_available=m.driver_packet_length_available=false;
    memset(m.addresses,0xaa,sizeof(m.addresses));m.noise_floor=-127;m.antenna=4;m.mcs=99;m.bandwidth_mhz=40;
    m.frame_control=m.duration_id=m.sequence_control=m.qos_control=0xabcd;
    assert(esp32_mquickjs_wifi_rx_wire_write_metadata(monitor,&f,&m,out,sizeof(out)));
    assert(!memcmp(out,expected,256)); /* Unavailable storage is never leaked. */

    esp32_mquickjs_wifi_rx_wire_metadata_init(&m);
    f=(frame_t){.packet_length=1,.driver_payload_length=UINT32_MAX,.packet_readable_length=28,
        .captured_header_length=1,.flags=1|2|8|16,.sequence=0x01020304};
    m.timestamp_us=UINT64_C(0x0102030405060708);m.rx_sequence=0x0a0b0c0d;
    m.session_generation=13;m.radio_generation=17;m.address_mask=5;
    memset(m.addresses,0xcc,sizeof(m.addresses));memset(m.addresses[0],0,6);memset(m.addresses[2],0x22,6);
    m.rssi=-61;m.noise_floor=-94;m.primary=6;m.secondary=1;m.phy=1;m.antenna=0;m.mcs=7;m.bandwidth_mhz=40;
    m.rx_flags=0x3ffff; /* All HT-era values have their availability flag. */
    m.rx_flags&=~((1U<<15)|(1U<<16)); /* Monitor must not claim CSI estimate validity. */
    m.frame_control_available=m.duration_available=m.sequence_available=m.qos_available=true;
    m.frame_control=0x0988;m.duration_id=0x1234;m.sequence_control=0x5678;m.qos_control=0x9abc;
    m.header_length=26;m.packet_type=3;m.capture_mode=2;m.driver_packet_length=32;
    m.driver_payload_length_available=m.driver_packet_length_available=true;
    assert(esp32_mquickjs_wifi_rx_wire_write_metadata(monitor,&f,&m,out,sizeof(out)));
    assert(u32(out)==0x01020304 && !memcmp(out+4,"\x08\x07\x06\x05\x04\x03\x02\x01",8));
    assert(u32(out+12)==0x0a0b0c0d && u32(out+16)==13 && u32(out+20)==17);
    assert(u16(out+54)==5 && out[56]==195 && out[57]==162);
    for(unsigned i=24;i<54;i++)assert(out[i]==(i>=36 && i<42?0x22:0));
    assert(out[58]==6 && out[59]==1 && out[60]==0 && out[61]==1 && out[62]==40 && out[63]==7);
    assert(u32(out+64)==0x27fff && u32(out+68)==0);
    for(unsigned i=72;i<87;i++)assert(out[i]==0);
    assert(u16(out+88)==0x0988 && u16(out+90)==0x1234 && u16(out+92)==0x5678 && u16(out+94)==0x9abc);
    assert(out[96]==3 && out[97]==8 && out[98]==0 && out[99]==2 && u16(out+100)==26);
    assert(u32(out+104)==UINT32_MAX && u32(out+108)==32 && u32(out+112)==1 && u32(out+116)==0x7e13d);
    assert(!u16(out+102));for(unsigned i=124;i<256;i++)assert(!out[i]);
    metadata_t good=m;frame_t good_frame=f;
    m.guard_interval_ns=400;
    assert(esp32_mquickjs_wifi_rx_wire_write_metadata(monitor,&f,&m,out,sizeof(out)) && u16(out+124)==400);
    m.guard_interval_ns=800;rejected(monitor,&f,&m); /* SGI true contradicts 800 ns. */
    m.rx_flags&=~ESP32_MQUICKJS_WIFI_RX_WIRE_SGI;
    assert(esp32_mquickjs_wifi_rx_wire_write_metadata(monitor,&f,&m,out,sizeof(out)));
    m=good;m.phy=3;m.rx_flags=ESP32_MQUICKJS_WIFI_RX_WIRE_DCM_AVAILABLE;
    m.guard_interval_ns=800;m.he_ltf_size=4;
    assert(esp32_mquickjs_wifi_rx_wire_write_metadata(monitor,&f,&m,out,sizeof(out)));
    assert(u16(out+124)==800 && out[126]==4 && !out[127] && u32(out+64)==(1U<<18));
    m.rx_flags|=ESP32_MQUICKJS_WIFI_RX_WIRE_DCM;
    assert(esp32_mquickjs_wifi_rx_wire_write_metadata(monitor,&f,&m,out,sizeof(out)) && u32(out+64)==(3U<<18));
    metadata_t he_good=m;
    const unsigned bad_gi[]={1,399,401,799,801,1599,1601,3199,3201,65535};
    for(unsigned i=0;i<sizeof(bad_gi)/sizeof(bad_gi[0]);i++){m=he_good;m.guard_interval_ns=bad_gi[i];rejected(monitor,&f,&m);}
    m=he_good;m.guard_interval_ns=400;rejected(monitor,&f,&m);
    m=he_good;m.he_ltf_size=3;rejected(monitor,&f,&m);
    m=he_good;m.rx_flags=ESP32_MQUICKJS_WIFI_RX_WIRE_DCM;rejected(monitor,&f,&m);
    m=he_good;m.phy=1;rejected(monitor,&f,&m);
    m=good;m.guard_interval_ns=1600;rejected(monitor,&f,&m);
    m=good;m.he_ltf_size=1;rejected(monitor,&f,&m);
    m=good;m.phy=255;m.guard_interval_ns=800;rejected(monitor,&f,&m);
    m=good;
    m.driver_payload_length_available=false;rejected(monitor,&f,&m);
    m=good;m.driver_packet_length_available=false;rejected(monitor,&f,&m);
    const uint32_t values[]={1U<<5,1U<<6,1U<<8,1U<<10,1U<<12,1U<<14,1U<<16};
    for(unsigned i=0;i<7;i++){m=good;m.rx_flags=values[i];rejected(csi,&f,&m);}
    for(unsigned i=18;i<32;i++){m=good;m.rx_flags|=1U<<i;rejected(csi,&f,&m);}
    m=good;m.rx_flags|=1U<<15;rejected(monitor,&f,&m);
    m=good;m.address_mask=32;rejected(monitor,&f,&m);
    m=good;m.secondary=3;rejected(monitor,&f,&m);
    m=good;m.phy=7;rejected(monitor,&f,&m); /* Native CSI UNKNOWN must be normalized. */
    m=good;m.antenna=255;rejected(monitor,&f,&m);
    m=good;m.mcs=255;rejected(monitor,&f,&m);
    m=good;m.bandwidth_mhz=0;rejected(monitor,&f,&m);
    m=good;m.timestamp_accuracy=3;rejected(monitor,&f,&m);
    m=good;m.packet_type=5;rejected(monitor,&f,&m);
    m=good;m.packet_type=1;rejected(monitor,&f,&m);
    m=good;m.fcs_state=4;rejected(monitor,&f,&m);
    m=good;m.capture_mode=3;rejected(monitor,&f,&m);
    m=good;m.capture_mode=0;rejected(monitor,&f,&m);
    m=good;m.capture_mode=1;rejected(monitor,&f,&m);
    m=good;m.header_length=29;rejected(monitor,&f,&m);
    m=good;m.frame_control_available=false;rejected(monitor,&f,&m);
    m=good;m.duration_available=false;rejected(monitor,&f,&m);
    m=good;m.frame_control|=1;rejected(monitor,&f,&m);
    m=good;f.captured_header_length=0;rejected(monitor,&f,&m);f=good_frame;
    m=good;m.capture_mode=1;f.packet_length=f.captured_header_length=26;f.flags=1|4|8|16;
    assert(esp32_mquickjs_wifi_rx_wire_write_metadata(monitor,&f,&m,out,sizeof(out)));
    assert(u32(out+116)==0x7e13b); /* Complete header-only is not truncation. */
    f.packet_length=f.captured_header_length=25;rejected(monitor,&f,&m);
    m=good;m.capture_mode=0;f=good_frame;f.packet_length=f.captured_header_length=0;f.flags=8|16;
    assert(esp32_mquickjs_wifi_rx_wire_write_metadata(monitor,&f,&m,out,sizeof(out)));
    assert(u32(out+104)==UINT32_MAX && u32(out+108)==32 && !u32(out+112));
    m=good;m.header_length=0;m.frame_control_available=m.duration_available=m.sequence_available=m.qos_available=false;
    f=good_frame;f.captured_header_length=0;f.packet_readable_length=1;f.flags=1|2|16;
    assert(esp32_mquickjs_wifi_rx_wire_write_metadata(monitor,&f,&m,out,sizeof(out)));
    assert(out[97]==255 && !u16(out+88)); /* One-byte native prefix of a longer reported packet. */
    f.flags&=~2;rejected(monitor,&f,&m);f.flags|=2;
    m.driver_packet_length=1;rejected(monitor,&f,&m);
    f.flags&=~2;
    assert(esp32_mquickjs_wifi_rx_wire_write_metadata(monitor,&f,&m,out,sizeof(out)));
    m.driver_packet_length=32;f.flags|=2;
    m.frame_control_available=true;rejected(monitor,&f,&m);
    f.packet_readable_length=2;f.flags|=2;m.duration_available=true;rejected(monitor,&f,&m);
    m.duration_available=false;m.sequence_available=true;rejected(monitor,&f,&m);
    m.sequence_available=false;m.qos_available=true;rejected(monitor,&f,&m);

    esp32_mquickjs_wifi_rx_wire_metadata_init(&m);f=(frame_t){.csi_length=9,.sequence=42};
    csi_t layout={.schema=1,.sample_encoding=1,.sample_bits=8,.byte_length=9,.iq_pair_count=4,
        .trailing_padding_bytes=1,.segment_count=2,.known=true};
    layout.segments[0]=(esp32_mquickjs_wifi_csi_segment_t){.type=1,.length_bytes=4,.iq_pair_count=2,
        .subcarrier_range_count=1,.subcarrier_ranges={{-1,0}},.null_subcarrier_count=1,.null_subcarriers={0}};
    layout.segments[1]=(esp32_mquickjs_wifi_csi_segment_t){.type=2,.offset_bytes=4,.length_bytes=4,.iq_pair_count=2,
        .subcarrier_range_count=1,.subcarrier_ranges={{1,2}}};
    memset(&layout.segments[2],0xa5,sizeof(layout.segments[2])); /* Unused native storage never serialized. */
    m.csi_layout=&layout;m.csi_data_valid=true;m.first_word_invalid=true;
    assert(esp32_mquickjs_wifi_rx_wire_write_metadata(csi,&f,&m,out,sizeof(out)));
    assert(u32(out+68)==7 && out[72]==1 && out[73]==8 && out[74]==1 && out[75]==0);
    assert(u32(out+76)==9 && u32(out+80)==4 && u16(out+84)==1 && out[86]==2);
    assert(out[128]==1 && out[129]==1 && out[130]==1 && !out[131]);
    assert(!u32(out+132) && u32(out+136)==4 && u32(out+140)==2 && u16(out+144)==65535 && !u16(out+146));
    for(unsigned i=148;i<168;i++)assert(!out[i]);
    assert(out[168]==2 && u32(out+172)==4 && u32(out+176)==4 && u32(out+180)==2);
    for(unsigned i=208;i<256;i++)assert(!out[i]);
    rejected(monitor,&f,&m);
    csi_t valid=layout;
#define BAD(change) do { layout=valid;change;rejected(csi,&f,&m); } while(0)
    BAD(layout.schema=3);BAD(layout.sample_encoding=4);BAD(layout.sample_bits=12);
    BAD(layout.byte_length=8);BAD(layout.trailing_padding_bytes=4);
    BAD(layout.segment_count=4);BAD(layout.segment_count=0);BAD(layout.iq_pair_count=5);
    BAD(layout.sample_encoding=0;layout.sample_bits=0);BAD(layout.schema=0);
    BAD(layout.segments[0].type=8);BAD(layout.segments[0].type=0);
    BAD(layout.segments[0].length_bytes=UINT32_MAX);
    BAD(layout.segments[1].offset_bytes=5);BAD(layout.segments[1].length_bytes=3);
    BAD(layout.segments[0].iq_pair_count=UINT32_MAX);
    BAD(layout.segments[0].subcarrier_range_count=3);BAD(layout.segments[0].null_subcarrier_count=4);
    BAD(layout.segments[0].subcarrier_ranges[0].end=-2);
    BAD(layout.segments[0].subcarrier_ranges[0].end=1);
    BAD(layout.segments[0].subcarrier_range_count=2;layout.segments[0].subcarrier_ranges[1]=layout.segments[0].subcarrier_ranges[0]);
    BAD(layout.segments[0].null_subcarrier_count=2); /* Duplicate null index. */
#undef BAD
    layout=(csi_t){.schema=2,.byte_length=9,.segment_count=1};layout.segments[0].length_bytes=9;
    assert(esp32_mquickjs_wifi_rx_wire_write_metadata(csi,&f,&m,out,sizeof(out)));
    assert(u32(out+68)==3 && !out[72] && !out[73] && out[74]==2 && !u32(out+80) && out[86]==1);
    /* An unknown layout preserves opaque bytes without guessing IQ/sample width. */
    layout=valid;layout.sample_encoding=3;layout.sample_bits=12;
    layout.byte_length=f.csi_length=13;layout.segments[0].length_bytes=6;layout.segments[1].offset_bytes=6;layout.segments[1].length_bytes=6;
    assert(esp32_mquickjs_wifi_rx_wire_write_metadata(csi,&f,&m,out,sizeof(out)));
    layout.sample_encoding=2;layout.byte_length=f.csi_length=17;
    layout.segments[0].length_bytes=8;layout.segments[1].offset_bytes=8;layout.segments[1].length_bytes=8;
    assert(esp32_mquickjs_wifi_rx_wire_write_metadata(csi,&f,&m,out,sizeof(out)));

    metadata_t snapshot=m;uint8_t before[256];memcpy(before,out,256);
    union {metadata_t input;uint8_t bytes[512];} alias;
    alias.input=snapshot;
    assert(esp32_mquickjs_wifi_rx_wire_write_metadata(csi,&f,&alias.input,alias.bytes,sizeof(alias.bytes)));
    assert(!memcmp(alias.bytes,before,256));
    union {csi_t input;uint8_t bytes[512];} layout_alias;
    layout_alias.input=layout;m.csi_layout=&layout_alias.input;
    assert(esp32_mquickjs_wifi_rx_wire_write_metadata(csi,&f,&m,layout_alias.bytes,sizeof(layout_alias.bytes)));
    assert(!memcmp(layout_alias.bytes,before,256));
    m=snapshot;
    memset(out,0xa5,sizeof(out));
    assert(!esp32_mquickjs_wifi_rx_wire_write_metadata(csi,&f,&m,out,255));
    for(unsigned i=0;i<sizeof(out);i++)assert(out[i]==0xa5);
    rejected(csi,NULL,&m);rejected(csi,&f,NULL);rejected(0,&f,&m);
    assert(!esp32_mquickjs_wifi_rx_wire_write_metadata(csi,&f,&m,NULL,256));
    assert(!esp32_mquickjs_wifi_rx_wire_write_metadata(csi,&f,&m,(uint8_t *)(UINTPTR_MAX-31),256));
}
