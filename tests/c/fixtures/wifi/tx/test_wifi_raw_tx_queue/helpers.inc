
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
