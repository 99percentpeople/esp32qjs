
#include <assert.h>
#include <stdbool.h>
#define NAN_DE_SUBSCRIBE 1
struct os_reltime {long sec,usec;};
struct nan_de {int listen_freq,ext_listen_freq,tx_wait_end_freq;};
struct nan_de_service {int type,*freq_list,multi_chan_idx,freq;struct os_reltime next_publish_chan;};
static bool os_reltime_initialized(const struct os_reltime*t){return t->sec||t->usec;}
static bool os_reltime_before(const struct os_reltime*a,const struct os_reltime*b){return a->sec<b->sec||(a->sec==b->sec&&a->usec<b->usec);}
static void os_reltime_add_ms(struct os_reltime*t,unsigned ms){t->sec+=ms/1000;t->usec+=(ms%1000)*1000;}
