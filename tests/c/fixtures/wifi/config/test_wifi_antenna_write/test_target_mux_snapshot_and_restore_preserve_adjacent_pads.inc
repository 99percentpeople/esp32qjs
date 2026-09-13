
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE 1
#define ESP_ERR_INVALID_RESPONSE 2
#define USB_INT_PHY0_DP_GPIO_NUM 13
#define USB_SERIAL_JTAG_PAD_PULL_OVERRIDE 1
#define USB_SERIAL_JTAG_DP_PULLUP 2
static uint32_t usb_register;
#define USB_SERIAL_JTAG_CONF0_REG (&usb_register)
#define REG_READ(address) (*(volatile uint32_t *)(address))
#define REG_WRITE(address, value) (*(volatile uint32_t *)(address) = (value))
static struct {struct {uint32_t val;} pin[29], func_out_sel_cfg[29];} GPIO;
static struct {struct {uint32_t val;} gpio[29];} IO_MUX;
#if CONFIG_IDF_TARGET_ESP32C5
/* The SDK header declares this table, but C5 does not define it. */
extern const uintptr_t GPIO_PIN_MUX_REG[29];
#else
static uintptr_t GPIO_PIN_MUX_REG[29];
#endif
static bool enabled[29], available = true;
typedef struct {bool oe;} gpio_io_config_t;
static int gpio_get_io_config(unsigned pin, gpio_io_config_t *io){io->oe=enabled[pin];return 0;}
static bool antenna_pad_available(unsigned pin){return available;}
#define gpio_ll_output_disable(hw, pin) (enabled[pin] = false)
#define gpio_ll_output_enable(hw, pin) (enabled[pin] = true)
