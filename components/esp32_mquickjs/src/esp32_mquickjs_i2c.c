#include "esp32_mquickjs_internal.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_heap_caps.h"

typedef struct {
    bool opened;
    gpio_num_t sda_pin;
    gpio_num_t scl_pin;
    uint32_t freq_hz;
    uint32_t timeout_ms;
    bool internal_pullup;
    i2c_master_bus_handle_t bus_handle;
} esp32_mquickjs_i2c_state_t;

static esp32_mquickjs_i2c_state_t s_i2c_state;

static bool js_value_to_gpio_num(JSContext *ctx, JSValue value, gpio_num_t *out_pin)
{
    int pin = -1;

    if (JS_ToInt32(ctx, &pin, value) != 0) {
        return false;
    }
    if (pin < 0 || pin >= GPIO_NUM_MAX || !GPIO_IS_VALID_GPIO(pin)) {
        return false;
    }

    *out_pin = (gpio_num_t)pin;
    return true;
}

static bool js_value_to_u32(JSContext *ctx, JSValue value, uint32_t *out_value)
{
    int raw_value = 0;

    if (JS_ToInt32(ctx, &raw_value, value) != 0 || raw_value < 0) {
        return false;
    }
    *out_value = (uint32_t)raw_value;
    return true;
}

static bool js_value_to_bool(JSContext *ctx, JSValue value, bool *out_value)
{
    int raw_value = 0;

    if (JS_IsBool(value)) {
        *out_value = (value == JS_TRUE);
        return true;
    }
    if (JS_ToInt32(ctx, &raw_value, value) != 0) {
        return false;
    }
    *out_value = raw_value != 0;
    return true;
}

static JSValue i2c_make_status_object(JSContext *ctx)
{
    JSGCRef status_ref;
    JSValue *status_obj;

    status_obj = JS_PushGCRef(ctx, &status_ref);
    *status_obj = JS_NewObject(ctx);
    if (JS_IsException(*status_obj)) {
        JS_PopGCRef(ctx, &status_ref);
        return JS_EXCEPTION;
    }

    if (!esp32_mquickjs_set_property(ctx, *status_obj, "opened",
                                     JS_NewBool(s_i2c_state.opened)) ||
        !esp32_mquickjs_set_property(ctx, *status_obj, "sda",
                                     JS_NewInt32(ctx, (int32_t)s_i2c_state.sda_pin)) ||
        !esp32_mquickjs_set_property(ctx, *status_obj, "scl",
                                     JS_NewInt32(ctx, (int32_t)s_i2c_state.scl_pin)) ||
        !esp32_mquickjs_set_property(ctx, *status_obj, "freqHz",
                                     JS_NewUint32(ctx, s_i2c_state.freq_hz)) ||
        !esp32_mquickjs_set_property(ctx, *status_obj, "timeoutMs",
                                     JS_NewUint32(ctx, s_i2c_state.timeout_ms)) ||
        !esp32_mquickjs_set_property(ctx, *status_obj, "internalPullup",
                                     JS_NewBool(s_i2c_state.internal_pullup))) {
        JS_PopGCRef(ctx, &status_ref);
        return JS_EXCEPTION;
    }

    return JS_PopGCRef(ctx, &status_ref);
}

static void i2c_reset_state(void)
{
    memset(&s_i2c_state, 0, sizeof(s_i2c_state));
    s_i2c_state.sda_pin = (gpio_num_t)ESP32_MQUICKJS_I2C_DEFAULT_SDA_PIN;
    s_i2c_state.scl_pin = (gpio_num_t)ESP32_MQUICKJS_I2C_DEFAULT_SCL_PIN;
    s_i2c_state.freq_hz = ESP32_MQUICKJS_I2C_DEFAULT_FREQ_HZ;
    s_i2c_state.timeout_ms = ESP32_MQUICKJS_I2C_DEFAULT_TIMEOUT_MS;
    s_i2c_state.internal_pullup = ESP32_MQUICKJS_I2C_ENABLE_INTERNAL_PULLUP;
}

static esp_err_t i2c_close_bus(void)
{
    esp_err_t err = ESP_OK;

    if (s_i2c_state.bus_handle != NULL) {
        err = i2c_del_master_bus(s_i2c_state.bus_handle);
    }
    i2c_reset_state();
    return err;
}

static JSValue i2c_throw_error(JSContext *ctx,
                               esp_err_t err,
                               const char *message)
{
    return JS_ThrowInternalError(ctx, "%s: %s", message, esp_err_to_name(err));
}

static JSValue i2c_open(JSContext *ctx, int argc, JSValue *argv)
{
    gpio_num_t sda_pin = (gpio_num_t)ESP32_MQUICKJS_I2C_DEFAULT_SDA_PIN;
    gpio_num_t scl_pin = (gpio_num_t)ESP32_MQUICKJS_I2C_DEFAULT_SCL_PIN;
    uint32_t freq_hz = ESP32_MQUICKJS_I2C_DEFAULT_FREQ_HZ;
    uint32_t timeout_ms = ESP32_MQUICKJS_I2C_DEFAULT_TIMEOUT_MS;
    bool internal_pullup = ESP32_MQUICKJS_I2C_ENABLE_INTERNAL_PULLUP;
    i2c_master_bus_config_t bus_config = {0};
    esp_err_t err;

    if (argc >= 1 && !JS_IsUndefined(argv[0])) {
        JSValue property = JS_UNDEFINED;

        if (JS_GetClassID(ctx, argv[0]) < 0) {
            return JS_ThrowTypeError(ctx,
                                     "i2c.open(options?) expects an object with sda/scl/freqHz/timeoutMs/internalPullup");
        }

        property = JS_GetPropertyStr(ctx, argv[0], "sda");
        if (JS_IsException(property)) {
            return JS_EXCEPTION;
        }
        if (!JS_IsUndefined(property) && !js_value_to_gpio_num(ctx, property, &sda_pin)) {
            return JS_ThrowTypeError(ctx, "i2c.open({ sda }) expects a valid GPIO");
        }

        property = JS_GetPropertyStr(ctx, argv[0], "scl");
        if (JS_IsException(property)) {
            return JS_EXCEPTION;
        }
        if (!JS_IsUndefined(property) && !js_value_to_gpio_num(ctx, property, &scl_pin)) {
            return JS_ThrowTypeError(ctx, "i2c.open({ scl }) expects a valid GPIO");
        }

        property = JS_GetPropertyStr(ctx, argv[0], "freqHz");
        if (JS_IsException(property)) {
            return JS_EXCEPTION;
        }
        if (JS_IsUndefined(property)) {
            property = JS_GetPropertyStr(ctx, argv[0], "freq");
            if (JS_IsException(property)) {
                return JS_EXCEPTION;
            }
        }
        if (!JS_IsUndefined(property) &&
            (!js_value_to_u32(ctx, property, &freq_hz) || freq_hz == 0)) {
            return JS_ThrowTypeError(ctx, "i2c.open({ freqHz }) expects a positive integer");
        }

        property = JS_GetPropertyStr(ctx, argv[0], "timeoutMs");
        if (JS_IsException(property)) {
            return JS_EXCEPTION;
        }
        if (!JS_IsUndefined(property) && !js_value_to_u32(ctx, property, &timeout_ms)) {
            return JS_ThrowTypeError(ctx, "i2c.open({ timeoutMs }) expects a non-negative integer");
        }

        property = JS_GetPropertyStr(ctx, argv[0], "internalPullup");
        if (JS_IsException(property)) {
            return JS_EXCEPTION;
        }
        if (!JS_IsUndefined(property) && !js_value_to_bool(ctx, property, &internal_pullup)) {
            return JS_ThrowTypeError(ctx, "i2c.open({ internalPullup }) expects a boolean-like value");
        }
    }

    err = i2c_close_bus();
    if (err != ESP_OK) {
        return i2c_throw_error(ctx, err, "i2c.close() failed before reopening");
    }

    bus_config.i2c_port = (i2c_port_num_t)-1;
    bus_config.sda_io_num = sda_pin;
    bus_config.scl_io_num = scl_pin;
    bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_config.glitch_ignore_cnt = 7;
    bus_config.flags.enable_internal_pullup = internal_pullup ? 1U : 0U;

    err = i2c_new_master_bus(&bus_config, &s_i2c_state.bus_handle);
    if (err != ESP_OK) {
        i2c_reset_state();
        return i2c_throw_error(ctx, err, "i2c.open() failed");
    }

    s_i2c_state.opened = true;
    s_i2c_state.sda_pin = sda_pin;
    s_i2c_state.scl_pin = scl_pin;
    s_i2c_state.freq_hz = freq_hz;
    s_i2c_state.timeout_ms = timeout_ms;
    s_i2c_state.internal_pullup = internal_pullup;
    return i2c_make_status_object(ctx);
}

static bool i2c_require_open(JSContext *ctx, JSValue *result)
{
    if (!s_i2c_state.opened || s_i2c_state.bus_handle == NULL) {
        *result = JS_ThrowInternalError(ctx, "i2c bus is not open; call i2c.open(...) first");
        return false;
    }
    return true;
}

static bool js_value_to_i2c_address(JSContext *ctx, JSValue value, uint16_t *out_address)
{
    uint32_t address = 0;

    if (!js_value_to_u32(ctx, value, &address) || address > 0x7f || address < 0x03) {
        return false;
    }

    *out_address = (uint16_t)address;
    return true;
}

static bool js_value_to_byte_array(JSContext *ctx,
                                   JSValue value,
                                   const char *api_name,
                                   uint8_t **out_bytes,
                                   size_t *out_len,
                                   JSValue *out_error)
{
    uint32_t length = 0;
    uint8_t *bytes;
    JSValue length_value;
    uint32_t i;

    *out_bytes = NULL;
    *out_len = 0;
    *out_error = JS_UNDEFINED;

    if (JS_GetClassID(ctx, value) < 0) {
        *out_error = JS_ThrowTypeError(ctx, "%s expects an array-like object of byte values", api_name);
        return false;
    }

    length_value = JS_GetPropertyStr(ctx, value, "length");
    if (JS_IsException(length_value) || !js_value_to_u32(ctx, length_value, &length)) {
        *out_error = JS_ThrowTypeError(ctx, "%s expects an array-like object with a numeric length", api_name);
        return false;
    }

    if (length == 0) {
        return true;
    }

    bytes = heap_caps_malloc(length, MALLOC_CAP_8BIT);
    if (bytes == NULL) {
        *out_error = JS_ThrowOutOfMemory(ctx);
        return false;
    }

    for (i = 0; i < length; ++i) {
        JSValue item = JS_GetPropertyUint32(ctx, value, i);
        uint32_t raw_byte = 0;

        if (JS_IsException(item) || !js_value_to_u32(ctx, item, &raw_byte) || raw_byte > 0xff) {
            heap_caps_free(bytes);
            *out_error = JS_ThrowTypeError(ctx, "%s expects byte values in the range 0-255", api_name);
            return false;
        }
        bytes[i] = (uint8_t)raw_byte;
    }

    *out_bytes = bytes;
    *out_len = length;
    return true;
}

static JSValue js_bytes_to_array(JSContext *ctx, const uint8_t *bytes, size_t length)
{
    JSGCRef array_ref;
    JSValue *array_obj;
    uint32_t i;

    array_obj = JS_PushGCRef(ctx, &array_ref);
    *array_obj = JS_NewArray(ctx, 0);
    if (JS_IsException(*array_obj)) {
        JS_PopGCRef(ctx, &array_ref);
        return JS_EXCEPTION;
    }

    for (i = 0; i < length; ++i) {
        if (JS_IsException(JS_SetPropertyUint32(ctx, *array_obj, i, JS_NewInt32(ctx, bytes[i])))) {
            JS_PopGCRef(ctx, &array_ref);
            return JS_EXCEPTION;
        }
    }

    return JS_PopGCRef(ctx, &array_ref);
}

static esp_err_t i2c_with_device(uint16_t address,
                                 i2c_master_dev_handle_t *out_handle)
{
    i2c_device_config_t device_config = {0};

    if (!s_i2c_state.opened || s_i2c_state.bus_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    device_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    device_config.device_address = address;
    device_config.scl_speed_hz = s_i2c_state.freq_hz;
    device_config.scl_wait_us = s_i2c_state.timeout_ms * 1000U;
    device_config.flags.disable_ack_check = 0;
    return i2c_master_bus_add_device(s_i2c_state.bus_handle, &device_config, out_handle);
}

static JSValue i2c_close(JSContext *ctx)
{
    esp_err_t err = i2c_close_bus();

    if (err != ESP_OK) {
        return i2c_throw_error(ctx, err, "i2c.close() failed");
    }
    return JS_NewBool(true);
}

static JSValue i2c_scan(JSContext *ctx)
{
    JSGCRef array_ref;
    JSValue *array_obj;
    uint32_t index = 0;
    uint16_t address;

    array_obj = JS_PushGCRef(ctx, &array_ref);
    *array_obj = JS_NewArray(ctx, 0);
    if (JS_IsException(*array_obj)) {
        JS_PopGCRef(ctx, &array_ref);
        return JS_EXCEPTION;
    }

    for (address = 0x03; address <= 0x77; ++address) {
        esp_err_t err = i2c_master_probe(s_i2c_state.bus_handle, address, (int)s_i2c_state.timeout_ms);

        if (err == ESP_OK) {
            if (JS_IsException(JS_SetPropertyUint32(ctx, *array_obj, index++, JS_NewInt32(ctx, address)))) {
                JS_PopGCRef(ctx, &array_ref);
                return JS_EXCEPTION;
            }
            continue;
        }
        if (err == ESP_ERR_NOT_FOUND || err == ESP_ERR_INVALID_RESPONSE) {
            continue;
        }

        JS_PopGCRef(ctx, &array_ref);
        return i2c_throw_error(ctx, err, "i2c.scan() failed");
    }

    return JS_PopGCRef(ctx, &array_ref);
}

static JSValue i2c_write(JSContext *ctx, uint16_t address, JSValue data_value)
{
    i2c_master_dev_handle_t device_handle = NULL;
    uint8_t *bytes = NULL;
    size_t length = 0;
    JSValue error = JS_UNDEFINED;
    esp_err_t err;

    if (!js_value_to_byte_array(ctx, data_value, "i2c.write(addr, data)", &bytes, &length, &error)) {
        return error;
    }

    err = i2c_with_device(address, &device_handle);
    if (err != ESP_OK) {
        heap_caps_free(bytes);
        return i2c_throw_error(ctx, err, "i2c.write() failed to add device");
    }

    err = i2c_master_transmit(device_handle, bytes, length, (int)s_i2c_state.timeout_ms);
    i2c_master_bus_rm_device(device_handle);
    heap_caps_free(bytes);
    if (err != ESP_OK) {
        return i2c_throw_error(ctx, err, "i2c.write() failed");
    }

    return JS_NewInt32(ctx, (int32_t)length);
}

static JSValue i2c_read(JSContext *ctx, uint16_t address, uint32_t length)
{
    i2c_master_dev_handle_t device_handle = NULL;
    uint8_t *bytes = NULL;
    JSValue result;
    esp_err_t err;

    if (length == 0) {
        return JS_NewArray(ctx, 0);
    }

    bytes = heap_caps_malloc(length, MALLOC_CAP_8BIT);
    if (bytes == NULL) {
        return JS_ThrowOutOfMemory(ctx);
    }

    err = i2c_with_device(address, &device_handle);
    if (err != ESP_OK) {
        heap_caps_free(bytes);
        return i2c_throw_error(ctx, err, "i2c.read() failed to add device");
    }

    err = i2c_master_receive(device_handle, bytes, length, (int)s_i2c_state.timeout_ms);
    i2c_master_bus_rm_device(device_handle);
    if (err != ESP_OK) {
        heap_caps_free(bytes);
        return i2c_throw_error(ctx, err, "i2c.read() failed");
    }

    result = js_bytes_to_array(ctx, bytes, length);
    heap_caps_free(bytes);
    return result;
}

static JSValue i2c_write_read(JSContext *ctx,
                              uint16_t address,
                              JSValue write_value,
                              uint32_t read_length)
{
    i2c_master_dev_handle_t device_handle = NULL;
    uint8_t *write_bytes = NULL;
    size_t write_length = 0;
    uint8_t *read_bytes = NULL;
    JSValue error = JS_UNDEFINED;
    JSValue result;
    esp_err_t err;

    if (!js_value_to_byte_array(ctx,
                                write_value,
                                "i2c.writeRead(addr, writeData, readLength)",
                                &write_bytes,
                                &write_length,
                                &error)) {
        return error;
    }

    if (read_length > 0) {
        read_bytes = heap_caps_malloc(read_length, MALLOC_CAP_8BIT);
        if (read_bytes == NULL) {
            heap_caps_free(write_bytes);
            return JS_ThrowOutOfMemory(ctx);
        }
    }

    err = i2c_with_device(address, &device_handle);
    if (err != ESP_OK) {
        heap_caps_free(read_bytes);
        heap_caps_free(write_bytes);
        return i2c_throw_error(ctx, err, "i2c.writeRead() failed to add device");
    }

    err = i2c_master_transmit_receive(device_handle,
                                      write_bytes,
                                      write_length,
                                      read_bytes,
                                      read_length,
                                      (int)s_i2c_state.timeout_ms);
    i2c_master_bus_rm_device(device_handle);
    heap_caps_free(write_bytes);
    if (err != ESP_OK) {
        heap_caps_free(read_bytes);
        return i2c_throw_error(ctx, err, "i2c.writeRead() failed");
    }

    result = js_bytes_to_array(ctx, read_bytes, read_length);
    heap_caps_free(read_bytes);
    return result;
}

bool esp32_mquickjs_install_i2c_module(JSContext *ctx, JSValue global_obj)
{
    JSGCRef module_ref;
    JSValue *module_obj;

    i2c_reset_state();

    module_obj = JS_PushGCRef(ctx, &module_ref);
    *module_obj = JS_NewObject(ctx);
    if (JS_IsException(*module_obj)) {
        goto fail;
    }

    if (!esp32_mquickjs_set_property(ctx, *module_obj, "DEFAULT_SDA",
                                     JS_NewInt32(ctx, ESP32_MQUICKJS_I2C_DEFAULT_SDA_PIN)) ||
        !esp32_mquickjs_set_property(ctx, *module_obj, "DEFAULT_SCL",
                                     JS_NewInt32(ctx, ESP32_MQUICKJS_I2C_DEFAULT_SCL_PIN)) ||
        !esp32_mquickjs_set_property(ctx, *module_obj, "DEFAULT_FREQ_HZ",
                                     JS_NewUint32(ctx, ESP32_MQUICKJS_I2C_DEFAULT_FREQ_HZ)) ||
        !esp32_mquickjs_set_property(ctx, *module_obj, "DEFAULT_TIMEOUT_MS",
                                     JS_NewUint32(ctx, ESP32_MQUICKJS_I2C_DEFAULT_TIMEOUT_MS)) ||
        !esp32_mquickjs_set_bound_bridge_function(ctx, *module_obj, global_obj, "open", "i2c.open") ||
        !esp32_mquickjs_set_bound_bridge_function(ctx, *module_obj, global_obj, "close", "i2c.close") ||
        !esp32_mquickjs_set_bound_bridge_function(ctx, *module_obj, global_obj, "status", "i2c.status") ||
        !esp32_mquickjs_set_bound_bridge_function(ctx, *module_obj, global_obj, "scan", "i2c.scan") ||
        !esp32_mquickjs_set_bound_bridge_function(ctx, *module_obj, global_obj, "write", "i2c.write") ||
        !esp32_mquickjs_set_bound_bridge_function(ctx, *module_obj, global_obj, "read", "i2c.read") ||
        !esp32_mquickjs_set_bound_bridge_function(ctx, *module_obj, global_obj, "writeRead", "i2c.writeRead")) {
        goto fail;
    }

    if (!esp32_mquickjs_set_property(ctx, global_obj, "i2c", JS_PopGCRef(ctx, &module_ref))) {
        return false;
    }
    return true;

fail:
    JS_PopGCRef(ctx, &module_ref);
    return false;
}

bool esp32_mquickjs_dispatch_i2c(JSContext *ctx,
                                 const char *operation,
                                 int argc,
                                 JSValue *argv,
                                 JSValue *result)
{
    if (strcmp(operation, "open") == 0) {
        *result = i2c_open(ctx, argc, argv);
        return true;
    }

    if (strcmp(operation, "close") == 0) {
        *result = i2c_close(ctx);
        return true;
    }

    if (strcmp(operation, "status") == 0) {
        *result = i2c_make_status_object(ctx);
        return true;
    }

    if (!i2c_require_open(ctx, result)) {
        return true;
    }

    if (strcmp(operation, "scan") == 0) {
        *result = i2c_scan(ctx);
        return true;
    }

    if (strcmp(operation, "write") == 0) {
        uint16_t address = 0;

        if (argc < 2 || !js_value_to_i2c_address(ctx, argv[0], &address)) {
            *result = JS_ThrowTypeError(ctx, "i2c.write(addr, data) expects a 7-bit address and byte array");
            return true;
        }
        *result = i2c_write(ctx, address, argv[1]);
        return true;
    }

    if (strcmp(operation, "read") == 0) {
        uint16_t address = 0;
        uint32_t length = 0;

        if (argc < 2 || !js_value_to_i2c_address(ctx, argv[0], &address) ||
            !js_value_to_u32(ctx, argv[1], &length)) {
            *result = JS_ThrowTypeError(ctx, "i2c.read(addr, length) expects a 7-bit address and byte length");
            return true;
        }
        *result = i2c_read(ctx, address, length);
        return true;
    }

    if (strcmp(operation, "writeRead") == 0) {
        uint16_t address = 0;
        uint32_t read_length = 0;

        if (argc < 3 || !js_value_to_i2c_address(ctx, argv[0], &address) ||
            !js_value_to_u32(ctx, argv[2], &read_length)) {
            *result = JS_ThrowTypeError(ctx,
                                        "i2c.writeRead(addr, writeData, readLength) expects a 7-bit address, write bytes, and read length");
            return true;
        }
        *result = i2c_write_read(ctx, address, argv[1], read_length);
        return true;
    }

    return false;
}
