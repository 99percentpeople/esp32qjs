
#include "esp32_mquickjs_wifi_csi_rx_native.h"
#include <assert.h>
#include <string.h>
uintptr_t task_id=1;int in_isr;
static unsigned observed,native_calls;static bool mismatch,nest;static uint32_t last_readable;
void *esp32qjs_wifi_csi_rx_begin_copy(void *,const void *,size_t);
void *esp32qjs_wifi_csi_rx_append_copy(void *,const void *,size_t);
void esp32qjs_wifi_csi_rx_copied(const void *,uint32_t,const void *,const void *,uint32_t,uint32_t);
void wdev_csi_rx_process(const void *samples,uint32_t sample_bytes,const void *rx,const void *header,
    uint32_t reported,uint32_t invalid){
    (void)invalid;native_calls++;
    if(nest){nest=false;esp32qjs_wifi_csi_rx_copied(samples,sample_bytes,rx,header,reported,0);}
    wifi_csi_info_t info={.buf=samples,.len=sample_bytes+(mismatch?1:0),.hdr=header,.payload_len=reported};
    esp32_mquickjs_wifi_csi_rx_packet_t packet;
    if(esp32_mquickjs_wifi_csi_rx_native_take(&info,&packet)){
        observed++;assert(packet.bytes==header);last_readable=(uint32_t)packet.readable_bytes;
    }
    assert(!esp32_mquickjs_wifi_csi_rx_native_take(&info,&packet));
}
static void copy(uint8_t *destination){
    uint8_t input[128]={0};input[METADATA]=8;
    assert(esp32qjs_wifi_csi_rx_begin_copy(destination,input,PREFIX)==destination);
    assert(esp32qjs_wifi_csi_rx_append_copy(destination+METADATA,input+METADATA,17)==destination+METADATA);
    assert(esp32qjs_wifi_csi_rx_append_copy(destination+METADATA+17,input+METADATA+17,23)==destination+METADATA+17);
}
int main(void){
    uint8_t data[6][128]={{0}},samples[4]={0};
    copy(data[0]);esp32qjs_wifi_csi_rx_copied(samples,4,data[0],data[0]+METADATA,40,0);assert(!observed&&native_calls==1);
    esp32_mquickjs_wifi_csi_rx_native_enable(true);
    copy(data[0]);esp32qjs_wifi_csi_rx_copied(samples,4,data[0],data[0]+METADATA,40,0);assert(observed==1&&last_readable==40);
    esp32qjs_wifi_csi_rx_copied(samples,4,data[0],data[0]+METADATA,40,0);assert(observed==1);
    copy(data[0]);mismatch=true;esp32qjs_wifi_csi_rx_copied(samples,4,data[0],data[0]+METADATA,40,0);mismatch=false;assert(observed==1);
    copy(data[0]);esp32qjs_wifi_csi_rx_copied(samples,4,data[0]+1,data[0]+METADATA,40,0);assert(observed==1);
    copy(data[0]);nest=true;esp32qjs_wifi_csi_rx_copied(samples,4,data[0],data[0]+METADATA,40,0);assert(observed==2);
    copy(data[0]);task_id=2;copy(data[1]);task_id=1;
    esp32qjs_wifi_csi_rx_copied(samples,4,data[0],data[0]+METADATA,40,0);
    task_id=2;esp32qjs_wifi_csi_rx_copied(samples,4,data[1],data[1]+METADATA,40,0);assert(observed==4);
    esp32_mquickjs_wifi_csi_rx_native_enable(false);esp32_mquickjs_wifi_csi_rx_native_enable(true);
    for(task_id=1;task_id<=5;task_id++)copy(data[task_id-1]);
    task_id=5;esp32qjs_wifi_csi_rx_copied(samples,4,data[4],data[4]+METADATA,40,0);assert(observed==4);
    esp32_mquickjs_wifi_csi_rx_native_enable(false);task_id=1;in_isr=1;
    esp32_mquickjs_wifi_csi_rx_native_enable(true);copy(data[0]);
    esp32qjs_wifi_csi_rx_copied(samples,4,data[0],data[0]+METADATA,40,0);assert(observed==4);
    in_isr=0;copy(data[0]);esp32_mquickjs_wifi_csi_rx_native_enable(false);
    esp32qjs_wifi_csi_rx_copied(samples,4,data[0],data[0]+METADATA,40,0);assert(observed==4);
    assert(esp32_mquickjs_wifi_csi_rx_native_control_bytes()>0);
    return 0;
}
