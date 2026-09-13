
static esp_phy_ant_gpio_config_t s_phy_ant_gpio_config;
static const uint32_t s_phy_ant_sel_sig_idx[4]={3,8,11,19};
typedef struct {int intr_type,mode,pull_down_en,pull_up_en;uint64_t pin_bit_mask;} gpio_config_t;
#define GPIO_INTR_DISABLE 0
#define GPIO_MODE_OUTPUT 2
#define GPIO_MODE_DISABLE 0
#define GPIO_PULLDOWN_DISABLE 0
#define GPIO_PULLUP_DISABLE 0
#define GPIO_IS_VALID_OUTPUT_GPIO(pin) ((pin)<49&&(pin)!=46)
static int writes,routes,fail_write,enables,fail_enable;
static uint64_t reserved,last_mask;
static bool esp_gpio_is_reserved(uint64_t mask){last_mask=mask;return !!(reserved&mask);}
static int gpio_config(const gpio_config_t*c){
 assert((c->mode==GPIO_MODE_OUTPUT||c->mode==GPIO_MODE_DISABLE)&&!c->intr_type&&!c->pull_down_en&&!c->pull_up_en);
 assert(c->pin_bit_mask&&!(c->pin_bit_mask&(c->pin_bit_mask-1)));++writes;return writes==fail_write?-77:0;
}
static int gpio_output_enable(unsigned pin){assert(reserved&(UINT64_C(1)<<pin));return ++enables==fail_enable?-78:0;}
static void esp_rom_gpio_connect_out_signal(uint32_t gpio,uint32_t signal,int invert,int output_invert){
 assert(gpio>=4&&gpio<=7&&signal==s_phy_ant_sel_sig_idx[routes]&&!invert&&!output_invert);++routes;
}
