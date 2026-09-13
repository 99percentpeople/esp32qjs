
static void check(JSContext *ctx,const char *expression) {
    JSValue value=JS_Eval(ctx,expression,strlen(expression),"metadata",JS_EVAL_RETVAL);
    assert(!JS_IsException(value) && value==JS_TRUE);
}
int main(int argc,char **argv) {
    assert(argc==2);int scenario=atoi(argv[1]),total=1;
    for(int nth=0;nth<=total;nth++) {
        void *heap=malloc(192*1024);assert(heap);
        JSContext *ctx=JS_NewContext(heap,192*1024,&js_stdlib);assert(ctx);test_ctx=ctx;
        esp32_mquickjs_wifi_monitor_info_t info={0};
        uint8_t packet[28]={0x08,0x09}; /* Data, ToDS and Retry, 24-byte header */
        memset(packet+4,0x11,6);memset(packet+10,0x22,6);memset(packet+16,0x33,6);
        packet[22]=0x34;packet[23]=0x12;
        esp32_mquickjs_wifi_rx_parse_header(packet,sizeof(packet),&info.header);
        assert(info.header.status==ESP32_MQUICKJS_WIFI_RX_PARSED);
        info.driver=(esp32_mquickjs_wifi_rx_driver_metadata_t){.available=true,
            .type=ESP32_MQUICKJS_WIFI_PACKET_DATA,.rssi=-61,.noise_floor=-94,
            .primary=6,.secondary_raw=1,.raw_format=1,.ht_fields_available=true,
            .mcs=7,.bandwidth_mhz=40,.antenna_available=true,.antenna=1,
            .ldpc=true,.stbc=true,.short_gi=true,.driver_length=32};
        info.callback_time_us=UINT64_C(4294967396);info.readable_length=28;
        info.captured_length=26;info.truncated=true;info.header_type_matches=true;
        if(scenario==1) {info.metadata_only=true;info.truncated=false;info.driver.type=ESP32_MQUICKJS_WIFI_PACKET_MISC;info.captured_length=info.readable_length=0;info.header=(esp32_mquickjs_wifi_rx_header_t){0};}
        if(scenario==2) {esp32_mquickjs_wifi_rx_parse_header(packet,1,&info.header);info.readable_length=info.captured_length=1;info.truncated=true;info.header_type_matches=false;}
        if(scenario==3) {info.header_type_matches=false;info.driver.type=ESP32_MQUICKJS_WIFI_PACKET_MANAGEMENT;}
        if(scenario==5) {info.driver.he_layout=true;info.driver.raw_format=2;info.driver.antenna_available=false;}
        if(scenario==4) {info.driver.primary=0;info.driver.secondary_raw=3;info.driver.raw_format=99;info.driver.ht_fields_available=false;info.driver.antenna_available=false;}
        if(scenario>=6) {
            info.driver.he_layout=true;info.driver.ht_fields_available=false;info.driver.antenna_available=false;
            info.driver.raw_format=scenario==7?RX_BB_FORMAT_VHT_MU:RX_BB_FORMAT_VHT;
            info.driver.signal_word1=((scenario==8?15U:9U)<<28U) | (63U<<4U) | 2U | (1U<<24U) | (1U<<26U);
        }
        if(scenario>=9) {
            info.driver.he_layout=true;info.driver.ht_fields_available=false;info.driver.antenna_available=false;
            info.driver.raw_format=scenario==10?RX_BB_FORMAT_HE_MU:scenario==11?RX_BB_FORMAT_HE_TB:
                scenario==12?RX_BB_FORMAT_HE_ERSU:RX_BB_FORMAT_HE_SU;
            info.driver.signal_word1=scenario==10?(7U<<15U):scenario==11?(2U<<24U):
                scenario==12?(1U<<19U):((scenario==13?15U:11U)<<3U)|(1U<<19U);
            info.driver.signal_word2=(1U<<7U)|(1U<<9U)|(1U<<12U);
        }
        if(scenario==14) {
            info.driver.he_layout=false;info.driver.raw_format=1;info.driver.ht_fields_available=true;
            info.driver.ampdu_count=UINT8_MAX;
        }
        if(scenario==15) {
            info.driver.signal_word1=(1U<<7U)|(3U<<21U); /* special 4x LTF / 800 ns, no data DCM/STBC */
        }
        esp32_mquickjs_wifi_monitor_wire_snapshot_t wire;uint8_t encoded[256];
        assert(esp32_mquickjs_wifi_monitor_wire_snapshot(&info,UINT32_MAX,23,17,&wire));
        assert(wire.metadata.timestamp_us==info.callback_time_us && wire.metadata.radio_generation==17);
        assert(wire.metadata.phy==(scenario==4?255:scenario==14?1:scenario>=9?(scenario==10?4:scenario==11?6:scenario==12?5:3):scenario>=6?2:1));
        assert(esp32_mquickjs_wifi_rx_wire_write_metadata(ESP32_MQUICKJS_WIFI_RX_WIRE_MONITOR,
            &wire.frame,&wire.metadata,encoded,sizeof(encoded)));
        JSGCRef r;JSValue *root=push_root(ctx,&r);
        calls=0;fail_at=nth;inject=collect=true;
        *root=esp32_mquickjs_wifi_monitor_info_to_js(ctx,&info,UINT32_MAX,17);
        inject=collect=false;
        if(nth==0) {total=calls;assert(total>30);assert(!JS_IsException(*root));}
        else {assert(JS_IsException(*root));assert(JS_HasException(ctx));(void)JS_GetException(ctx);}
        assert(root_count==1 && !native_live);
        if(nth==0) {
            assert(!JS_IsException(JS_SetPropertyStr(ctx,JS_GetGlobalObject(ctx),"info",*root)));
            check(ctx,"info.sequence===4294967295 && info.radioGeneration===17 && info.timestampUs===4294967396 && info.timestampAccuracy==='callback-time' && info.rxSequence===null");
            check(ctx,"info.signal.rssi===-61 && info.signal.noiseFloor===-94");
            if(scenario==1) check(ctx,"info.packet===null && info.addresses.source===null");
            else {
                check(ctx,"info.packet.capture.fcs==='unknown' && info.packet.capture.pointerLayoutValid===true");
                if(scenario==0) check(ctx,"info.packet.category==='data' && info.packet.frameType.type===2 && info.packet.frameType.subtype===0 && info.packet.frameType.name==='data'");
                if(scenario==2) check(ctx,"info.packet.frameType===null && info.packet.frameControl===null && info.packet.flags.retry===null && !info.packet.capture.parseValid && info.addresses.source===null");
                else if(scenario==3) check(ctx,"!info.packet.capture.parseValid && info.addresses.source===null && info.packet.capture.payloadLength===0");
                else check(ctx,"info.packet.capture.parseValid && info.packet.capture.headerLength===24 && info.packet.capture.payloadLength===4 && info.packet.capture.capturedLength===26 && info.packet.capture.payloadCapturedLength===2 && info.packet.capture.truncated && info.packet.flags.toDs && info.packet.flags.retry && info.packet.sequenceControl===4660 && info.addresses.source==='22:22:22:22:22:22' && info.addresses.destination==='33:33:33:33:33:33' && info.addresses.bssid==='11:11:11:11:11:11'");
            }
            if(scenario==4 || scenario==11) check(ctx,"info.phy.guardIntervalNs===null && info.phy.heLtfSize===null && info.phy.dcm===null");
            else if(scenario>=9 && scenario!=14) {
                check(ctx,"info.phy.guardIntervalNs===800");
                if(scenario==10) check(ctx,"info.phy.heLtfSize===4 && info.phy.dcm===null");
                else if(scenario==15) check(ctx,"info.phy.heLtfSize===4 && info.phy.dcm===false && info.phy.stbc===false");
                else check(ctx,"info.phy.heLtfSize===1 && info.phy.dcm===false");
            } else check(ctx,"info.phy.guardIntervalNs===400 && info.phy.heLtfSize===null && info.phy.dcm===null");
            if(scenario==4) check(ctx,"info.channel.band===null && info.channel.secondary===null && info.phy.format==='unknown' && info.phy.mcs===null && info.phy.stbc===null && info.phy.bandwidthMHz===null && info.signal.antenna===null");
            else if(scenario==14)check(ctx,"info.phy.format==='ht' && info.phy.ampduCount===null && info.phy.mcs===7 && info.phy.bandwidthMHz===40");
            else if(scenario>=9) {
                check(ctx,"info.phy.shortGuardInterval===null && info.phy.ampduCount===null && info.phy.aggregation===null && info.signal.antenna===null");
                if(scenario==9)check(ctx,"info.phy.format==='he-su' && info.phy.mcs===11 && info.phy.bandwidthMHz===40 && info.phy.stbc===true && info.phy.fecCoding==='ldpc'");
                if(scenario==10)check(ctx,"info.phy.format==='he-mu' && info.phy.mcs===null && info.phy.bandwidthMHz===160 && info.phy.stbc===true && info.phy.fecCoding===null");
                if(scenario==11)check(ctx,"info.phy.format==='he-tb' && info.phy.mcs===null && info.phy.bandwidthMHz===80 && info.phy.stbc===null && info.phy.fecCoding===null");
                if(scenario==12)check(ctx,"info.phy.format==='he-er-su' && info.phy.mcs===0 && info.phy.bandwidthMHz===20 && info.phy.stbc===true && info.phy.fecCoding==='ldpc'");
                if(scenario==13)check(ctx,"info.phy.mcs===null && info.phy.bandwidthMHz===40");
            }
            else if(scenario>=6) {
                check(ctx,"info.phy.format==='vht' && info.phy.bandwidthMHz===80 && info.phy.shortGuardInterval===true && info.phy.stbc===false && info.phy.ampduCount===null && info.phy.aggregation===null && info.signal.antenna===null");
                if(scenario==6)check(ctx,"info.phy.mcs===9 && info.phy.fecCoding==='ldpc'");
                if(scenario==7)check(ctx,"info.phy.mcs===null && info.phy.fecCoding===null");
                if(scenario==8)check(ctx,"info.phy.mcs===null && info.phy.fecCoding==='ldpc'");
            }
            else check(ctx,"info.channel.band==='2.4GHz' && info.channel.secondary==='above' && info.phy.format==='ht' && info.phy.mcs===7 && info.phy.bandwidthMHz===40 && info.phy.fecCoding==='ldpc' && info.phy.legacyRate===null");
        }
        if(nth==0 && scenario==5) {
            check(ctx,"info.phy.ampduCount===null && info.signal.antenna===null");
            assert(wire.metadata.ampdu_count==UINT8_MAX);
            assert(wire.metadata.mcs==7 && wire.metadata.bandwidth_mhz==40);
        }
        pop_root(ctx,&r);assert(!root_count);
        JS_FreeContext(ctx);free(heap);
    }
    assert(moved_roots>0);
}
