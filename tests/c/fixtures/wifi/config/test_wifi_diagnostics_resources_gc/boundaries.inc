
#include <stdatomic.h>
typedef struct {uint64_t callbacks,accepted,dropped_pool_full,dropped_queue_full,dropped_closing;
    uint32_t leased_frames,publishers;} monitor_counts_t;
typedef struct {bool accepting,identity_exhausted;size_t allocated_bytes;uint32_t free_slots;
    monitor_counts_t counters;} monitor_resources_t;
typedef struct {uint32_t generation;bool closed,close_requested,retirement_blocked;
    monitor_resources_t resources;} esp32_mquickjs_wifi_monitor_diagnostic_t;
typedef struct {char control[29];} esp32_mquickjs_wifi_monitor_session_t;
#define ESP32_MQUICKJS_WIFI_MONITOR_MAX_SESSIONS 8U
static int state,locked,observations;
static size_t esp32_mquickjs_wifi_monitor_diagnostics_snapshot(
    esp32_mquickjs_wifi_monitor_diagnostic_t output[8],uint32_t *unavailable){
    assert(!locked && !observations++);*unavailable=1;
    for(unsigned i=0;i<8;i++)output[i]=(esp32_mquickjs_wifi_monitor_diagnostic_t){
        .generation=100+i,.closed=i>0,.close_requested=i>0,.retirement_blocked=i==7,
        .resources={.accepting=i==0,.allocated_bytes=512+i,.free_slots=2,
            .counters={.callbacks=(UINT64_C(1)<<40)+i,.accepted=10+i,.leased_frames=1}}};
    return 8;
}
typedef enum {WIFI_CSI_CLOSED,WIFI_CSI_OPENING,WIFI_CSI_RUNNING,WIFI_CSI_STOPPING,
    WIFI_CSI_STOPPED,WIFI_CSI_FAULTED} wifi_csi_lifecycle_t;
#include "esp32_mquickjs_wifi_csi_store.h"
static struct {uint32_t generation;wifi_csi_lifecycle_t lifecycle;
    bool cleanup_scheduled,close_requested;} s_wifi_csi;
static int s_wifi_csi_store_lock;
static size_t esp32_mquickjs_wifi_csi_rx_native_control_bytes(void){return 128;}
static esp32_mquickjs_wifi_csi_store_t s_wifi_csi_store={.maximum_slots=64,.next_generation=9};
size_t esp32_mquickjs_wifi_csi_store_snapshot(
    esp32_mquickjs_wifi_csi_store_t *store,esp32_mquickjs_wifi_csi_store_snapshot_t output[8]){
    assert(store==&s_wifi_csi_store && !locked && !observations++);
    if(state==3)return 0;
    for(unsigned i=0;i<8;i++)output[i]=(esp32_mquickjs_wifi_csi_store_snapshot_t){
        .generation=71+i,.capacity=4,.bytes=512+i,.free_slots=3,.leased_frames=1,.callbacks=8+i,.accepted=6+i,
        .state=i?ESP32_MQUICKJS_WIFI_CSI_STORE_RETAINED:
            state==0?ESP32_MQUICKJS_WIFI_CSI_STORE_ACTIVE:state==1?ESP32_MQUICKJS_WIFI_CSI_STORE_RETAINED:
            state==2?ESP32_MQUICKJS_WIFI_CSI_STORE_RETIRING:ESP32_MQUICKJS_WIFI_CSI_STORE_ALLOCATING};
    return 8;
}

