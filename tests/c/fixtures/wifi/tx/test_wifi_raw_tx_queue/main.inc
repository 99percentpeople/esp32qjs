
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
