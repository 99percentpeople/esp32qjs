
#include "esp_wifi.h"
unsigned char g_ic[24];
unsigned char nvs[1500], chm[700], node[400];
unsigned char *g_wifi_nvs=nvs, *g_chm=chm;
static int band=0, initialized=1, reads, writes, getter_error;
static unsigned protocols=7;
static int protocols_5g=-1, protocols_ap=-1, written_interface=-1;
static wifi_tx_rate_config_t written;
int fixture_initialized(void) { return initialized; }
int chm_get_current_band(void) { return band; }
int fixture_get_protocol(wifi_interface_t interface, unsigned char *output)
{ reads++; *output=interface==WIFI_IF_AP && protocols_ap>=0?(unsigned)protocols_ap:protocols; return getter_error; }
#if CONFIG_SOC_WIFI_SUPPORT_5G
int fixture_get_protocols(wifi_interface_t interface, wifi_protocols_t *output)
{ reads++; output->ghz_2g=interface==WIFI_IF_AP && protocols_ap>=0?(unsigned)protocols_ap:protocols; output->ghz_5g=protocols_5g<0?protocols:(unsigned)protocols_5g; return getter_error; }
#endif
void wifi_log(int a, int b, int c, const char *s, ...) { (void)a; (void)b; (void)c; (void)s; }
void ic_set_80211_tx_rate_config(unsigned char interface, const wifi_tx_rate_config_t *value)
{ written_interface=interface; writes++; written=*value; }
void *memcpy(void *out,const void *in,unsigned n)
{ unsigned char *d=out; const unsigned char *s=in; while(n--) *d++=*s++; return out; }
int esp_wifi_config_80211_tx(wifi_interface_t, wifi_tx_rate_config_t *);
#define CHECK(expr, code) do { if (!(expr)) return code; } while(0)
int main(void)
{
    wifi_tx_rate_config_t config={.phymode=WIFI_PHY_MODE_11G,.rate=WIFI_PHY_RATE_6M};
    /* STOP has destroyed both interface objects. The SDK API promises that
     * init-before-start configuration works, without manufacturing an object. */
    for (int interface=0; interface<2; interface++) {
        CHECK(esp_wifi_config_80211_tx(interface,&config)==ESP_OK,10+interface);
        CHECK(writes==interface+1 && written.rate==WIFI_PHY_RATE_6M,12);
    }
    CHECK(reads==2,13);
    band=1;
    initialized=0;
    CHECK(esp_wifi_config_80211_tx(0,&config)==ESP_ERR_WIFI_NOT_INIT && writes==2,14);
    initialized=1;
    CHECK(esp_wifi_config_80211_tx(2,&config)!=ESP_OK && reads==2,15);
    CHECK(esp_wifi_config_80211_tx(0,0)!=ESP_OK && reads==2,16);
    /* Getter failure is a real pre-write error, not a guessed PHY/default. */
    getter_error=ESP_ERR_NO_MEM;
    CHECK(esp_wifi_config_80211_tx(0,&config)==ESP_ERR_NO_MEM && writes==2,17);
    getter_error=0;
    protocols=1; config.phymode=WIFI_PHY_MODE_HT20; config.rate=WIFI_PHY_RATE_MCS0_LGI;
    CHECK(esp_wifi_config_80211_tx(0,&config)!=ESP_OK && writes==2,18);
    protocols=7;
    CHECK(esp_wifi_config_80211_tx(0,&config)==ESP_OK && writes==3,19);
    config.phymode=WIFI_PHY_MODE_HT40;
    CHECK(esp_wifi_config_80211_tx(0,&config)!=ESP_OK && writes==3,20);
    nvs[157]=2; chm[SECONDARY_OFFSET]=1;
    CHECK(esp_wifi_config_80211_tx(0,&config)==ESP_OK && writes==4,21);
    /* The interface has not enabled LR. */
    config.phymode=WIFI_PHY_MODE_LR; config.rate=WIFI_PHY_RATE_LORA_250K; protocols=7;
    CHECK(esp_wifi_config_80211_tx(0,&config)!=ESP_OK && writes==4,22);
    /* A live interface keeps the original SDK PHY source; no getter or saved
     * protocol replaces its runtime value. */
    *(void **)(g_ic+16)=node; node[PHY_OFFSET]=1; protocols=7;
    config.phymode=WIFI_PHY_MODE_HT20; config.rate=WIFI_PHY_RATE_MCS0_LGI;
    int before=reads;
    CHECK(esp_wifi_config_80211_tx(0,&config)!=ESP_OK && reads==before && writes==4,24);
    *(void **)(g_ic+16)=0;
#if CONFIG_SOC_WIFI_SUPPORT_5G
    protocols=71; config.phymode=WIFI_PHY_MODE_HE20; config.ersu=true; config.dcm=false;
    config.rate=WIFI_PHY_RATE_MCS3_LGI;
    CHECK(esp_wifi_config_80211_tx(0,&config)!=ESP_OK && writes==4,25);
    config.rate=WIFI_PHY_RATE_MCS0_LGI;
    CHECK(esp_wifi_config_80211_tx(0,&config)==ESP_OK && writes==5,26);
    config.ersu=false; config.dcm=true; config.rate=WIFI_PHY_RATE_MCS2_LGI;
    CHECK(esp_wifi_config_80211_tx(0,&config)!=ESP_OK && writes==5,27);
    config.dcm=false; band=2; protocols=16;
    config.phymode=WIFI_PHY_MODE_11A; config.rate=WIFI_PHY_RATE_6M;
    CHECK(esp_wifi_config_80211_tx(0,&config)==ESP_OK && writes==6,28);
    config.phymode=WIFI_PHY_MODE_11B; config.rate=WIFI_PHY_RATE_1M_L;
    CHECK(esp_wifi_config_80211_tx(0,&config)!=ESP_OK && writes==6,29);
    protocols=116; config.phymode=WIFI_PHY_MODE_HE20; config.rate=WIFI_PHY_RATE_MCS0_LGI;
    CHECK(esp_wifi_config_80211_tx(0,&config)==ESP_OK && writes==7,30);
    band=0; protocols=71; protocols_5g=116;
    CHECK(esp_wifi_config_80211_tx(0,&config)==ESP_OK && writes==8,31);
    protocols_5g=20;
    CHECK(esp_wifi_config_80211_tx(0,&config)==ESP_ERR_INVALID_STATE && writes==8,32);
    config.phymode=WIFI_PHY_MODE_11A; config.rate=WIFI_PHY_RATE_6M;
    CHECK(esp_wifi_config_80211_tx(0,&config)==ESP_OK && writes==9,33);
    protocols_5g=0;
    CHECK(esp_wifi_config_80211_tx(0,&config)==ESP_ERR_INVALID_STATE && writes==9,34);
#endif
    /* Actual SDK LR validation: only the selected interface's protocol flag
     * enables LR. Neither the other interface nor an unrelated NVS byte can
     * authorize it. Exercise fresh AUTO, STOP and existing interface objects. */
    for (int live=0; live<2; live++) {
        *(void **)(g_ic+16)=live?node:0;
        *(void **)(g_ic+20)=live?node:0;
        node[PHY_OFFSET]=3;
        for (band=0; band<=1; band++) {
            for (int interface=0; interface<2; interface++) {
                for (int mixed=0; mixed<2; mixed++) {
                    protocols=interface==0?(mixed?15:8):7;
                    protocols_ap=interface==1?(mixed?15:8):7;
                    nvs[1]=0; config.ersu=false; config.dcm=false;
                    config.phymode=WIFI_PHY_MODE_LR;
                    for (int rate=WIFI_PHY_RATE_LORA_250K; rate<=WIFI_PHY_RATE_LORA_500K; rate++) {
                        config.rate=rate; int count=writes;
                        CHECK(esp_wifi_config_80211_tx(interface,&config)==ESP_OK,40);
                        CHECK(writes==count+1 && written_interface==interface &&
                            written.phymode==WIFI_PHY_MODE_LR && written.rate==rate &&
                            !written.ersu && !written.dcm,41);
                        nvs[1]=1; count=writes;
                        CHECK(esp_wifi_config_80211_tx(1-interface,&config)!=ESP_OK && writes==count,42);
                        nvs[1]=0;
                    }
                    int count=writes;
                    for (int rate=-1; rate<=43; rate++) {
                        if (rate==WIFI_PHY_RATE_LORA_250K || rate==WIFI_PHY_RATE_LORA_500K) continue;
                        config.rate=rate;
                        CHECK(esp_wifi_config_80211_tx(interface,&config)!=ESP_OK && writes==count,43);
                    }
                    config.rate=WIFI_PHY_RATE_LORA_250K;
                    config.ersu=true;
                    CHECK(esp_wifi_config_80211_tx(interface,&config)!=ESP_OK && writes==count,44);
                    config.ersu=false; config.dcm=true;
                    CHECK(esp_wifi_config_80211_tx(interface,&config)!=ESP_OK && writes==count,45);
                    config.dcm=false; getter_error=ESP_ERR_NO_MEM;
                    CHECK(esp_wifi_config_80211_tx(interface,&config)==ESP_ERR_NO_MEM && writes==count,46);
                    getter_error=0;
                }
            }
        }
    }
#if CONFIG_SOC_WIFI_SUPPORT_5G
    band=2; protocols=15; protocols_ap=15; protocols_5g=116;
    int count=writes;
    CHECK(esp_wifi_config_80211_tx(0,&config)!=ESP_OK && writes==count,47);
    CHECK(esp_wifi_config_80211_tx(1,&config)!=ESP_OK && writes==count,48);
#endif
    return 0;
}
__attribute__((naked,noreturn)) void _start(void)
{ __asm__ volatile("call main\nli a7,93\necall\n"); }
