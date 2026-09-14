#include <string.h>

typedef esp32_mquickjs_wifi_raw_tx_snapshot_t snapshot_t;
static void rejects(const esp_80211_tx_info_t *info) {
    snapshot_t out,old;memset(&out,0xa5,sizeof(out));memcpy(&old,&out,sizeof(old));
    assert(!esp32_mquickjs_wifi_raw_tx_snapshot(info,100,-1,&out));assert(!memcmp(&out,&old,sizeof(out)));
}
int main(void) {
    size_t page=(size_t)sysconf(_SC_PAGESIZE);
    uint8_t *memory=mmap(NULL,page*4,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    assert(memory!=MAP_FAILED && !mprotect(memory+page,page,PROT_NONE) && !mprotect(memory+page*3,page,PROT_NONE));
    uint8_t *source=memory+page-6,*destination=memory+page*3-6;
    memset(source,0x12,6);memset(destination,0x34,6);
    esp_80211_tx_info_t info={.src_addr=source,.des_addr=destination,.ifidx=WIFI_IF_STA,
        .data=memory+page,.data_len=255,.rate=(wifi_phy_rate_t)123,.tx_status=WIFI_SEND_SUCCESS};
    esp_80211_tx_info_t before;memcpy(&before,&info,sizeof(info));snapshot_t out;
    assert(esp32_mquickjs_wifi_raw_tx_snapshot(&info,UINT64_MAX,-1,&out));
    assert(!memcmp(&info,&before,sizeof(info)));
    assert(out.callback_time_us==UINT64_MAX && out.interface==ESP32_MQUICKJS_WIFI_RAW_TX_STATION);
    assert(out.status==ESP32_MQUICKJS_WIFI_RAW_TX_DRIVER_SUCCESS && out.raw_status==WIFI_SEND_SUCCESS);
    assert(out.source_available && out.destination_available && out.raw_body_length==255 && out.raw_rate==123);
    for(unsigned i=0;i<6;i++)assert(out.source[i]==0x12 && out.destination[i]==0x34);
    /* Guards start immediately after six-byte addresses. data is completely unreadable. */
    info.ifidx=WIFI_IF_AP;info.tx_status=WIFI_SEND_FAIL;info.data_len=0;
    assert(esp32_mquickjs_wifi_raw_tx_snapshot(&info,0,-1,&out));
    assert(out.interface==ESP32_MQUICKJS_WIFI_RAW_TX_ACCESS_POINT && !out.callback_time_us && !out.raw_body_length);
    assert(out.status==ESP32_MQUICKJS_WIFI_RAW_TX_DRIVER_FAILED);
    info.tx_status=(wifi_tx_status_t)99;
    assert(esp32_mquickjs_wifi_raw_tx_snapshot(&info,42,-1,&out));
    assert(out.status==ESP32_MQUICKJS_WIFI_RAW_TX_DRIVER_UNKNOWN && out.raw_status==99);
    /* Input/output may alias when storage is large enough: all reads precede commit. */
    union { esp_80211_tx_info_t info; snapshot_t snapshot; } shared;
    shared.info=info;
    assert(esp32_mquickjs_wifi_raw_tx_snapshot(&shared.info,123,-1,&shared.snapshot));
    assert(shared.snapshot.callback_time_us==123 && shared.snapshot.source[0]==0x12 && shared.snapshot.destination[0]==0x34);
    info.src_addr=info.des_addr=NULL;info.data=NULL;
    assert(esp32_mquickjs_wifi_raw_tx_snapshot(&info,42,-1,&out));
    assert(!out.source_available && !out.destination_available);
    for(unsigned i=0;i<6;i++)assert(!out.source[i] && !out.destination[i]);
    info.src_addr=source;info.des_addr=destination;
    assert(esp32_mquickjs_wifi_raw_tx_snapshot(&info,42,-1,&out));
    assert(!munmap(memory,page*4));
    for(unsigned i=0;i<6;i++)assert(out.source[i]==0x12 && out.destination[i]==0x34);
    info.src_addr=info.des_addr=NULL;info.ifidx=(wifi_interface_t)99;
    rejects(&info);rejects(NULL);
    info.ifidx=WIFI_IF_STA;
    assert(!esp32_mquickjs_wifi_raw_tx_snapshot(&info,0,-1,NULL));

    /* LMAC status byte is optional and never invented when unavailable. */
    assert(!out.mac_status_available && out.mac_status_byte==0);
    info.ifidx=WIFI_IF_STA;info.tx_status=WIFI_SEND_FAIL;
    assert(esp32_mquickjs_wifi_raw_tx_snapshot(&info,7,8,&out));
    assert(out.mac_status_available && out.mac_status_byte==8);
    /* Do not confuse the LMAC state-machine byte with TX-info completion. */
    for (unsigned code=0;code<256;code++) {
        const char *name=esp32_mquickjs_wifi_raw_tx_mac_status_name((uint8_t)code);
        const char *expected=code==1 ? "success" : code==2 || code==3 ? "frame-exchange" :
            code==4 ? "discarded" : NULL;
        assert(expected ? name && !strcmp(name,expected) : name==NULL);
    }
    assert(esp32_mquickjs_wifi_raw_tx_snapshot(&info,7,256,&out));
    assert(!out.mac_status_available);
    return 0;
}
