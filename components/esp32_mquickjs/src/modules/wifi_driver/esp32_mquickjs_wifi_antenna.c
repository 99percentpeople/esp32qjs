#include "esp32_mquickjs_wifi_antenna.h"
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
#include "esp32_mquickjs_ble.h"
#include "esp32_mquickjs_memory.h"
#include "driver/gpio.h"
#include "esp_private/esp_gpio_reserve.h"
#include "hal/gpio_ll.h"
#include "soc/gpio_periph.h"
#include "soc/gpio_reg.h"
#include "soc/gpio_sig_map.h"
#include "soc/usb_serial_jtag_reg.h"
#if CONFIG_IDF_TARGET_ESP32S3
#include "hal/rtc_io_periph.h"
#include "soc/rtc_cntl_reg.h"
#elif CONFIG_IDF_TARGET_ESP32C5
#include "soc/io_mux_struct.h"
#include "soc/lp_aon_struct.h"
#endif
#if CONFIG_BT_ENABLED
#include "esp_bt.h"
#endif
#if CONFIG_IEEE802154_ENABLED
#include "esp_ieee802154.h"
#endif
#include <stdatomic.h>
#include <string.h>

/* Defined only in the hash-pinned SDK build copies. */
extern esp_err_t esp32qjs_phy_antenna_run_idle(esp_err_t (*apply)(void *), void *context);
extern esp_err_t esp32qjs_phy_set_ant_gpio_owned(esp_phy_ant_gpio_config_t *config, uint64_t owned);
extern void esp32qjs_phy_ant_gpio_restore_config(const esp_phy_ant_gpio_config_t *config);
extern unsigned esp32qjs_phy_ant_gpio_signal(unsigned index);

typedef struct {
    uint32_t mux, pin, output, rtc, usb;
    bool enabled;
} antenna_route_t;
typedef struct {
    uint8_t pin;
    bool selected;
    antenna_route_t original, applied;
} antenna_pin_t;
typedef struct {
    esp_phy_ant_gpio_config_t config;
    antenna_pin_t pins[4];
} antenna_gpio_owner_t;
static antenna_gpio_owner_t *s_antenna_gpio;
static _Atomic int s_antenna_fault;

esp_err_t esp32_mquickjs_wifi_antenna_fault(void)
{
    return atomic_load_explicit(&s_antenna_fault, memory_order_acquire);
}

bool esp32_mquickjs_wifi_antenna_valid(const esp_phy_ant_config_t *config)
{
    return config != NULL &&
        (config->rx_ant_mode == ESP_PHY_ANT_MODE_ANT0 || config->rx_ant_mode == ESP_PHY_ANT_MODE_ANT1 ||
         config->rx_ant_mode == ESP_PHY_ANT_MODE_AUTO) &&
        (config->tx_ant_mode == ESP_PHY_ANT_MODE_ANT0 || config->tx_ant_mode == ESP_PHY_ANT_MODE_ANT1 ||
         config->tx_ant_mode == ESP_PHY_ANT_MODE_AUTO) &&
        (config->rx_ant_default == ESP_PHY_ANT_ANT0 || config->rx_ant_default == ESP_PHY_ANT_ANT1) &&
        (config->tx_ant_mode != ESP_PHY_ANT_MODE_AUTO || config->rx_ant_mode == ESP_PHY_ANT_MODE_AUTO);
}

static bool antenna_config_equal(const esp_phy_ant_config_t *a, const esp_phy_ant_config_t *b)
{
    return a->rx_ant_mode == b->rx_ant_mode && a->tx_ant_mode == b->tx_ant_mode &&
        a->rx_ant_default == b->rx_ant_default && a->enabled_ant0 == b->enabled_ant0 &&
        a->enabled_ant1 == b->enabled_ant1;
}

static bool antenna_gpio_equal(const esp_phy_ant_gpio_config_t *a, const esp_phy_ant_gpio_config_t *b)
{
    for (unsigned i = 0; i < 4; ++i)
        if (a->gpio_cfg[i].gpio_select != b->gpio_cfg[i].gpio_select ||
            a->gpio_cfg[i].gpio_num != b->gpio_cfg[i].gpio_num) return false;
    return true;
}

static esp_err_t antenna_gpio_mask(const esp_phy_ant_gpio_config_t *config, uint64_t *mask)
{
    *mask = 0;
    for (unsigned i = 0; i < 4; ++i) {
        if (!config->gpio_cfg[i].gpio_select) continue;
        int pin = config->gpio_cfg[i].gpio_num;
        if (pin >= 64 || !GPIO_IS_VALID_OUTPUT_GPIO(pin)) return ESP_ERR_INVALID_ARG;
        uint64_t bit = UINT64_C(1) << pin;
        if (*mask & bit) return ESP_ERR_INVALID_ARG;
        *mask |= bit;
    }
    return ESP_OK;
}

/* Read only individually addressed pad registers. Active RTC/LP mux or hold
 * cannot be appropriated by a digital antenna configuration. */
static bool antenna_pad_available(unsigned pin)
{
    if ((pin == USB_INT_PHY0_DM_GPIO_NUM || pin == USB_INT_PHY0_DP_GPIO_NUM) &&
        (REG_READ(USB_SERIAL_JTAG_CONF0_REG) & USB_SERIAL_JTAG_USB_PAD_ENABLE)) return false;
#if CONFIG_IDF_TARGET_ESP32S3
    int rtc = rtc_io_num_map[pin];
    if (rtc >= 0) {
        const rtc_io_desc_t *desc = &rtc_io_desc[rtc];
        return !(REG_READ(desc->reg) & (desc->mux | desc->hold)) &&
            !(REG_READ(RTC_CNTL_PAD_HOLD_REG) & desc->hold_force);
    }
#elif CONFIG_IDF_TARGET_ESP32C5
    if (pin < SOC_RTCIO_PIN_COUNT && (LP_AON.gpio_mux.gpio_mux_sel & (1U << pin))) return false;
#endif
    return !gpio_ll_is_digital_io_hold(&GPIO, pin);
}

static antenna_route_t antenna_route_read(unsigned pin)
{
    gpio_io_config_t io = {0};
    (void)gpio_get_io_config(pin, &io); /* Caller already validated the pin. */
    antenna_route_t route = {
#if CONFIG_IDF_TARGET_ESP32C5
        .mux = IO_MUX.gpio[pin].val,
#else
        .mux = REG_READ(GPIO_PIN_MUX_REG[pin]),
#endif
        .pin = GPIO.pin[pin].val,
        .output = GPIO.func_out_sel_cfg[pin].val, .enabled = io.oe,
    };
    if (pin == USB_INT_PHY0_DP_GPIO_NUM)
        route.usb = REG_READ(USB_SERIAL_JTAG_CONF0_REG) &
            (USB_SERIAL_JTAG_PAD_PULL_OVERRIDE | USB_SERIAL_JTAG_DP_PULLUP);
#if CONFIG_IDF_TARGET_ESP32S3
    int rtc = rtc_io_num_map[pin];
    if (rtc >= 0) route.rtc = REG_READ(rtc_io_desc[rtc].reg);
#endif
    return route;
}

static bool antenna_route_equal(const antenna_route_t *a, const antenna_route_t *b)
{
    return a->mux == b->mux && a->pin == b->pin && a->output == b->output &&
        a->rtc == b->rtc && a->usb == b->usb && a->enabled == b->enabled;
}

static esp_err_t antenna_route_restore(unsigned pin, const antenna_route_t *route)
{
    if (!antenna_pad_available(pin)) return ESP_ERR_INVALID_STATE;
    gpio_ll_output_disable(&GPIO, pin);
    GPIO.func_out_sel_cfg[pin].val = route->output;
    GPIO.pin[pin].val = route->pin;
#if CONFIG_IDF_TARGET_ESP32C5
    IO_MUX.gpio[pin].val = route->mux;
#else
    REG_WRITE(GPIO_PIN_MUX_REG[pin], route->mux);
#endif
    if (pin == USB_INT_PHY0_DP_GPIO_NUM) {
        uint32_t usb = REG_READ(USB_SERIAL_JTAG_CONF0_REG);
        usb &= ~(USB_SERIAL_JTAG_PAD_PULL_OVERRIDE | USB_SERIAL_JTAG_DP_PULLUP);
        REG_WRITE(USB_SERIAL_JTAG_CONF0_REG, usb | route->usb);
    }
#if CONFIG_IDF_TARGET_ESP32S3
    int rtc = rtc_io_num_map[pin];
    if (rtc >= 0) REG_WRITE(rtc_io_desc[rtc].reg, route->rtc);
#endif
    if (route->enabled) gpio_ll_output_enable(&GPIO, pin);
    antenna_route_t actual = antenna_route_read(pin);
    return antenna_route_equal(route, &actual) ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

static int antenna_owned_pin(unsigned pin)
{
    if (s_antenna_gpio != NULL)
        for (unsigned i = 0; i < 4; ++i)
            if (s_antenna_gpio->pins[i].selected && s_antenna_gpio->pins[i].pin == pin) return (int)i;
    return -1;
}

static esp_err_t antenna_write_gpio(const esp_phy_ant_gpio_config_t *requested,
    esp32_mquickjs_wifi_radio_config_result_t *result)
{
    esp_phy_ant_gpio_config_t previous, observed;
    uint64_t wanted, owned = 0;
    result->stage = "antenna-gpio-validate";
    esp_err_t error = antenna_gpio_mask(requested, &wanted);
    if (error != ESP_OK) return error;
    result->stage = "antenna-gpio-snapshot";
    error = esp_phy_get_ant_gpio(&previous);
    if (error != ESP_OK) return error;
    error = antenna_gpio_mask(&previous, &owned);
    if (error != ESP_OK) return ESP_ERR_INVALID_RESPONSE;
    /* Never adopt a foreign SDK route from its pin number alone. */
    if ((s_antenna_gpio == NULL && owned != 0) ||
        (s_antenna_gpio != NULL && !antenna_gpio_equal(&s_antenna_gpio->config, &previous)))
        return ESP_ERR_INVALID_STATE;
    antenna_gpio_owner_t next = {.config = *requested};
    result->stage = "antenna-gpio-ownership";
    for (unsigned i = 0; s_antenna_gpio != NULL && i < 4; ++i) {
        const antenna_pin_t *entry = &s_antenna_gpio->pins[i];
        if (!entry->selected) continue;
        antenna_route_t actual = antenna_route_read(entry->pin);
        if (!esp_gpio_is_reserved(UINT64_C(1) << entry->pin) || !antenna_pad_available(entry->pin) ||
            !antenna_route_equal(&entry->applied, &actual)) return ESP_ERR_INVALID_STATE;
    }
    for (unsigned i = 0; i < 4; ++i) {
        if (!requested->gpio_cfg[i].gpio_select) continue;
        unsigned pin = requested->gpio_cfg[i].gpio_num;
        next.pins[i].pin = pin;
        next.pins[i].selected = true;
        int existing = antenna_owned_pin(pin);
        if (existing >= 0) next.pins[i].original = s_antenna_gpio->pins[existing].original;
        else {
            gpio_io_config_t io = {0};
            error = gpio_get_io_config(pin, &io);
            if (error != ESP_OK) return error;
            /* Disabled GPIO mux is the explicit released-pin state. Peripheral
             * muxes, input consumers, interrupts, wake and hold are not idle. */
            if (esp_gpio_is_reserved(UINT64_C(1) << pin) || !antenna_pad_available(pin) ||
                io.fun_sel != PIN_FUNC_GPIO || io.ie || io.oe || io.sig_out != SIG_GPIO_OUT_IDX ||
                GPIO.pin[pin].int_ena || GPIO.pin[pin].int_type || GPIO.pin[pin].wakeup_enable)
                return ESP_ERR_INVALID_STATE;
            next.pins[i].original = antenna_route_read(pin);
        }
    }
    antenna_gpio_owner_t *allocation = NULL;
    if (wanted && s_antenna_gpio == NULL) {
        allocation = esp32_mquickjs_memory_wireless_calloc("wifi.antenna.gpio", 1, sizeof(*allocation),
            ESP32_MQUICKJS_MEMORY_DEFAULT, ESP32_MQUICKJS_MEMORY_BUDGET_CONTROL);
        if (allocation == NULL) return ESP_ERR_NO_MEM;
    }
    uint64_t added = wanted & ~owned;
    uint64_t conflict = esp_gpio_reserve(added) & added;
    if (conflict) {
        esp_gpio_revoke(added & ~conflict);
        esp32_mquickjs_memory_payload_free(allocation);
        return ESP_ERR_INVALID_STATE;
    }
    result->stage = "antenna-gpio-write";
    result->mutation_attempted = true;
    esp_phy_ant_gpio_config_t work = *requested;
    error = esp32qjs_phy_set_ant_gpio_owned(&work, wanted);
    if (error == ESP_OK) {
        result->stage = "antenna-gpio-readback";
        error = esp_phy_get_ant_gpio(&observed);
        if (error == ESP_OK && !antenna_gpio_equal(requested, &observed)) error = ESP_ERR_INVALID_RESPONSE;
        for (unsigned i = 0; error == ESP_OK && i < 4; ++i) {
            if (!next.pins[i].selected) continue;
            gpio_io_config_t io = {0};
            error = gpio_get_io_config(next.pins[i].pin, &io);
            if (error == ESP_OK && (io.fun_sel != PIN_FUNC_GPIO || !io.oe || io.ie ||
                io.sig_out != esp32qjs_phy_ant_gpio_signal(i) || io.oe_inv ||
                (GPIO.func_out_sel_cfg[next.pins[i].pin].val & GPIO_FUNC0_OUT_INV_SEL))) error = ESP_ERR_INVALID_RESPONSE;
        }
    }
    if (error == ESP_OK) {
        result->stage = "antenna-gpio-release";
        for (unsigned i = 0; s_antenna_gpio != NULL && i < 4; ++i) {
            const antenna_pin_t *entry = &s_antenna_gpio->pins[i];
            if (!entry->selected || (wanted & (UINT64_C(1) << entry->pin))) continue;
            error = antenna_route_restore(entry->pin, &entry->original);
            if (error != ESP_OK) break;
        }
    }
    if (error == ESP_OK) {
        for (unsigned i = 0; i < 4; ++i)
            if (next.pins[i].selected) next.pins[i].applied = antenna_route_read(next.pins[i].pin);
        esp_gpio_revoke(owned & ~wanted);
        if (wanted) {
            if (s_antenna_gpio == NULL) s_antenna_gpio = allocation;
            *s_antenna_gpio = next;
        } else {
            esp32_mquickjs_memory_payload_free(s_antenna_gpio);
            s_antenna_gpio = NULL;
        }
        result->stage = "complete";
        return ESP_OK;
    }
    result->rollback_attempted = true;
    result->rollback_stage = "rollback-antenna-gpio-routes";
    esp_err_t rollback = ESP_OK;
    for (unsigned i = 0; i < 4; ++i) {
        const antenna_pin_t *entry = &next.pins[i];
        if (!entry->selected || !(added & (UINT64_C(1) << entry->pin))) continue;
        esp_err_t restored = antenna_route_restore(entry->pin, &entry->original);
        if (rollback == ESP_OK) rollback = restored;
    }
    for (unsigned i = 0; s_antenna_gpio != NULL && i < 4; ++i) {
        const antenna_pin_t *entry = &s_antenna_gpio->pins[i];
        if (!entry->selected) continue;
        esp_err_t restored = antenna_route_restore(entry->pin, &entry->applied);
        if (rollback == ESP_OK) rollback = restored;
    }
    /* Restore the SDK RAM snapshot without configuring a second set of pins. */
    esp32qjs_phy_ant_gpio_restore_config(&previous);
    if (rollback == ESP_OK) {
        rollback = esp_phy_get_ant_gpio(&observed);
        if (rollback == ESP_OK && !antenna_gpio_equal(&previous, &observed)) rollback = ESP_ERR_INVALID_RESPONSE;
    }
    result->rollback_error = rollback;
    result->rollback_complete = rollback == ESP_OK;
    if (result->rollback_complete) {
        result->rollback_stage = NULL;
        esp_gpio_revoke(added);
    } else atomic_store_explicit(&s_antenna_fault, rollback, memory_order_release);
    esp32_mquickjs_memory_payload_free(allocation);
    return error;
}

typedef struct {
    bool gpio;
    const esp32_mquickjs_wifi_antenna_snapshot_t *requested;
    esp32_mquickjs_wifi_radio_config_result_t *result;
} antenna_transaction_t;

static esp_err_t antenna_apply_idle(void *opaque)
{
    antenna_transaction_t *transaction = opaque;
    esp32_mquickjs_wifi_radio_config_result_t *result = transaction->result;
    result->stage = "antenna-modem-ownership";
#if CONFIG_ESP32_MQUICKJS_FEATURE_BLE
    if (!esp32_mquickjs_ble_phy_idle()) return ESP_ERR_INVALID_STATE;
#endif
#if CONFIG_BT_ENABLED
    if (esp_bt_controller_get_status() != ESP_BT_CONTROLLER_STATUS_IDLE) return ESP_ERR_INVALID_STATE;
#endif
#if CONFIG_IEEE802154_ENABLED
    if (esp_ieee802154_get_state() != ESP_IEEE802154_RADIO_DISABLE) return ESP_ERR_INVALID_STATE;
#endif
    if (transaction->gpio) return antenna_write_gpio(&transaction->requested->gpio, result);
    esp_phy_ant_config_t previous, observed, work = transaction->requested->config;
    result->stage = "antenna-snapshot";
    esp_err_t error = esp_phy_get_ant(&previous);
    if (error != ESP_OK) return error;
    if (!esp32_mquickjs_wifi_antenna_valid(&previous)) return ESP_ERR_INVALID_RESPONSE;
    if (antenna_config_equal(&previous, &work)) { result->stage = "complete"; return ESP_OK; }
    result->stage = "antenna-write";
    result->mutation_attempted = true;
    error = esp_phy_set_ant(&work);
    if (error == ESP_OK) {
        result->stage = "antenna-readback";
        error = esp_phy_get_ant(&observed);
        if (error == ESP_OK && !antenna_config_equal(&transaction->requested->config, &observed))
            error = ESP_ERR_INVALID_RESPONSE;
    }
    if (error == ESP_OK) { result->stage = "complete"; return ESP_OK; }
    result->rollback_attempted = true;
    result->rollback_stage = "rollback-antenna";
    work = previous;
    result->rollback_error = esp_phy_set_ant(&work);
    if (result->rollback_error == ESP_OK) {
        result->rollback_stage = "rollback-antenna-readback";
        result->rollback_error = esp_phy_get_ant(&observed);
        if (result->rollback_error == ESP_OK && !antenna_config_equal(&previous, &observed))
            result->rollback_error = ESP_ERR_INVALID_RESPONSE;
    }
    result->rollback_complete = result->rollback_error == ESP_OK;
    if (result->rollback_complete) result->rollback_stage = NULL;
    else atomic_store_explicit(&s_antenna_fault, result->rollback_error, memory_order_release);
    return error;
}

esp_err_t esp32_mquickjs_wifi_antenna_write(bool gpio,
    const esp32_mquickjs_wifi_antenna_snapshot_t *requested,
    esp32_mquickjs_wifi_radio_config_result_t *result)
{
    if (requested == NULL || result == NULL || (!gpio && !esp32_mquickjs_wifi_antenna_valid(&requested->config)))
        return ESP_ERR_INVALID_ARG;
    result->stage = "antenna-phy-idle";
    esp_err_t fault = esp32_mquickjs_wifi_antenna_fault();
    if (fault != ESP_OK) { result->stage = "antenna-device-restart-required"; return fault; }
    antenna_transaction_t transaction = {gpio, requested, result};
    return esp32qjs_phy_antenna_run_idle(antenna_apply_idle, &transaction);
}
#endif
