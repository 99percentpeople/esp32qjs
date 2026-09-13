
#include "esp32_mquickjs_wifi_csi_store.h"
#include <assert.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
static pthread_mutex_t mutex=PTHREAD_MUTEX_INITIALIZER;
static _Thread_local unsigned depth;
static void lock(void *p){(void)p;assert(!depth);assert(!pthread_mutex_lock(&mutex));depth++;}
static void unlock(void *p){(void)p;assert(depth==1);depth--;assert(!pthread_mutex_unlock(&mutex));}
static esp32_mquickjs_wifi_csi_store_t store={.lock=lock,.unlock=unlock,.maximum_slots=4,.max_frame_bytes=64,.next_generation=1};
static unsigned calls,fail_at,live;
static void *recycle_trigger,*recycled;
static esp32_mquickjs_wifi_csi_resources_t *open_pool(unsigned capacity);
static pthread_mutex_t pause_mutex=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t changed=PTHREAD_COND_INITIALIZER;
static bool pause_free,paused,proceed;
static void *allocate(size_t n,size_t size,void *p){
    (void)p;assert(!depth);if(++calls==fail_at)return NULL;
    void *result;
    if(recycled){assert(n==1 && size==sizeof(esp32_mquickjs_wifi_csi_resources_t));result=recycled;recycled=NULL;memset(result,0,size);}
    else result=calloc(n,size);
    assert(result);live++;return result;
}
static void *payload(size_t size,void *p){return allocate(1,size,p);}
static void release(void *p,void *opaque){
    (void)opaque;assert(!depth && p && live);
    if(p==recycle_trigger){
        /* Deterministically emulate immediate reuse at allocator return. The
         * retiring registry tombstone must not hide this new live control. */
        recycle_trigger=NULL;recycled=p;live--;
        esp32_mquickjs_wifi_csi_resources_t *replacement=open_pool(1);assert(replacement==p && !recycled);
        assert(esp32_mquickjs_wifi_csi_store_retire(&store,replacement));
        return;
    }
    free(p);live--;
    assert(!pthread_mutex_lock(&pause_mutex));
    if(pause_free){pause_free=false;paused=true;assert(!pthread_cond_broadcast(&changed));
        while(!proceed)assert(!pthread_cond_wait(&changed,&pause_mutex));}
    assert(!pthread_mutex_unlock(&pause_mutex));
}
static atomic_uint retirement_notifications;
static void retire_allocation(void *p,void *opaque){
    (void)opaque;assert(p && !depth);
    bool pinned=false;
    lock(NULL);
    for(size_t i=0;i<ESP32_MQUICKJS_WIFI_CSI_STORE_MAX_GENERATIONS;++i){
        esp32_mquickjs_wifi_csi_store_entry_t *entry=&store.entries[i];
        if(entry->state!=ESP32_MQUICKJS_WIFI_CSI_STORE_RETAINED)continue;
        esp32_mquickjs_wifi_csi_resources_t *r=entry->resources;
        if(p==r || p==r->slots || p==r->payload_storage){assert(entry->pins);pinned=true;break;}
    }
    unlock(NULL);
    assert(pinned);atomic_fetch_add(&retirement_notifications,1);
}
static esp32_mquickjs_wifi_csi_allocator_t allocator={allocate, payload, release, NULL, retire_allocation};
static esp32_mquickjs_wifi_csi_store_result_t result;
static esp32_mquickjs_wifi_csi_event_t event;
static bool publish(const esp32_mquickjs_wifi_csi_event_t *e,void *p){(void)p;event=*e;return true;}
static esp32_mquickjs_wifi_csi_resources_t *open_pool(unsigned capacity){
    return esp32_mquickjs_wifi_csi_store_open(&store,capacity,0,&allocator,&result);
}
static void *retire_thread(void *p){assert(esp32_mquickjs_wifi_csi_store_retire(&store,p));return NULL;}
int main(void){
    for(unsigned nth=1;nth<=3;nth++){
        calls=0;fail_at=nth;assert(!open_pool(2));
        assert(result==ESP32_MQUICKJS_WIFI_CSI_STORE_MEMORY && !live && !store.reserved_slots);
    }
    calls=fail_at=0;
    assert(!open_pool(0) && result==ESP32_MQUICKJS_WIFI_CSI_STORE_INVALID && !calls);
    assert(!open_pool(UINT32_MAX) && result==ESP32_MQUICKJS_WIFI_CSI_STORE_INVALID && !calls);
    esp32_mquickjs_wifi_csi_resources_t *old=open_pool(2);assert(old && live==3);
    unsigned old_generation=old->generation;
    esp32_mquickjs_wifi_csi_resources_set_accepting(old,true);
    assert(esp32_mquickjs_wifi_csi_callback_enter(old));
    assert(!esp32_mquickjs_wifi_csi_store_retire(&store,old));
    assert(atomic_load(&retirement_notifications)==0);
    uint8_t bytes[]={2,4,6,8};esp32_mquickjs_wifi_csi_metadata_t metadata={0};
    assert(esp32_mquickjs_wifi_csi_callback_publish(old,&metadata,bytes,4,NULL,publish,NULL)==ESP32_MQUICKJS_WIFI_CSI_PUBLISH_ACCEPTED);
    esp32_mquickjs_wifi_csi_callback_leave(old);
    assert(esp32_mquickjs_wifi_csi_slot_take_event_owner(old,&event));
    esp32_mquickjs_wifi_csi_slot_t *slot=esp32_mquickjs_wifi_csi_slot_from_event(old,&event);assert(slot);
    assert(esp32_mquickjs_wifi_csi_slot_retain(old,slot));
    assert(esp32_mquickjs_wifi_csi_slot_close_public_owner(old,slot));
    esp32_mquickjs_wifi_csi_resources_set_accepting(old,false);
    assert(esp32_mquickjs_wifi_csi_store_retire(&store,old));assert(!esp32_mquickjs_wifi_csi_store_retire(&store,old));
    assert(atomic_load(&retirement_notifications)==3);
    esp32_mquickjs_wifi_csi_resources_t *current=open_pool(2);assert(current && current->generation!=old_generation);
    unsigned before=calls;assert(!open_pool(1) && result==ESP32_MQUICKJS_WIFI_CSI_STORE_BUDGET && calls==before);
    esp32_mquickjs_wifi_csi_store_snapshot_t snapshots[8];
    assert(esp32_mquickjs_wifi_csi_store_snapshot(&store,snapshots)==2);
    assert(snapshots[0].state==ESP32_MQUICKJS_WIFI_CSI_STORE_RETAINED && snapshots[0].leased_frames==1);
    assert(snapshots[1].state==ESP32_MQUICKJS_WIFI_CSI_STORE_ACTIVE);
    unsigned sequence=atomic_load(&old->sequence),next_generation=store.next_generation;
    assert(esp32_mquickjs_wifi_csi_store_reset_counters(&store)==0);
    assert(!atomic_load(&old->counters.accepted) && atomic_load(&old->counters.leased_frames)==1);
    assert(atomic_load(&old->sequence)==sequence && store.next_generation==next_generation);
    assert(store.reserved_slots==4 && esp32_mquickjs_wifi_csi_store_snapshot(&store,snapshots)==2);
    assert(snapshots[0].state==ESP32_MQUICKJS_WIFI_CSI_STORE_RETAINED && snapshots[0].leased_frames==1);
    old=esp32_mquickjs_wifi_csi_store_acquire(&store,old_generation);assert(old);
    slot=esp32_mquickjs_wifi_csi_slot_from_event(old,&event);assert(slot && !memcmp(slot->payload,bytes,4));
    assert(!esp32_mquickjs_wifi_csi_slot_from_event(current,&event));
    assert(esp32_mquickjs_wifi_csi_slot_release(old,slot));
    esp32_mquickjs_wifi_csi_store_release(&store,old);
    assert(!esp32_mquickjs_wifi_csi_store_acquire(&store,old_generation) && live==3 && store.reserved_slots==2);
    esp32_mquickjs_wifi_csi_resources_t *pin=esp32_mquickjs_wifi_csi_store_acquire(&store,current->generation);assert(pin==current);
    assert(esp32_mquickjs_wifi_csi_store_retire(&store,current));assert(live==3);
    esp32_mquickjs_wifi_csi_store_release(&store,pin);assert(!live && !store.reserved_slots);

    /* Exact allocator return barrier, while another thread snapshots and
     * attempts admission. The native registry and real deinit are executed. */
    current=open_pool(4);assert(current);unsigned retiring_generation=current->generation;
    pause_free=true;paused=proceed=false;pthread_t thread;
    assert(!pthread_create(&thread,NULL,retire_thread,current));
    assert(!pthread_mutex_lock(&pause_mutex));while(!paused)assert(!pthread_cond_wait(&changed,&pause_mutex));
    assert(!pthread_mutex_unlock(&pause_mutex));
    assert(esp32_mquickjs_wifi_csi_store_snapshot(&store,snapshots)==1);
    assert(snapshots[0].state==ESP32_MQUICKJS_WIFI_CSI_STORE_RETIRING && snapshots[0].bytes>256);
    assert(esp32_mquickjs_wifi_csi_store_reset_counters(&store)==1);
    assert(store.reserved_slots==4); /* no access to already freed pool storage */
    assert(!esp32_mquickjs_wifi_csi_store_acquire(&store,retiring_generation));
    before=calls;assert(!open_pool(1) && result==ESP32_MQUICKJS_WIFI_CSI_STORE_BUDGET && calls==before);
    assert(!pthread_mutex_lock(&pause_mutex));proceed=true;assert(!pthread_cond_broadcast(&changed));assert(!pthread_mutex_unlock(&pause_mutex));
    assert(!pthread_join(thread,NULL));assert(!live && !store.reserved_slots);

    current=open_pool(1);assert(current);recycle_trigger=current;
    assert(esp32_mquickjs_wifi_csi_store_retire(&store,current));
    assert(!live && !store.reserved_slots && !recycled && !recycle_trigger);

    store.maximum_slots=16;esp32_mquickjs_wifi_csi_resources_t *all[8];
    for(unsigned i=0;i<8;i++){all[i]=open_pool(1);assert(all[i]);}
    before=calls;assert(!open_pool(1) && result==ESP32_MQUICKJS_WIFI_CSI_STORE_BUDGET && before==calls);
    lock(NULL);store.entries[0].pins=UINT32_MAX;unlock(NULL);
    assert(!esp32_mquickjs_wifi_csi_store_acquire(&store,all[0]->generation));
    lock(NULL);store.entries[0].pins=1;unlock(NULL);
    for(unsigned i=0;i<8;i++)assert(esp32_mquickjs_wifi_csi_store_retire(&store,all[i]));
    assert(!live && !store.reserved_slots);
    store.next_generation=UINT32_MAX;current=open_pool(1);assert(current && current->generation==UINT32_MAX);
    assert(esp32_mquickjs_wifi_csi_store_retire(&store,current));before=calls;
    assert(!open_pool(1) && result==ESP32_MQUICKJS_WIFI_CSI_STORE_IDENTITY && calls==before);
    assert(!live && !store.reserved_slots && store.next_generation==0);
    assert(esp32_mquickjs_wifi_csi_store_snapshot(&store,snapshots)==0);
}
