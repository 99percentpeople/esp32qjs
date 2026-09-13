
#include "esp32_mquickjs_wifi_csi_target.h"
#include <assert.h>
#include <string.h>
/* Field-shaped input boundary, not a substitute for target ABI/build/RF checks. */
typedef struct {
    int8_t rssi,noise_floor;
    uint8_t channel,secondary_channel,second,ant,stbc,sig_mode,mcs,cwb,sgi;
    uint8_t fec_coding,aggregation,smoothing,not_sounding,ampdu_cnt,cur_bb_format;
    uint32_t timestamp,he_siga1;
    uint16_t he_siga2;
    bool rx_channel_estimate_info_vld;
} wifi_pkt_rx_ctrl_t;
typedef struct {wifi_pkt_rx_ctrl_t rx_ctrl;uint16_t len;bool first_word_invalid;} wifi_csi_info_t;
enum { RX_BB_FORMAT_11B, RX_BB_FORMAT_11G, RX_BB_FORMAT_HT, RX_BB_FORMAT_VHT,
    RX_BB_FORMAT_HE_SU, RX_BB_FORMAT_HE_MU, RX_BB_FORMAT_HE_ERSU,
    RX_BB_FORMAT_HE_TB, RX_BB_FORMAT_VHT_MU };
