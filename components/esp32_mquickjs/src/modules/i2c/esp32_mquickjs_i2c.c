#include "esp32_mquickjs_i2c.h"

#if CONFIG_ESP32_MQUICKJS_FEATURE_I2C

#include "esp32_mquickjs_core.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "soc/soc_caps.h"

typedef struct {
    int32_t bus_id;
    uint32_t generation;
} esp32_mquickjs_i2c_bus_ref_t;

typedef struct {
    bool allocated;
    int32_t bus_id;
    uint32_t generation;
    gpio_num_t sda_pin;
    gpio_num_t scl_pin;
    uint32_t freq_hz;
    uint32_t timeout_ms;
    bool internal_pullup;
    i2c_master_bus_handle_t bus_handle;
} esp32_mquickjs_i2c_slot_t;

static esp32_mquickjs_i2c_slot_t s_i2c_slots[SOC_I2C_NUM];
static uint32_t s_i2c_next_generation = 1;

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

static JSValue i2c_throw_error(JSContext *ctx,
                               esp_err_t err,
                               const char *message)
{
    return JS_ThrowInternalError(ctx, "%s: %s", message, esp_err_to_name(err));
}

static void i2c_init_slot(esp32_mquickjs_i2c_slot_t *slot, int32_t bus_id)
{
    memset(slot, 0, sizeof(*slot));
    slot->bus_id = bus_id;
}

static void i2c_reset_slots(void)
{
    int32_t i;

    for (i = 0; i < (int32_t)SOC_I2C_NUM; ++i) {
        i2c_init_slot(&s_i2c_slots[i], i);
    }
    s_i2c_next_generation = 1;
}

static uint32_t i2c_take_generation(void)
{
    uint32_t generation = s_i2c_next_generation++;

    if (generation == 0) {
        generation = s_i2c_next_generation++;
    }
    return generation;
}

static esp32_mquickjs_i2c_slot_t *i2c_alloc_slot(void)
{
    int32_t i;

    for (i = 0; i < (int32_t)SOC_I2C_NUM; ++i) {
        esp32_mquickjs_i2c_slot_t *slot = &s_i2c_slots[i];

        if (slot->allocated) {
            continue;
        }
        i2c_init_slot(slot, i);
        slot->allocated = true;
        slot->generation = i2c_take_generation();
        return slot;
    }
    return NULL;
}

static void i2c_cleanup_slot(esp32_mquickjs_i2c_slot_t *slot)
{
    int32_t bus_id;

    if (slot == NULL || !slot->allocated) {
        return;
    }

    bus_id = slot->bus_id;
    if (slot->bus_handle != NULL) {
        i2c_del_master_bus(slot->bus_handle);
    }
    i2c_init_slot(slot, bus_id);
}

static esp32_mquickjs_i2c_slot_t *i2c_get_slot(const esp32_mquickjs_i2c_bus_ref_t *ref)
{
    esp32_mquickjs_i2c_slot_t *slot;

    if (ref == NULL || ref->bus_id < 0 || ref->bus_id >= (int32_t)SOC_I2C_NUM) {
        return NULL;
    }

    slot = &s_i2c_slots[ref->bus_id];
    if (!slot->allocated || slot->generation != ref->generation) {
        return NULL;
    }
    return slot;
}

static JSValue i2c_make_status_object(JSContext *ctx, const esp32_mquickjs_i2c_slot_t *slot)
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
                                     JS_NewBool(slot != NULL && slot->allocated)) ||
        !esp32_mquickjs_set_property(ctx, *status_obj, "sda",
                                     JS_NewInt32(ctx, slot != NULL ? (int32_t)slot->sda_pin : ESP32_MQUICKJS_I2C_DEFAULT_SDA_PIN)) ||
        !esp32_mquickjs_set_property(ctx, *status_obj, "scl",
                                     JS_NewInt32(ctx, slot != NULL ? (int32_t)slot->scl_pin : ESP32_MQUICKJS_I2C_DEFAULT_SCL_PIN)) ||
        !esp32_mquickjs_set_property(ctx, *status_obj, "freqHz",
                                     JS_NewUint32(ctx, slot != NULL ? slot->freq_hz : ESP32_MQUICKJS_I2C_DEFAULT_FREQ_HZ)) ||
        !esp32_mquickjs_set_property(ctx, *status_obj, "timeoutMs",
                                     JS_NewUint32(ctx, slot != NULL ? slot->timeout_ms : ESP32_MQUICKJS_I2C_DEFAULT_TIMEOUT_MS)) ||
        !esp32_mquickjs_set_property(ctx, *status_obj, "internalPullup",
                                     JS_NewBool(slot != NULL ? slot->internal_pullup : ESP32_MQUICKJS_I2C_ENABLE_INTERNAL_PULLUP))) {
        JS_PopGCRef(ctx, &status_ref);
        return JS_EXCEPTION;
    }

    return JS_PopGCRef(ctx, &status_ref);
}

static JSValue i2c_make_bus_object(JSContext *ctx, const esp32_mquickjs_i2c_slot_t *slot)
{
    JSGCRef bus_ref;
    JSValue *bus_obj;
    esp32_mquickjs_i2c_bus_ref_t *bus_data;

    bus_obj = JS_PushGCRef(ctx, &bus_ref);
    *bus_obj = JS_NewObjectClassUser(ctx, JS_CLASS_I2C_BUS);
    if (JS_IsException(*bus_obj) || slot == NULL) {
        JS_PopGCRef(ctx, &bus_ref);
        return JS_EXCEPTION;
    }

    bus_data = heap_caps_malloc(sizeof(*bus_data), MALLOC_CAP_8BIT);
    if (bus_data == NULL) {
        JS_PopGCRef(ctx, &bus_ref);
        return JS_ThrowOutOfMemory(ctx);
    }
    bus_data->bus_id = slot->bus_id;
    bus_data->generation = slot->generation;
    JS_SetOpaque(ctx, *bus_obj, bus_data);

    return JS_PopGCRef(ctx, &bus_ref);
}

static int i2c_bus_ref_from_object(JSContext *ctx,
                                   JSValue bus_value,
                                   const char *api_name,
                                   esp32_mquickjs_i2c_bus_ref_t *out_ref)
{
    const esp32_mquickjs_i2c_bus_ref_t *bus_ref;

    if (out_ref == NULL || JS_GetClassID(ctx, bus_value) != JS_CLASS_I2C_BUS) {
        JS_ThrowTypeError(ctx, "%s expects a valid I2CBus instance", api_name);
        return -1;
    }
    bus_ref = JS_GetOpaque(ctx, bus_value);
    if (bus_ref == NULL) {
        JS_ThrowTypeError(ctx, "%s expects a valid I2CBus instance", api_name);
        return -1;
    }
    *out_ref = *bus_ref;
    return 0;
}

static int i2c_get_bound_slot(JSContext *ctx,
                              JSValue bus_value,
                              const char *api_name,
                              esp32_mquickjs_i2c_bus_ref_t *out_ref,
                              esp32_mquickjs_i2c_slot_t **out_slot)
{
    esp32_mquickjs_i2c_slot_t *slot;

    if (i2c_bus_ref_from_object(ctx, bus_value, api_name, out_ref) != 0) {
        return -1;
    }
    slot = i2c_get_slot(out_ref);
    if (slot == NULL) {
        JS_ThrowReferenceError(ctx, "%s failed because the I2C bus is closed", api_name);
        return -1;
    }
    if (out_slot != NULL) {
        *out_slot = slot;
    }
    return 0;
}

static int i2c_get_this_slot(JSContext *ctx,
                             JSValue this_value,
                             const char *api_name,
                             esp32_mquickjs_i2c_bus_ref_t *out_ref,
                             esp32_mquickjs_i2c_slot_t **out_slot)
{
    return i2c_get_bound_slot(ctx, this_value, api_name, out_ref, out_slot);
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
    JSGCRef length_ref;
    JSValue *length_value;
    uint32_t length = 0;
    uint8_t *bytes;
    uint32_t i;

    *out_bytes = NULL;
    *out_len = 0;
    *out_error = JS_UNDEFINED;

    if (JS_GetClassID(ctx, value) < 0) {
        *out_error = JS_ThrowTypeError(ctx, "%s expects an array-like object of byte values", api_name);
        return false;
    }

    length_value = JS_PushGCRef(ctx, &length_ref);
    *length_value = JS_GetPropertyStr(ctx, value, "length");
    if (JS_IsException(*length_value) || !js_value_to_u32(ctx, *length_value, &length)) {
        JS_PopGCRef(ctx, &length_ref);
        *out_error = JS_ThrowTypeError(ctx, "%s expects an array-like object with a numeric length", api_name);
        return false;
    }
    JS_PopGCRef(ctx, &length_ref);

    if (length == 0) {
        return true;
    }

    bytes = heap_caps_malloc(length, MALLOC_CAP_8BIT);
    if (bytes == NULL) {
        *out_error = JS_ThrowOutOfMemory(ctx);
        return false;
    }

    for (i = 0; i < length; ++i) {
        JSGCRef item_ref;
        JSValue *item = JS_PushGCRef(ctx, &item_ref);
        uint32_t raw_byte = 0;

        *item = JS_GetPropertyUint32(ctx, value, i);
        if (JS_IsException(*item) || !js_value_to_u32(ctx, *item, &raw_byte) || raw_byte > 0xff) {
            JS_PopGCRef(ctx, &item_ref);
            heap_caps_free(bytes);
            *out_error = JS_ThrowTypeError(ctx, "%s expects byte values in the range 0-255", api_name);
            return false;
        }
        bytes[i] = (uint8_t)raw_byte;
        JS_PopGCRef(ctx, &item_ref);
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

static esp_err_t i2c_with_device(const esp32_mquickjs_i2c_slot_t *slot,
                                 uint16_t address,
                                 i2c_master_dev_handle_t *out_handle)
{
    i2c_device_config_t device_config = {0};

    if (slot == NULL || !slot->allocated || slot->bus_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    device_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    device_config.device_address = address;
    device_config.scl_speed_hz = slot->freq_hz;
    device_config.scl_wait_us = slot->timeout_ms * 1000U;
    device_config.flags.disable_ack_check = 0;
    return i2c_master_bus_add_device(slot->bus_handle, &device_config, out_handle);
}

static JSValue i2c_scan(JSContext *ctx, const esp32_mquickjs_i2c_slot_t *slot)
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
        esp_err_t err = i2c_master_probe(slot->bus_handle, address, (int)slot->timeout_ms);

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
        return i2c_throw_error(ctx, err, "I2CBus.scan() failed");
    }

    return JS_PopGCRef(ctx, &array_ref);
}

static JSValue i2c_write(JSContext *ctx,
                         const esp32_mquickjs_i2c_slot_t *slot,
                         uint16_t address,
                         JSValue data_value)
{
    i2c_master_dev_handle_t device_handle = NULL;
    uint8_t *bytes = NULL;
    size_t length = 0;
    JSValue error = JS_UNDEFINED;
    esp_err_t err;

    if (!js_value_to_byte_array(ctx, data_value, "I2CBus.write(addr, data)", &bytes, &length, &error)) {
        return error;
    }

    err = i2c_with_device(slot, address, &device_handle);
    if (err != ESP_OK) {
        heap_caps_free(bytes);
        return i2c_throw_error(ctx, err, "I2CBus.write() failed to add device");
    }

    err = i2c_master_transmit(device_handle, bytes, length, (int)slot->timeout_ms);
    i2c_master_bus_rm_device(device_handle);
    heap_caps_free(bytes);
    if (err != ESP_OK) {
        return i2c_throw_error(ctx, err, "I2CBus.write() failed");
    }

    return JS_NewInt32(ctx, (int32_t)length);
}

static JSValue i2c_read(JSContext *ctx,
                        const esp32_mquickjs_i2c_slot_t *slot,
                        uint16_t address,
                        uint32_t length)
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

    err = i2c_with_device(slot, address, &device_handle);
    if (err != ESP_OK) {
        heap_caps_free(bytes);
        return i2c_throw_error(ctx, err, "I2CBus.read() failed to add device");
    }

    err = i2c_master_receive(device_handle, bytes, length, (int)slot->timeout_ms);
    i2c_master_bus_rm_device(device_handle);
    if (err != ESP_OK) {
        heap_caps_free(bytes);
        return i2c_throw_error(ctx, err, "I2CBus.read() failed");
    }

    result = js_bytes_to_array(ctx, bytes, length);
    heap_caps_free(bytes);
    return result;
}

static JSValue i2c_write_read(JSContext *ctx,
                              const esp32_mquickjs_i2c_slot_t *slot,
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
                                "I2CBus.writeRead(addr, writeData, readLength)",
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

    err = i2c_with_device(slot, address, &device_handle);
    if (err != ESP_OK) {
        heap_caps_free(read_bytes);
        heap_caps_free(write_bytes);
        return i2c_throw_error(ctx, err, "I2CBus.writeRead() failed to add device");
    }

    err = i2c_master_transmit_receive(device_handle,
                                      write_bytes,
                                      write_length,
                                      read_bytes,
                                      read_length,
                                      (int)slot->timeout_ms);
    i2c_master_bus_rm_device(device_handle);
    heap_caps_free(write_bytes);
    if (err != ESP_OK) {
        heap_caps_free(read_bytes);
        return i2c_throw_error(ctx, err, "I2CBus.writeRead() failed");
    }

    result = js_bytes_to_array(ctx, read_bytes, read_length);
    heap_caps_free(read_bytes);
    return result;
}

static JSValue i2c_open(JSContext *ctx, int argc, JSValue *argv)
{
    gpio_num_t sda_pin = (gpio_num_t)ESP32_MQUICKJS_I2C_DEFAULT_SDA_PIN;
    gpio_num_t scl_pin = (gpio_num_t)ESP32_MQUICKJS_I2C_DEFAULT_SCL_PIN;
    uint32_t freq_hz = ESP32_MQUICKJS_I2C_DEFAULT_FREQ_HZ;
    uint32_t timeout_ms = ESP32_MQUICKJS_I2C_DEFAULT_TIMEOUT_MS;
    bool internal_pullup = ESP32_MQUICKJS_I2C_ENABLE_INTERNAL_PULLUP;
    i2c_master_bus_config_t bus_config = {0};
    esp32_mquickjs_i2c_slot_t *slot;
    JSValue result;
    esp_err_t err;

    if (argc >= 1 && !JS_IsUndefined(argv[0])) {
        JSGCRef property_ref;
        JSValue *property;

        if (JS_GetClassID(ctx, argv[0]) < 0) {
            return JS_ThrowTypeError(ctx,
                                     "i2c.open(options?) expects an object with sda/scl/freqHz/timeoutMs/internalPullup");
        }

        property = JS_PushGCRef(ctx, &property_ref);

        *property = JS_GetPropertyStr(ctx, argv[0], "sda");
        if (JS_IsException(*property)) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_EXCEPTION;
        }
        if (!JS_IsUndefined(*property) && !js_value_to_gpio_num(ctx, *property, &sda_pin)) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_ThrowTypeError(ctx, "i2c.open({ sda }) expects a valid GPIO");
        }

        *property = JS_GetPropertyStr(ctx, argv[0], "scl");
        if (JS_IsException(*property)) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_EXCEPTION;
        }
        if (!JS_IsUndefined(*property) && !js_value_to_gpio_num(ctx, *property, &scl_pin)) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_ThrowTypeError(ctx, "i2c.open({ scl }) expects a valid GPIO");
        }

        *property = JS_GetPropertyStr(ctx, argv[0], "freqHz");
        if (JS_IsException(*property)) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_EXCEPTION;
        }
        if (JS_IsUndefined(*property)) {
            *property = JS_GetPropertyStr(ctx, argv[0], "freq");
            if (JS_IsException(*property)) {
                JS_PopGCRef(ctx, &property_ref);
                return JS_EXCEPTION;
            }
        }
        if (!JS_IsUndefined(*property) &&
            (!js_value_to_u32(ctx, *property, &freq_hz) || freq_hz == 0)) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_ThrowTypeError(ctx, "i2c.open({ freqHz }) expects a positive integer");
        }

        *property = JS_GetPropertyStr(ctx, argv[0], "timeoutMs");
        if (JS_IsException(*property)) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_EXCEPTION;
        }
        if (!JS_IsUndefined(*property) && !js_value_to_u32(ctx, *property, &timeout_ms)) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_ThrowTypeError(ctx, "i2c.open({ timeoutMs }) expects a non-negative integer");
        }

        *property = JS_GetPropertyStr(ctx, argv[0], "internalPullup");
        if (JS_IsException(*property)) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_EXCEPTION;
        }
        if (!JS_IsUndefined(*property) && !js_value_to_bool(ctx, *property, &internal_pullup)) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_ThrowTypeError(ctx, "i2c.open({ internalPullup }) expects a boolean-like value");
        }
        JS_PopGCRef(ctx, &property_ref);
    }

    slot = i2c_alloc_slot();
    if (slot == NULL) {
        return JS_ThrowInternalError(ctx, "i2c.open() failed: no available I2C bus slots");
    }

    bus_config.i2c_port = (i2c_port_num_t)-1;
    bus_config.sda_io_num = sda_pin;
    bus_config.scl_io_num = scl_pin;
    bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_config.glitch_ignore_cnt = 7;
    bus_config.flags.enable_internal_pullup = internal_pullup ? 1U : 0U;

    err = i2c_new_master_bus(&bus_config, &slot->bus_handle);
    if (err != ESP_OK) {
        i2c_cleanup_slot(slot);
        return i2c_throw_error(ctx, err, "i2c.open() failed");
    }

    slot->sda_pin = sda_pin;
    slot->scl_pin = scl_pin;
    slot->freq_hz = freq_hz;
    slot->timeout_ms = timeout_ms;
    slot->internal_pullup = internal_pullup;

    result = i2c_make_bus_object(ctx, slot);
    if (JS_IsException(result)) {
        i2c_cleanup_slot(slot);
    }
    return result;
}

void esp32_mquickjs_init_i2c_runtime(void)
{
    int32_t i;

    for (i = 0; i < (int32_t)SOC_I2C_NUM; ++i) {
        if (s_i2c_slots[i].allocated) {
            i2c_cleanup_slot(&s_i2c_slots[i]);
        }
    }
    i2c_reset_slots();
}

JSValue js_i2c_bus_constructor(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_ThrowTypeError(ctx, "I2CBus cannot be constructed directly");
}

void js_i2c_bus_finalizer(JSContext *ctx, void *opaque)
{
    esp32_mquickjs_i2c_bus_ref_t *bus_ref = opaque;
    esp32_mquickjs_i2c_slot_t *slot;

    (void)ctx;

    if (bus_ref == NULL) {
        return;
    }

    slot = i2c_get_slot(bus_ref);
    if (slot != NULL) {
        i2c_cleanup_slot(slot);
    }
    heap_caps_free(bus_ref);
}

JSValue js_i2c_bus_close(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    esp32_mquickjs_i2c_bus_ref_t bus_ref;
    esp32_mquickjs_i2c_bus_ref_t *bus_ref_ptr;
    esp32_mquickjs_i2c_slot_t *slot;

    (void)argc;
    (void)argv;

    if (i2c_bus_ref_from_object(ctx, *this_val, "I2CBus.close()", &bus_ref) != 0) {
        return JS_EXCEPTION;
    }
    slot = i2c_get_slot(&bus_ref);
    if (slot != NULL) {
        i2c_cleanup_slot(slot);
    }
    bus_ref_ptr = JS_GetOpaque(ctx, *this_val);
    if (bus_ref_ptr != NULL) {
        bus_ref_ptr->bus_id = -1;
        bus_ref_ptr->generation = 0;
    }
    return JS_TRUE;
}

JSValue js_i2c_bus_status(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    esp32_mquickjs_i2c_bus_ref_t bus_ref;
    esp32_mquickjs_i2c_slot_t *slot = NULL;

    (void)argc;
    (void)argv;

    if (i2c_get_this_slot(ctx, *this_val, "I2CBus.status()", &bus_ref, &slot) != 0) {
        return JS_EXCEPTION;
    }
    return i2c_make_status_object(ctx, slot);
}

JSValue js_i2c_bus_scan(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    esp32_mquickjs_i2c_bus_ref_t bus_ref;
    esp32_mquickjs_i2c_slot_t *slot = NULL;

    (void)argc;
    (void)argv;

    if (i2c_get_this_slot(ctx, *this_val, "I2CBus.scan()", &bus_ref, &slot) != 0) {
        return JS_EXCEPTION;
    }
    return i2c_scan(ctx, slot);
}

JSValue js_i2c_bus_write(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    esp32_mquickjs_i2c_bus_ref_t bus_ref;
    esp32_mquickjs_i2c_slot_t *slot = NULL;
    uint16_t address = 0;

    if (i2c_get_this_slot(ctx, *this_val, "I2CBus.write()", &bus_ref, &slot) != 0) {
        return JS_EXCEPTION;
    }
    if (argc < 2 || !js_value_to_i2c_address(ctx, argv[0], &address)) {
        return JS_ThrowTypeError(ctx, "I2CBus.write(addr, data) expects a 7-bit address and byte array");
    }
    return i2c_write(ctx, slot, address, argv[1]);
}

JSValue js_i2c_bus_read(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    esp32_mquickjs_i2c_bus_ref_t bus_ref;
    esp32_mquickjs_i2c_slot_t *slot = NULL;
    uint16_t address = 0;
    uint32_t length = 0;

    if (i2c_get_this_slot(ctx, *this_val, "I2CBus.read()", &bus_ref, &slot) != 0) {
        return JS_EXCEPTION;
    }
    if (argc < 2 || !js_value_to_i2c_address(ctx, argv[0], &address) ||
        !js_value_to_u32(ctx, argv[1], &length)) {
        return JS_ThrowTypeError(ctx, "I2CBus.read(addr, length) expects a 7-bit address and byte length");
    }
    return i2c_read(ctx, slot, address, length);
}

JSValue js_i2c_bus_writeRead(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    esp32_mquickjs_i2c_bus_ref_t bus_ref;
    esp32_mquickjs_i2c_slot_t *slot = NULL;
    uint16_t address = 0;
    uint32_t read_length = 0;

    if (i2c_get_this_slot(ctx, *this_val, "I2CBus.writeRead()", &bus_ref, &slot) != 0) {
        return JS_EXCEPTION;
    }
    if (argc < 3 || !js_value_to_i2c_address(ctx, argv[0], &address) ||
        !js_value_to_u32(ctx, argv[2], &read_length)) {
        return JS_ThrowTypeError(ctx,
                                 "I2CBus.writeRead(addr, writeData, readLength) expects a 7-bit address, write bytes, and read length");
    }
    return i2c_write_read(ctx, slot, address, argv[1], read_length);
}

JSValue js_i2c_open(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    return i2c_open(ctx, argc, argv);
}

JSValue js_i2c_get_default_sda(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_NewInt32(ctx, ESP32_MQUICKJS_I2C_DEFAULT_SDA_PIN);
}

JSValue js_i2c_get_default_scl(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_NewInt32(ctx, ESP32_MQUICKJS_I2C_DEFAULT_SCL_PIN);
}

JSValue js_i2c_get_default_freq_hz(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_NewUint32(ctx, ESP32_MQUICKJS_I2C_DEFAULT_FREQ_HZ);
}

JSValue js_i2c_get_default_timeout_ms(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_NewUint32(ctx, ESP32_MQUICKJS_I2C_DEFAULT_TIMEOUT_MS);
}

#endif
