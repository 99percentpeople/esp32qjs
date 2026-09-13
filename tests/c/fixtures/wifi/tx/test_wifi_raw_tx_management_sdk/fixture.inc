/* Execute the real SDK entry and validator with injected OS/HMAC boundaries. */
typedef unsigned char u8;
typedef unsigned int u32;
extern int ieee80211_raw_frame_sanity_check(int, const void *, int, int);
extern int esp_wifi_80211_tx(int, const void *, int, int);
static int locks, unlocks;
static int lock(void *unused) { (void)unused; locks++; return 1; }
static int unlock(void *unused) { (void)unused; unlocks++; return 1; }
static void *osi[23];
void **g_osi_funcs_p=osi;
void *g_wifi_global_lock;
u32 g_ic[512];
static u32 station[128], ap[128], peer[32];
static const u8 mac[6]={2,3,4,5,6,7};
static int peer_found;
void *memset(void *p,int value,unsigned n) {for(unsigned i=0;i<n;i++) ((u8 *)p)[i]=(u8)value;return p;}
void wifi_log(int a,int b,int c,const char *s,...) {(void)a;(void)b;(void)c;(void)s;}
int memcmp(const void *a,const void *b,unsigned n) {
    const u8 *x=a,*y=b;
    for(unsigned i=0;i<n;i++) if(x[i]!=y[i]) return x[i]-y[i];
    return 0;
}
void wifi_get_macaddr(int iface,void *out) {
    (void)iface;for(unsigned i=0;i<6;i++) ((u8 *)out)[i]=mac[i];
}
void *cnx_node_search(const void *address) {(void)address;return peer_found?peer:0;}
static u32 eb[64], tx_info[64];
static u8 copied[1500], channel=1;
static int copied_length, posted, allocation_failure;
void *ic_ebuf_alloc(const void *bytes,int kind,int length) {
    (void)kind;if(allocation_failure) return 0;
    memset(eb,0,sizeof(eb));memset(tx_info,0,sizeof(tx_info));
    eb[EB_TXINFO_WORD]=(u32)tx_info;copied_length=length;
    for(int i=0;i<length;i++) copied[i]=((const u8 *)bytes)[i];
    return eb;
}
void *chm_get_current_channel(void) {return &channel;}
int chm_get_band_from_chan(int ch) {(void)ch;return 1;}
int ic_get_default_sched(void) {return 0;}
int ic_get_80211_tx_rate(int iface) {(void)iface;return 1;}
void ic_get_80211_tx_rate_config(int iface,void *config) {(void)iface;memset(config,0,12);}
void fixture_post_hmac_tx(void *buffer) {if(buffer==eb) posted++;}
#define CHECK(x,n) do {if(!(x)) return n;}while(0)
int main(void) {
    osi[21]=(void *)lock;osi[22]=(void *)unlock;
    g_ic[4]=(u32)station;g_ic[5]=(u32)ap;
    u8 bytes[1501]={0};
    for(unsigned i=0;i<6;i++) {bytes[10+i]=mac[i];bytes[4+i]=((u8 *)peer)[4+i]=(u8)(i+20);}
    for(int iface=0;iface<2;iface++) {
        for(int subtype=0;subtype<16;subtype++) {
            bytes[0]=(u8)(subtype<<4);
            int allowed=EXPECT_EXTENDED || subtype==4 || subtype==5 || subtype==8 || subtype==13;
            CHECK((ieee80211_raw_frame_sanity_check(iface,bytes,24,1)==0)==allowed,10+subtype);
            int before=posted;
            CHECK((esp_wifi_80211_tx(iface,bytes,64,1)==0)==allowed,60);
            CHECK(posted==before+allowed,61);
            if(allowed) {
                CHECK(copied_length==64 && !memcmp(copied,bytes,64),62);
                CHECK(tx_info[0]&1,63); /* Retains the requested driver sequence flag. */
            }
        }
        bytes[0]=0x80;
        CHECK(ieee80211_raw_frame_sanity_check(iface,0,24,1)!=0,30);
        CHECK(ieee80211_raw_frame_sanity_check(iface,bytes,23,1)!=0,31);
        CHECK(ieee80211_raw_frame_sanity_check(iface,bytes,1501,1)!=0,32);
        CHECK(ieee80211_raw_frame_sanity_check(iface,bytes,1500,1)==0,33);
        allocation_failure=1;
        int before=posted;
        CHECK(esp_wifi_80211_tx(iface,bytes,64,1)==0x101 && posted==before,64);
        allocation_failure=0;
        for(int subtype=0;subtype<16;subtype++) {
            bytes[0]=(u8)(subtype<<4);bytes[1]=0x40;
            CHECK(ieee80211_raw_frame_sanity_check(iface,bytes,24,1)!=0,34);
            bytes[1]=0;bytes[0]=(u8)((subtype<<4)|4);
            CHECK(ieee80211_raw_frame_sanity_check(iface,bytes,24,1)!=0,35);
            bytes[0]=(u8)((subtype<<4)|12);
            CHECK(ieee80211_raw_frame_sanity_check(iface,bytes,24,1)!=0,36);
            bytes[0]=(u8)((subtype<<4)|8);
            CHECK((ieee80211_raw_frame_sanity_check(iface,bytes,24,1)==0)==(subtype<8),37);
        }
        /* SDK's own connected-path safeguards survive the type expansion. */
        station[57]=(u32)peer;peer_found=1;
        bytes[0]=0x80;
        CHECK(ieee80211_raw_frame_sanity_check(iface,bytes,24,0)!=0,40);
        CHECK(ieee80211_raw_frame_sanity_check(iface,bytes,24,1)==0,41);
        for(int flag=4;flag<=32;flag<<=1) {
            bytes[1]=(u8)flag;
            CHECK(ieee80211_raw_frame_sanity_check(iface,bytes,24,1)!=0,42);
        }
        bytes[0]=8;bytes[1]=(u8)(iface==0?1:2);
        CHECK(ieee80211_raw_frame_sanity_check(iface,bytes,24,1)==0,43);
        bytes[1]=(u8)(iface==0?2:1);
        CHECK(ieee80211_raw_frame_sanity_check(iface,bytes,24,1)!=0,44);
        bytes[1]=0;station[57]=0;peer_found=0;
    }
    CHECK(ieee80211_raw_frame_sanity_check(2,bytes,24,1)!=0,50);
    g_ic[4]=0;
    CHECK(ieee80211_raw_frame_sanity_check(0,bytes,24,1)!=0,51);
    CHECK(locks>0 && locks==unlocks,52);
    return 0;
}
__attribute__((naked,noreturn)) void _start(void) {__asm__ volatile("call main\nli a7,93\necall\n");}
