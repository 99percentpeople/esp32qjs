
#include "esp32_mquickjs_wifi_csi_store.h"
#include "esp32_mquickjs_wifi_csi_rx_span.h"
#include "esp32_mquickjs_wifi_csi_wire.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
static unsigned calls, fail_at, live;
static void *allocate(size_t n,size_t size,void *opaque){
    (void)opaque;if(++calls==fail_at)return NULL;
    void *p=calloc(n,size);if(p)live++;return p;
}
static void *allocate_bytes(size_t n,void *opaque){return allocate(1,n,opaque);}
static void release(void *p,void *opaque){(void)opaque;if(p){assert(live);live--;free(p);}}
static void lock(void *opaque){(void)opaque;}
static esp32_mquickjs_wifi_csi_event_t event;
static bool reject;
static bool publish(const esp32_mquickjs_wifi_csi_event_t *value,void *opaque){
    (void)opaque;event=*value;return !reject;
}
/* Calls production publication even when packet byte capture is disabled. */
static void filter_and_identity(void){
    esp32_mquickjs_wifi_csi_allocator_t allocator={allocate, allocate_bytes, release, NULL, NULL};
    esp32_mquickjs_wifi_csi_resources_t resources;
    assert(esp32_mquickjs_wifi_csi_resources_init(&resources,99,1,4,0,&allocator));
    esp32_mquickjs_wifi_csi_resources_set_accepting(&resources,true);
    uint8_t bytes[40]={8,2},samples[4]={0}; /* FromDS: addr2 is AP, addr3 is source. */
    for(unsigned i=0;i<6;i++){bytes[4+i]=(uint8_t)(i+1);bytes[10+i]=(uint8_t)(i+11);bytes[16+i]=(uint8_t)(i+21);bytes[24+i]=(uint8_t)(i+31);}
    bytes[22]=0x30;bytes[23]=0x12;
    esp32_mquickjs_wifi_csi_packet_input_t input={bytes,40,40,16};
    esp32_mquickjs_wifi_csi_metadata_t metadata={.radio_generation=4,.channel=6,
        .layout={.byte_length=4,.segment_count=1,.segments={{.length_bytes=4}}}};
    assert(esp32_mquickjs_wifi_csi_callback_publish(&resources,&metadata,samples,0,&input,publish,NULL)==ESP32_MQUICKJS_WIFI_CSI_PUBLISH_INVALID);
    assert(esp32_mquickjs_wifi_csi_callback_publish(&resources,&metadata,NULL,4,&input,publish,NULL)==ESP32_MQUICKJS_WIFI_CSI_PUBLISH_INVALID);
    assert(atomic_load(&resources.counters.invalid_callback_data)==2);
    resources.filter.source_mac_count=resources.filter.bssid_count=1;
    memcpy(resources.filter.source_macs[0],bytes+16,6);memcpy(resources.filter.bssids[0],bytes+10,6);
    resources.filter.frame_types_set=true;resources.filter.frame_types=1U<<ESP32_MQUICKJS_WIFI_PACKET_DATA;
    resources.filter.frame_subtypes_set=true;resources.filter.frame_subtypes=1;
    unsigned allocations=calls;
    assert(esp32_mquickjs_wifi_csi_callback_publish(&resources,&metadata,samples,4,&input,publish,NULL)==ESP32_MQUICKJS_WIFI_CSI_PUBLISH_ACCEPTED);
    assert(calls==allocations&&resources.slots[0].packet==NULL&&resources.max_packet_bytes==0);
    esp32_mquickjs_wifi_csi_slot_t *slot=&resources.slots[0];
    assert(slot->metadata.rx_sequence==0x123&&slot->metadata.address_mask==31);
    assert(!memcmp(slot->metadata.addresses[ESP32_MQUICKJS_WIFI_RX_SOURCE],bytes+16,6));
    esp32_mquickjs_wifi_csi_wire_snapshot_t wire;assert(esp32_mquickjs_wifi_csi_wire_snapshot(slot,&wire));
    assert(!wire.frame.packet_length&&wire.metadata.address_mask==31&&wire.metadata.rx_sequence==0x123);
    assert(esp32_mquickjs_wifi_csi_slot_discard_event(&resources,&event));
    bytes[1]=3; /* Four-address frame has no BSSID; cannot match requested BSSID. */
    assert(esp32_mquickjs_wifi_csi_callback_publish(&resources,&metadata,samples,4,&input,publish,NULL)==ESP32_MQUICKJS_WIFI_CSI_PUBLISH_FILTERED);
    bytes[1]=2;bytes[0]=0x88; /* QoS data subtype differs. */
    assert(esp32_mquickjs_wifi_csi_callback_publish(&resources,&metadata,samples,4,&input,publish,NULL)==ESP32_MQUICKJS_WIFI_CSI_PUBLISH_FILTERED);
    bytes[0]=8;input.copied_length=23;
    assert(esp32_mquickjs_wifi_csi_callback_publish(&resources,&metadata,samples,4,&input,publish,NULL)==ESP32_MQUICKJS_WIFI_CSI_PUBLISH_FILTERED);
    assert(!atomic_load(&resources.counters.leased_frames));
    resources.filter=(esp32_mquickjs_wifi_csi_filter_t){.sample_every=1};
    bytes[0]=0xd4;bytes[1]=0;input.driver_packet_length=10;input.copied_length=10;
    assert(esp32_mquickjs_wifi_csi_callback_publish(&resources,&metadata,samples,4,&input,publish,NULL)==ESP32_MQUICKJS_WIFI_CSI_PUBLISH_ACCEPTED);
    /* ACK has one wire address, representing both receiver and destination;
     * it has no sequence, source, transmitter or BSSID. */
    assert(slot->metadata.rx_sequence==UINT32_MAX&&slot->metadata.address_mask==
        ((1U<<ESP32_MQUICKJS_WIFI_RX_RECEIVER)|(1U<<ESP32_MQUICKJS_WIFI_RX_DESTINATION)));
    assert(!memcmp(slot->metadata.addresses[ESP32_MQUICKJS_WIFI_RX_RECEIVER],bytes+4,6));
    assert(!memcmp(slot->metadata.addresses[ESP32_MQUICKJS_WIFI_RX_DESTINATION],bytes+4,6));
    assert(esp32_mquickjs_wifi_csi_slot_discard_event(&resources,&event));
    slot->slot_generation=UINT32_MAX-1;
    assert(esp32_mquickjs_wifi_csi_callback_publish(&resources,&metadata,samples,4,&input,publish,NULL)==ESP32_MQUICKJS_WIFI_CSI_PUBLISH_ACCEPTED);
    esp32_mquickjs_wifi_csi_event_t stale=event;assert(stale.slot_generation==UINT32_MAX);
    assert(esp32_mquickjs_wifi_csi_slot_discard_event(&resources,&event));
    assert(esp32_mquickjs_wifi_csi_callback_publish(&resources,&metadata,samples,4,&input,publish,NULL)==ESP32_MQUICKJS_WIFI_CSI_PUBLISH_IDENTITY_EXHAUSTED);
    assert(atomic_load(&resources.identity_exhausted)&&!atomic_load(&resources.counters.leased_frames));
    assert(esp32_mquickjs_native_pool_available(&resources.pool)==1);
    assert(!esp32_mquickjs_wifi_csi_slot_discard_event(&resources,&stale));
    esp32_mquickjs_wifi_csi_resources_set_accepting(&resources,true);
    assert(!atomic_load(&resources.accepting));
    assert(esp32_mquickjs_wifi_csi_resources_deinit(&resources));
    assert(esp32_mquickjs_wifi_csi_resources_init(&resources,100,1,4,0,&allocator));
    esp32_mquickjs_wifi_csi_resources_set_accepting(&resources,true);
    atomic_store(&resources.sequence,UINT32_MAX);
    assert(esp32_mquickjs_wifi_csi_callback_publish(&resources,&metadata,samples,4,&input,publish,NULL)==ESP32_MQUICKJS_WIFI_CSI_PUBLISH_IDENTITY_EXHAUSTED);
    assert(esp32_mquickjs_wifi_csi_resources_deinit(&resources));
    assert(!live);
}
int main(void){
    filter_and_identity();
    uint8_t native[160]={0},bytes[64]={8},samples[4]={1,2,3,4};size_t readable;
    esp32_mquickjs_wifi_csi_rx_span_t receipt;
    esp32_mquickjs_wifi_csi_rx_span_begin(&receipt,native,48,44);
    esp32_mquickjs_wifi_csi_rx_span_append(&receipt,native+44,4);
    assert(!esp32_mquickjs_wifi_csi_rx_span_finish(&receipt,native,native+48,&readable));
    esp32_mquickjs_wifi_csi_rx_span_append(&receipt,native+48,13);
    esp32_mquickjs_wifi_csi_rx_span_append(&receipt,native+61,27);
    assert(esp32_mquickjs_wifi_csi_rx_span_finish(&receipt,native,native+48,&readable)&&readable==40);
    assert(!esp32_mquickjs_wifi_csi_rx_span_finish(&receipt,native+1,native+48,&readable));
    esp32_mquickjs_wifi_csi_rx_span_append(&receipt,native+89,1);
    assert(!esp32_mquickjs_wifi_csi_rx_span_finish(&receipt,native,native+48,&readable));
    esp32_mquickjs_wifi_csi_rx_span_begin(&receipt,(void *)(UINTPTR_MAX-15),48,44);assert(!receipt.valid);
    esp32_mquickjs_wifi_csi_rx_span_begin(&receipt,native,48,44);
    esp32_mquickjs_wifi_csi_rx_span_append(&receipt,(void *)(UINTPTR_MAX-1),4);assert(!receipt.valid);

    esp32_mquickjs_wifi_csi_packet_options_t options={.mode=ESP32_MQUICKJS_WIFI_CSI_PACKET_FULL,.snap_length=36};
    esp32_mquickjs_wifi_csi_packet_input_t input={bytes,40,40,40};
    esp32_mquickjs_wifi_csi_packet_t packet;
    assert(esp32_mquickjs_wifi_csi_packet_prepare(&options,&input,&packet)==ESP32_MQUICKJS_WIFI_CSI_PACKET_OK);
    assert(packet.length==36&&packet.readable_length==40&&packet.header.header_length==24&&packet.truncated);
    options.require_complete=true;
    assert(esp32_mquickjs_wifi_csi_packet_prepare(&options,&input,&packet)==ESP32_MQUICKJS_WIFI_CSI_PACKET_INCOMPLETE);
    options.snap_length=64;input.copied_length=30;
    assert(esp32_mquickjs_wifi_csi_packet_prepare(&options,&input,&packet)==ESP32_MQUICKJS_WIFI_CSI_PACKET_INCOMPLETE);
    input.copied_length=64;
    assert(esp32_mquickjs_wifi_csi_packet_prepare(&options,&input,&packet)==ESP32_MQUICKJS_WIFI_CSI_PACKET_OK);
    assert(packet.length==40&&packet.readable_length==40&&!packet.truncated);
    options.require_complete=false;options.mode=ESP32_MQUICKJS_WIFI_CSI_PACKET_HEADER;
    assert(esp32_mquickjs_wifi_csi_packet_prepare(&options,&input,&packet)==ESP32_MQUICKJS_WIFI_CSI_PACKET_OK);
    assert(packet.length==24&&!packet.truncated);
    /* Addr4 + QoS + HT control: the SDK's fixed hdr+24 is not the header end. */
    bytes[0]=0x88;bytes[1]=0x83;
    assert(esp32_mquickjs_wifi_csi_packet_prepare(&options,&input,&packet)==ESP32_MQUICKJS_WIFI_CSI_PACKET_OK);
    assert(packet.length==36&&packet.header.qos_valid&&packet.header.ht_valid);
    bytes[0]=0xd4;bytes[1]=0;input.driver_packet_length=10;
    assert(esp32_mquickjs_wifi_csi_packet_prepare(&options,&input,&packet)==ESP32_MQUICKJS_WIFI_CSI_PACKET_OK&&packet.length==10);
    bytes[0]=8;input.driver_packet_length=40;
    /* Every short input ends at a protected page, proving parser read bounds. */
    size_t page=(size_t)sysconf(_SC_PAGESIZE);
    uint8_t *guard=mmap(NULL,page*2,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);assert(guard!=MAP_FAILED);
    assert(!mprotect(guard+page,page,PROT_NONE));
    for(size_t n=0;n<24;n++){
        uint8_t *p=guard+page-n;if(n){memset(p,0,n);p[0]=8;}
        esp32_mquickjs_wifi_csi_packet_input_t short_input={p,n,40,40};
        assert(esp32_mquickjs_wifi_csi_packet_prepare(&options,&short_input,&packet)!=ESP32_MQUICKJS_WIFI_CSI_PACKET_OK);
    }
    munmap(guard,page*2);

    esp32_mquickjs_wifi_csi_store_t store={.lock=lock,.unlock=lock,.maximum_slots=2,.max_frame_bytes=4,.next_generation=1};
    esp32_mquickjs_wifi_csi_allocator_t allocator={allocate, allocate_bytes, release, NULL, NULL};
    esp32_mquickjs_wifi_csi_store_result_t result;
    for(unsigned nth=1;nth<=3;nth++){
        calls=0;fail_at=nth;
        assert(!esp32_mquickjs_wifi_csi_store_open(&store,2,40,&allocator,&result));
        assert(result==ESP32_MQUICKJS_WIFI_CSI_STORE_MEMORY&&!live&&!store.reserved_slots);
    }
    fail_at=0;
    esp32_mquickjs_wifi_csi_resources_t *resources=esp32_mquickjs_wifi_csi_store_open(&store,1,40,&allocator,&result);assert(resources);
    size_t control,data;
    assert(esp32_mquickjs_wifi_csi_resources_size(1,4,40,&control,&data));
    assert(data==44&&control==sizeof(esp32_mquickjs_wifi_csi_slot_t)+sizeof(esp32_mquickjs_wifi_csi_packet_t));
    assert(!esp32_mquickjs_wifi_csi_resources_size(128,4,16385,&control,&data));
    resources->packet_options=(esp32_mquickjs_wifi_csi_packet_options_t){.mode=ESP32_MQUICKJS_WIFI_CSI_PACKET_FULL,.snap_length=40,.required=true};
    esp32_mquickjs_wifi_csi_resources_set_accepting(resources,true);
    esp32_mquickjs_wifi_csi_metadata_t metadata={.channel=6,.radio_generation=17,.timestamp_us=123,
        .layout={.byte_length=4,.segment_count=1,.segments={{.length_bytes=4}}}};
    input.bytes=bytes;input.copied_length=40;
    assert(esp32_mquickjs_wifi_csi_callback_publish(resources,&metadata,samples,4,NULL,publish,NULL)==ESP32_MQUICKJS_WIFI_CSI_PUBLISH_PACKET_REQUIRED);
    assert(atomic_load(&resources->counters.packet_unavailable)==1&&atomic_load(&resources->counters.dropped_packet_required)==1);
    assert(esp32_mquickjs_wifi_csi_callback_publish(resources,&metadata,samples,4,&input,publish,NULL)==ESP32_MQUICKJS_WIFI_CSI_PUBLISH_ACCEPTED);
    esp32_mquickjs_wifi_csi_slot_t *slot=esp32_mquickjs_wifi_csi_slot_from_event(resources,&event);assert(slot);
    assert(slot->packet&&slot->packet->length==40&&!memcmp(slot->packet->bytes,bytes,40));
    esp32_mquickjs_wifi_csi_wire_snapshot_t snapshot;
    assert(esp32_mquickjs_wifi_csi_wire_snapshot(slot,&snapshot));
    assert(snapshot.frame.packet_length==40&&snapshot.frame.csi_length==4&&snapshot.frame.captured_header_length==24);
    assert(snapshot.metadata.fcs_state==0&&snapshot.metadata.driver_packet_length==40);
    esp32_mquickjs_wifi_rx_wire_frame_t frame=snapshot.frame;
    esp32_mquickjs_wifi_rx_wire_offsets_t offsets;
    assert(esp32_mquickjs_wifi_rx_wire_offsets(ESP32_MQUICKJS_WIFI_RX_WIRE_CSI,&frame,1,0,&offsets));
    assert(offsets.csi_offset==328&&offsets.packet_offset==332&&offsets.end_offset==372);
    assert(esp32_mquickjs_wifi_csi_slot_take_event_owner(resources,&event));
    assert(esp32_mquickjs_wifi_csi_slot_retain(resources,slot));
    esp32_mquickjs_wifi_csi_resources_set_accepting(resources,false);
    assert(esp32_mquickjs_wifi_csi_store_retire(&store,resources));
    esp32_mquickjs_wifi_csi_resources_t *next=esp32_mquickjs_wifi_csi_store_open(&store,1,0,&allocator,&result);assert(next);
    assert(next->slots[0].packet==NULL&&next->generation!=resources->generation);
    assert(esp32_mquickjs_wifi_csi_slot_close_public_owner(resources,slot));
    assert(!memcmp(slot->packet->bytes,bytes,40));
    assert(esp32_mquickjs_wifi_csi_slot_release(resources,slot));
    /* A temporary pin collects the retained generation only after its final owner. */
    assert(esp32_mquickjs_wifi_csi_store_acquire(&store,resources->generation)==resources);
    esp32_mquickjs_wifi_csi_store_release(&store,resources);
    assert(store.reserved_slots==1);
    assert(esp32_mquickjs_wifi_csi_store_retire(&store,next));
    assert(!store.reserved_slots&&!live);
    return 0;
}
