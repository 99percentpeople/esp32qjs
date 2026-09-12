"""Deferred production FIFO/ownership/fenced ledger tests; no replacement model."""
import unittest
from test_wifi_rx_target import ROOT, INTERNAL, unit
from test_wireless_control_regression import compile_run


class WiFiRawTxQueue(unittest.TestCase):
    def test_atomic_batches_protected_active_batch_and_flush_fences(self):
        code = PRELUDE
        code += unit(INTERNAL / 'esp32_mquickjs_wifi_raw_tx_queue.h')
        code += unit(ROOT / 'components/esp32_mquickjs/src/modules/wifi_raw_tx/esp32_mquickjs_wifi_raw_tx_queue.c')
        compile_run(self, code + HELPERS + MAIN)


PRELUDE = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
'''

HELPERS = r'''
#define api(name) esp32_mquickjs_wifi_raw_tx_queue_##name
#define OK ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_OK
#define INVALID ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_INVALID
#define FULL ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_FULL
#define EXHAUSTED ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_EXHAUSTED
#define REJECT ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_REJECT_NEWEST
#define DROP ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_DROP_OLDEST_BATCH
#define SUCCESS ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_SUCCESS
#define FAILED ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_FAILED
#define UNKNOWN ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_UNKNOWN
#define REJECTED ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_REJECTED
#define ABORTED ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_ABORTED
typedef esp32_mquickjs_wifi_raw_tx_ticket_t ticket_t;
typedef esp32_mquickjs_wifi_raw_tx_flush_token_t flush_t;
typedef esp32_mquickjs_wifi_raw_tx_flush_status_t flush_status_t;
typedef esp32_mquickjs_wifi_raw_tx_admission_t admission_t;
static void *owned[512];
static unsigned live,serial;
static payload_t payload(void) {
    uint8_t *data=malloc(24);assert(data);memset(data,0,24);data[0]=0x80;data[23]=++serial;
    for(unsigned i=0;i<512;i++)if(!owned[i]) {owned[i]=data;++live;return (payload_t){data,24};}
    assert(0);return (payload_t){0};
}
static void release(payload_t *value) {
    assert(value->data && value->length==24);
    for(unsigned i=0;i<512;i++)if(owned[i]==value->data) {
        owned[i]=NULL;--live;free(value->data);memset(value,0,sizeof(*value));return;
    }
    assert(!"duplicate release or unknown payload");
}
static void audit(const queue_t *q) {
    assert(q->initialized && q->totals.admitted==q->last_sequence);
    bool seen[MAX_PACKETS]={0};unsigned queued=0,allocated=0;uint32_t previous=0;
    for(uint16_t i=q->pending_head;i!=NONE;i=q->slots[i].next) {
        assert(i<q->capacity && !seen[i] && q->slots[i].allocated && i!=q->active);
        seen[i]=true;++queued;assert(q->slots[i].sequence>previous);previous=q->slots[i].sequence;
        assert(!q->slots[i].accepted && q->slots[i].payload.data && q->slots[i].payload.length==24);
    }
    assert(queued==q->queued);
    for(unsigned i=0;i<q->capacity;i++) {
        if(q->slots[i].allocated){++allocated;assert(seen[i] || i==q->active);}
        else assert(!q->slots[i].payload.data);
    }
    assert(allocated+q->free_count==q->capacity);
    assert(allocated==q->queued+(q->active!=NONE));
    const totals_t *t=&q->totals;
    assert(t->settled==t->succeeded+t->failed+t->unknown+t->rejected+t->aborted+t->dropped);
    assert(t->admitted-t->settled==allocated && t->submitted<=t->admitted);
    assert(t->succeeded+t->failed+t->unknown<=t->submitted);
    for(unsigned i=0;i<MAX_FLUSHES;i++)if(q->flushes[i].identity) {
        const flush_status_t *s=&q->flushes[i].status;
        assert(s->fence==s->totals.admitted && s->pending==s->totals.admitted-s->totals.settled);
    }
}
static admission_t admit(queue_t *q,unsigned count,esp32_mquickjs_wifi_raw_tx_queue_overflow_t overflow) {
    payload_t frames[MAX_PACKETS]={0},removed[MAX_PACKETS]={0};admission_t result;
    for(unsigned i=0;i<count;i++)frames[i]=payload();
    assert(api(admit)(q,frames,count,overflow,removed,MAX_PACKETS,&result)==OK);
    for(unsigned i=0;i<count;i++)assert(!frames[i].data && !frames[i].length);
    for(unsigned i=0;i<result.evicted_packets;i++)release(&removed[i]);
    audit(q);return result;
}
static ticket_t take(queue_t *q) {
    ticket_t ticket={0};payload_t borrowed={0};
    assert(api(take)(q,&ticket,&borrowed) && borrowed.data && borrowed.length==24);
    audit(q);return ticket;
}
static void finish(queue_t *q,ticket_t *ticket,esp32_mquickjs_wifi_raw_tx_queue_outcome_t outcome) {
    ticket_t stale=*ticket;payload_t retired={0};
    assert(api(finish)(q,ticket,outcome,&retired) && !ticket->generation && !ticket->sequence);
    release(&retired);audit(q);
    assert(!api(finish)(q,&stale,outcome,&retired));assert(!api(accept)(q,&stale));
}
static flush_status_t flush_status(queue_t *q,const flush_t *token) {
    flush_status_t result;assert(api(flush_status)(q,token,&result));return result;
}
static void close_queue(queue_t *q) {
    payload_t removed[MAX_PACKETS]={0};uint16_t count=UINT16_MAX;
    assert(api(close)(q,removed,MAX_PACKETS,&count));
    for(unsigned i=0;i<count;i++)release(&removed[i]);
    audit(q);assert(api(close)(q,removed,MAX_PACKETS,&count) && count==0);
}
/* Failed admissions preserve the production registry, every descriptor and the
 * caller receipt. This is an object-representation snapshot, not another FIFO. */
static void rejects(queue_t *q,payload_t *frames,uint16_t count,esp32_mquickjs_wifi_raw_tx_queue_overflow_t overflow,
                    esp32_mquickjs_wifi_raw_tx_queue_result_t expected) {
    queue_t before;slot_t old_slots[MAX_PACKETS];payload_t old_frames[MAX_PACKETS],removed[MAX_PACKETS]={0};
    admission_t receipt,old_receipt;memset(&receipt,0xa5,sizeof(receipt));memcpy(&old_receipt,&receipt,sizeof(receipt));
    memcpy(&before,q,sizeof(*q));memcpy(old_slots,q->slots,q->capacity*sizeof(slot_t));
    memcpy(old_frames,frames,count*sizeof(payload_t));
    assert(api(admit)(q,frames,count,overflow,removed,MAX_PACKETS,&receipt)==expected);
    assert(!memcmp(&before,q,sizeof(*q)) && !memcmp(old_slots,q->slots,q->capacity*sizeof(slot_t)));
    assert(!memcmp(old_frames,frames,count*sizeof(payload_t)) && !memcmp(&receipt,&old_receipt,sizeof(receipt)));
    for(unsigned i=0;i<MAX_PACKETS;i++)assert(!removed[i].data && !removed[i].length);
}
'''

MAIN = r'''
int main(void) {
    queue_t q={0};slot_t slots[MAX_PACKETS];
    assert(!api(init)(&q,slots,0,7) && !api(init)(&q,slots,MAX_PACKETS+1,7));
    assert(!api(init)(&q,slots,4,0));assert(api(init)(&q,slots,4,7));assert(!api(init)(&q,slots,4,8));
    flush_t empty={0};assert(api(flush_begin)(&q,&empty)==OK);
    assert(flush_status(&q,&empty).pending==0 && flush_status(&q,&empty).fence==0);
    assert(api(flush_release)(&q,&empty));
    payload_t bad[2]={payload(),payload()};
    rejects(&q,bad,2,(esp32_mquickjs_wifi_raw_tx_queue_overflow_t)99,INVALID);
    uint16_t length=bad[0].length;bad[0].length=23;rejects(&q,bad,2,REJECT,INVALID);bad[0].length=length;
    payload_t saved=bad[1];bad[1]=bad[0];rejects(&q,bad,2,REJECT,INVALID);bad[1]=saved;
    saved=bad[0];bad[0].data=(uint8_t *)(uintptr_t)(UINTPTR_MAX-8);rejects(&q,bad,2,REJECT,INVALID);bad[0]=saved;
    release(&bad[0]);release(&bad[1]);
    admission_t a=admit(&q,2,REJECT),b=admit(&q,1,REJECT),c=admit(&q,1,REJECT);
    assert(a.first_sequence==1 && a.last_sequence==2 && b.first_sequence==3 && c.first_sequence==4);
    ticket_t active=take(&q),old_active=active;
    payload_t retired={0};
    assert(!api(finish)(&q,&active,SUCCESS,&retired));
    assert(api(accept)(&q,&active) && !api(accept)(&q,&active));
    ticket_t wrong=active;++wrong.generation;assert(!api(finish)(&q,&wrong,FAILED,&retired));
    finish(&q,&active,SUCCESS);
    /* Active batch remains protected between its two packets. */
    assert(q.active==NONE && q.active_batch==1 && q.queued==3);
    flush_t first={0};assert(api(flush_begin)(&q,&first)==OK);
    assert(flush_status(&q,&first).pending==3);
    payload_t large[4]={payload(),payload(),payload(),payload()};
    rejects(&q,large,4,DROP,FULL); /* Even eviction cannot remove the active remainder. */
    rejects(&q,large,3,REJECT,FULL);
    for(unsigned i=0;i<4;i++)release(&large[i]);
    admission_t d=admit(&q,3,DROP);
    assert(d.first_sequence==5 && d.last_sequence==7 && d.evicted_batches==2 && d.evicted_packets==2);
    flush_status_t first_status=flush_status(&q,&first);
    assert(first_status.fence==4 && first_status.pending==1 && first_status.totals.dropped==2);
    flush_t second={0};assert(api(flush_begin)(&q,&second)==OK);
    active=take(&q);assert(active.sequence==2 && api(accept)(&q,&active));finish(&q,&active,UNKNOWN);
    first_status=flush_status(&q,&first);
    assert(first_status.pending==0 && first_status.totals.admitted==4 && first_status.totals.settled==4);
    assert(first_status.totals.succeeded==1 && first_status.totals.unknown==1 && first_status.totals.submitted==2);
    active=take(&q);assert(active.sequence==5 && api(accept)(&q,&active));
    assert(!api(finish)(&q,&old_active,SUCCESS,&retired));
    /* Close drops the remainder but retains the active native payload. */
    uint8_t *active_bytes=q.slots[q.active].payload.data;unsigned before_live=live;
    close_queue(&q);assert(q.active!=NONE && q.slots[q.active].payload.data==active_bytes && live==before_live-2);
    assert(!api(deinit)(&q));
    assert(flush_status(&q,&second).pending==1);
    finish(&q,&active,ABORTED);
    flush_status_t after=flush_status(&q,&first);
    assert(first_status.generation==after.generation && first_status.fence==after.fence && first_status.pending==after.pending);
    assert(!memcmp(&first_status.totals,&after.totals,sizeof(after.totals)));
    assert(flush_status(&q,&second).pending==0 && flush_status(&q,&second).totals.aborted==1);
    assert(flush_status(&q,&second).totals.dropped==4);
    assert(!api(deinit)(&q));
    flush_t stale=first;assert(api(flush_release)(&q,&first));assert(!api(flush_release)(&q,&stale));
    assert(api(flush_release)(&q,&second) && api(deinit)(&q) && live==0);
    /* New Session generation must reject an old packet with the same sequence. */
    assert(api(init)(&q,slots,2,8));admit(&q,1,REJECT);active=take(&q);
    assert(active.sequence==old_active.sequence && !api(accept)(&q,&old_active));
    finish(&q,&active,REJECTED);close_queue(&q);assert(api(deinit)(&q));
    /* Full watcher registry; exact stale tokens cannot release a replacement. */
    assert(api(init)(&q,slots,2,9));flush_t watches[MAX_FLUSHES+1]={0};
    for(unsigned i=0;i<MAX_FLUSHES;i++)assert(api(flush_begin)(&q,&watches[i])==OK);
    assert(api(flush_begin)(&q,&watches[MAX_FLUSHES])==FULL);
    stale=watches[3];assert(api(flush_release)(&q,&watches[3]));
    assert(api(flush_begin)(&q,&watches[3])==OK && watches[3].identity!=stale.identity);
    assert(!api(flush_release)(&q,&stale));
    close_queue(&q);assert(!api(deinit)(&q));
    for(unsigned i=0;i<MAX_FLUSHES;i++)assert(api(flush_release)(&q,&watches[i]));
    assert(api(deinit)(&q));
    /* Seed production counters to reach exhaustion without billions of sends. */
    assert(api(init)(&q,slots,2,10));
    q.last_sequence=q.totals.admitted=q.totals.settled=q.totals.dropped=UINT32_MAX-1;
    payload_t end[2]={payload(),payload()};rejects(&q,end,2,REJECT,EXHAUSTED);
    release(&end[0]);release(&end[1]);
    admission_t last=admit(&q,1,REJECT);assert(last.last_sequence==UINT32_MAX);
    end[0]=payload();rejects(&q,end,1,DROP,EXHAUSTED);release(&end[0]);
    q.next_flush_identity=UINT32_MAX;flush_t last_flush={0},overflow_flush={0};
    assert(api(flush_begin)(&q,&last_flush)==OK && last_flush.identity==UINT32_MAX);
    assert(api(flush_begin)(&q,&overflow_flush)==EXHAUSTED);
    close_queue(&q);assert(flush_status(&q,&last_flush).pending==0 && flush_status(&q,&last_flush).totals.settled==UINT32_MAX);
    assert(api(flush_release)(&q,&last_flush) && api(deinit)(&q));
    /* Full configured capacity and all callback terminal categories. */
    assert(api(init)(&q,slots,MAX_PACKETS,11));admit(&q,MAX_PACKETS,REJECT);
    flush_t maximum={0};assert(api(flush_begin)(&q,&maximum)==OK);
    for(unsigned i=0;i<MAX_PACKETS;i++) {
        active=take(&q);assert(api(accept)(&q,&active));
        finish(&q,&active,(esp32_mquickjs_wifi_raw_tx_queue_outcome_t)(i%3));
    }
    assert(flush_status(&q,&maximum).pending==0 && q.totals.submitted==MAX_PACKETS);
    close_queue(&q);assert(api(flush_release)(&q,&maximum) && api(deinit)(&q));
    assert(!live);
    return 0;
}
'''
