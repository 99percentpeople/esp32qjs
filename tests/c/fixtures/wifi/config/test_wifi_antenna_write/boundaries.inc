
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_INVALID_RESPONSE 3
#define ESP_ERR_NO_MEM 4
#define CONFIG_ESP32_MQUICKJS_FEATURE_BLE 1
#define CONFIG_BT_ENABLED 1
#define CONFIG_IEEE802154_ENABLED 1
#define ESP_BT_CONTROLLER_STATUS_IDLE 0
#define ESP_IEEE802154_RADIO_DISABLE 0
#define GPIO_IS_VALID_OUTPUT_GPIO(pin) ((pin)<32)
#define PIN_FUNC_GPIO 1
#define SIG_GPIO_OUT_IDX 256
#define GPIO_FUNC0_OUT_INV_SEL 512
static antenna_gpio_owner_t *s_antenna_gpio;
static _Atomic int s_antenna_fault;
static bool ble_closed=true,awake,fail_alloc;
static int controller,ieee154,lock_depth,writes,reads,pin_writes,fail_pin,fail_restore,live;
static int fail_set,fail_get,fail_rollback;
static uint64_t reserved,race_reserved;
static antenna_route_t routes[32];
static bool pad_available[32];
static esp_phy_ant_config_t stored_config;
static esp_phy_ant_gpio_config_t stored_gpio;
static struct {struct {unsigned int_ena,int_type,wakeup_enable;} pin[32];struct{uint32_t val;}func_out_sel_cfg[32];} GPIO;
typedef struct{unsigned fun_sel,ie,oe,sig_out,oe_inv;}gpio_io_config_t;
static bool esp32_mquickjs_ble_phy_idle(void){assert(lock_depth==1);return ble_closed;}
static int esp_bt_controller_get_status(void){assert(lock_depth==1);return controller;}
static int esp_ieee802154_get_state(void){assert(lock_depth==1);return ieee154;}
static int esp32qjs_phy_antenna_run_idle(int(*fn)(void*),void *arg){
 assert(!lock_depth);lock_depth=1;int e=awake?ESP_ERR_INVALID_STATE:fn(arg);lock_depth=0;return e;
}
static int esp_phy_get_ant(esp_phy_ant_config_t *out){assert(lock_depth);*out=stored_config;return ++reads==fail_get?-61:0;}
static int esp_phy_set_ant(esp_phy_ant_config_t *value){
 assert(lock_depth);stored_config=*value;writes++;return writes==fail_set?-62:writes==fail_rollback?-63:0;
}
static int esp_phy_get_ant_gpio(esp_phy_ant_gpio_config_t *out){assert(lock_depth);*out=stored_gpio;return 0;}
static void esp32qjs_phy_ant_gpio_restore_config(const esp_phy_ant_gpio_config_t *value){assert(lock_depth);stored_gpio=*value;}
static unsigned esp32qjs_phy_ant_gpio_signal(unsigned i){static const unsigned s[]={3,8,11,19};return s[i];}
static int gpio_get_io_config(unsigned pin,gpio_io_config_t *io){
 *io=(gpio_io_config_t){.fun_sel=routes[pin].mux,.ie=routes[pin].pin,.oe=routes[pin].enabled,.sig_out=routes[pin].output};return 0;
}
static antenna_route_t antenna_route_read(unsigned pin){return routes[pin];}
static bool antenna_pad_available(unsigned pin){return pad_available[pin];}
static int antenna_route_restore(unsigned pin,const antenna_route_t *value){if((int)pin==fail_restore)return -71;routes[pin]=*value;return 0;}
static bool esp_gpio_is_reserved(uint64_t mask){return !!(reserved&mask);}
static uint64_t esp_gpio_reserve(uint64_t mask){reserved|=race_reserved;uint64_t old=reserved;reserved|=mask;return old;}
static uint64_t esp_gpio_revoke(uint64_t mask){uint64_t old=reserved;reserved&=~mask;return old;}
static int esp32qjs_phy_set_ant_gpio_owned(esp_phy_ant_gpio_config_t *value,uint64_t mask){
 assert(lock_depth&&!(mask&~reserved));writes++;
 for(unsigned i=0;i<4;i++)if(value->gpio_cfg[i].gpio_select){
  unsigned pin=value->gpio_cfg[i].gpio_num;
  routes[pin]=(antenna_route_t){.mux=PIN_FUNC_GPIO,.output=esp32qjs_phy_ant_gpio_signal(i),.enabled=true};
  if(++pin_writes==fail_pin)return -72;
 }
 stored_gpio=*value;return 0;
}
static void *allocate(size_t n){if(fail_alloc)return NULL;void *p=calloc(1,n);assert(p);live++;return p;}
#define esp32_mquickjs_memory_wireless_calloc(owner,n,size,policy,role) allocate((n)*(size))
static void esp32_mquickjs_memory_payload_free(void *p){if(p){assert(live);live--;free(p);}}
