
#include "esp32_mquickjs_wifi_raw_tx_periodic.h"
#include <assert.h>
#include <limits.h>
#include <string.h>
#define api(name) esp32_mquickjs_wifi_raw_tx_periodic_##name
#define E(name) ESP32_MQUICKJS_WIFI_RAW_TX_PERIODIC_##name
typedef esp32_mquickjs_wifi_raw_tx_periodic_t ledger_t;
typedef esp32_mquickjs_wifi_raw_tx_periodic_options_t options_t;
typedef esp32_mquickjs_wifi_raw_tx_periodic_ticket_t ticket_t;

static ledger_t create(uint32_t generation, uint32_t count, bool stop, bool stop_on_error) {
    ledger_t p={0}; options_t options={.interval_us=1000,.start_delay_us=500,
        .count=count,.busy=stop ? E(STOP) : E(SKIP),.stop_on_error=stop_on_error};
    assert(api(init)(&p,generation,&options,100));return p;
}
static void complete(ledger_t *p,ticket_t *t) {
    assert(api(submitted)(p,t));assert(!api(submitted)(p,t));
    assert(!api(finish)(p,t,E(ABORTED)));assert(api(finish)(p,t,E(SUCCESS)));
    assert(!t->sequence && !p->active.sequence);assert(!api(finish)(p,t,E(SUCCESS)));
}
static void invariant(const ledger_t *p) {
    assert(p->scheduled==p->issued+p->skipped_busy+p->skipped_late);
    assert(p->submitted<=p->issued && p->completed<=p->submitted);
    assert(p->issued==p->completed+p->rejected+p->aborted+p->dropped+(p->active.sequence!=0));
}
int main(void) {
    ledger_t p=create(1,0,false,false),saved=p;ticket_t t={0},other={0};
    options_t invalid={.interval_us=999}; ledger_t zero={0};
    assert(!api(init)(&zero,2,&invalid,0) && !zero.generation);
    invalid.interval_us=1000;invalid.start_delay_us=1;
    assert(!api(init)(&zero,2,&invalid,INT64_MAX));
    assert(!api(init)(&p,3,&invalid,0) && !memcmp(&saved,&p,sizeof(p)));
    assert(!api(init)(&zero,0,&invalid,0));assert(!api(init)(&zero,1,&invalid,-1));
    assert(api(due)(&p,599,false,&t)==E(NOT_DUE));
    assert(api(due)(&p,600,false,&t)==E(ISSUE) && t.generation==1 && t.sequence==1);
    assert(!api(drained)(&p)); ticket_t first=t;
    assert(api(due)(&p,1600,false,&other)==E(SKIPPED) && !other.sequence && p.skipped_busy==1);
    assert(!api(finish)(&p,&t,E(SUCCESS))); /* reservation is not driver acceptance */
    complete(&p,&t);invariant(&p);
    assert(api(due)(&p,2600,false,&t)==E(ISSUE));
    saved=p;assert(!api(submitted)(&p,&first) && !memcmp(&saved,&p,sizeof(p)));
    ticket_t stale=t;stale.generation=99;assert(!api(finish)(&p,&stale,E(REJECTED)));
    assert(!api(submitted)(&p,&p.active));
    assert(api(due)(&p,3600,false,(ticket_t *)&p.scheduled)==E(INVALID));
    complete(&p,&t);invariant(&p);
    /* One late observation consumes 4 deadlines, but issues one packet only. */
    p=create(2,0,false,false);
    assert(api(due)(&p,3999,false,&t)==E(ISSUE));
    assert(p.scheduled==4 && p.issued==1 && p.skipped_late==3 && p.next_due_us==4600);
    complete(&p,&t);invariant(&p);
    /* Busy skip keeps the absolute cadence. Busy stop retires scheduling only. */
    p=create(3,0,false,false);assert(api(due)(&p,600,true,&t)==E(SKIPPED) && p.running);
    assert(api(due)(&p,1600,false,&t)==E(ISSUE));complete(&p,&t);invariant(&p);
    p=create(4,0,true,false);assert(api(due)(&p,600,false,&t)==E(ISSUE));
    assert(api(due)(&p,1600,false,&other)==E(SKIPPED) && !p.running && !api(drained)(&p));
    complete(&p,&t);assert(api(drained)(&p));invariant(&p);
    /* Finite count includes skipped opportunities, and final issue outlives
     * running=false. Neither stop nor close invents native completion. */
    p=create(5,3,false,false);assert(api(due)(&p,10000,false,&t)==E(ISSUE));
    assert(p.scheduled==3 && p.skipped_late==2 && !p.running && !api(drained)(&p));
    api(stop)(&p);api(close)(&p);assert(p.closing && t.sequence==3 && p.active.sequence==3);
    assert(api(due)(&p,10001,false,&other)==E(HALTED));complete(&p,&t);
    assert(api(drained)(&p));invariant(&p);
    p=create(6,0,false,true);assert(api(due)(&p,600,false,&t)==E(ISSUE));
    assert(api(finish)(&p,&t,E(REJECTED)) && p.failed==0 && p.rejected==1 && p.faulted);
    assert(api(drained)(&p));invariant(&p);
    p=create(7,0,false,true);assert(api(due)(&p,600,false,&t)==E(ISSUE));
    assert(api(submitted)(&p,&t));assert(api(finish)(&p,&t,E(FAILED)));
    assert(p.completed==1 && p.failed==1 && p.faulted);invariant(&p);
    p=create(8,0,false,false);assert(api(due)(&p,600,false,&t)==E(ISSUE));
    assert(api(submitted)(&p,&t));assert(api(finish)(&p,&t,E(UNKNOWN)));
    assert(p.unknown==1 && p.completed==1 && p.running);invariant(&p);
    assert(api(due)(&p,1600,false,&t)==E(ISSUE));assert(api(finish)(&p,&t,E(ABORTED)));
    api(stop)(&p);assert(api(drained)(&p));invariant(&p);
    p=create(81,0,false,false);assert(api(due)(&p,600,false,&t)==E(ISSUE));
    assert(api(busy)(&p,&t) && !t.sequence && p.skipped_busy==1 && !p.issued);invariant(&p);
    assert(api(due)(&p,1600,false,&t)==E(ISSUE));assert(api(finish)(&p,&t,E(DROPPED)));
    assert(p.dropped==1 && p.failed==0 && p.running);invariant(&p);
    /* An SDK error with uncertain ownership is never no-submit proof. */
    p=create(9,0,false,false);assert(api(due)(&p,600,false,&t)==E(ISSUE));
    assert(api(uncertain)(&p,&t));api(close)(&p);
    assert(!api(finish)(&p,&t,E(REJECTED)) && !api(finish)(&p,&t,E(ABORTED)));
    assert(!api(finish)(&p,&t,E(SUCCESS)) && !api(submitted)(&p,&t));
    assert(p.uncertain && p.faulted && !api(drained)(&p));invariant(&p);
    /* Only explicit native teardown can settle an uncertain ticket. */
    other=t;other.generation++;
    assert(!api(terminated)(&p,&other,false) && !api(terminated)(&p,&p.active,false));
    assert(api(terminated)(&p,&t,false) && !p.uncertain && p.faulted && p.aborted==1 && !p.completed && !p.submitted);
    assert(api(drained)(&p) && !api(terminated)(&p,&t,false));invariant(&p);
    p=create(91,0,false,false);assert(api(due)(&p,600,false,&t)==E(ISSUE));
    assert(api(submitted)(&p,&t) && api(uncertain)(&p,&t));
    assert(!api(terminated)(&p,&t,false));
    assert(api(terminated)(&p,&t,true) && p.submitted==1 && !p.completed && p.aborted==1);invariant(&p);
    p=create(92,0,false,false);assert(api(due)(&p,600,false,&t)==E(ISSUE));
    assert(api(uncertain)(&p,&t) && api(terminated)(&p,&t,true));
    assert(p.submitted==1 && p.aborted==1 && !p.completed);invariant(&p);
    /* Backwards time faults without recycling an outstanding ticket. */
    t=(ticket_t){0};p=create(10,0,false,false);assert(api(due)(&p,600,false,&t)==E(ISSUE));
    saved=p;assert(api(due)(&p,599,false,&other)==E(INVALID) && !memcmp(&p,&saved,sizeof(p)));
    other=(ticket_t){0};
    assert(api(due)(&p,599,false,&other)==E(HALTED) && p.faulted && p.active.sequence==1);
    complete(&p,&t);invariant(&p);
    /* Max opportunity may be used once; neither identity nor counters wrap. */
    p=create(11,0,false,false);
    int64_t late=600+(int64_t)(UINT32_MAX-1U)*1000;
    assert(api(due)(&p,late,false,&t)==E(ISSUE));
    assert(t.sequence==UINT32_MAX && p.exhausted && p.faulted && !p.running);
    assert(p.skipped_late==UINT32_MAX-1U);complete(&p,&t);invariant(&p);
    assert(api(due)(&p,late+1000,false,&t)==E(HALTED) && !t.sequence);
    p=create(12,UINT32_MAX,false,false);assert(api(due)(&p,late,false,&t)==E(ISSUE));
    assert(!p.exhausted && !p.faulted);complete(&p,&t);invariant(&p);
    zero=(ledger_t){0};invalid=(options_t){.interval_us=1000};
    assert(api(init)(&zero,13,&invalid,INT64_MAX-999));
    assert(api(due)(&zero,INT64_MAX-999,false,&t)==E(HALTED) && zero.exhausted);
    /* Huge wake delay remains O(1), including large interval multiplication. */
    zero=(ledger_t){0};invalid.interval_us=UINT32_MAX;
    assert(api(init)(&zero,14,&invalid,0));
    assert(api(due)(&zero,INT64_MAX,false,&t)==E(HALTED) && zero.exhausted && !t.sequence);
    return 0;
}
