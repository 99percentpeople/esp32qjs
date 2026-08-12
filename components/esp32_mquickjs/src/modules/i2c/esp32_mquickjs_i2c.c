#include "esp32_mquickjs_i2c.h"

#if CONFIG_ESP32_MQUICKJS_FEATURE_I2C

#include "utils/esp32_mquickjs_byte_source.h"
#include "esp32_mquickjs_core.h"
#include "esp32_mquickjs_future.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
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
    bool busy;
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

    if (!esp32_mquickjs_set_property_ref(ctx, status_obj, "opened",
                                     JS_NewBool(slot != NULL && slot->allocated)) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "sda",
                                     JS_NewInt32(ctx, slot != NULL ? (int32_t)slot->sda_pin : ESP32_MQUICKJS_I2C_DEFAULT_SDA_PIN)) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "scl",
                                     JS_NewInt32(ctx, slot != NULL ? (int32_t)slot->scl_pin : ESP32_MQUICKJS_I2C_DEFAULT_SCL_PIN)) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "freqHz",
                                     JS_NewUint32(ctx, slot != NULL ? slot->freq_hz : ESP32_MQUICKJS_I2C_DEFAULT_FREQ_HZ)) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "timeoutMs",
                                     JS_NewUint32(ctx, slot != NULL ? slot->timeout_ms : ESP32_MQUICKJS_I2C_DEFAULT_TIMEOUT_MS)) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "internalPullup",
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

static JSValue i2c_make_write_chunks_stats(JSContext *ctx,
                                           uint32_t chunks,
                                           size_t bytes,
                                           uint64_t total_us)
{
    JSGCRef stats_ref;
    JSValue *stats;

    stats = JS_PushGCRef(ctx, &stats_ref);
    *stats = JS_NewObject(ctx);
    if (JS_IsException(*stats)) {
        JS_PopGCRef(ctx, &stats_ref);
        return JS_EXCEPTION;
    }

    if (!esp32_mquickjs_set_property_ref(ctx, stats, "chunks", JS_NewUint32(ctx, chunks)) ||
        !esp32_mquickjs_set_property_ref(ctx, stats, "bytes", JS_NewInt64(ctx, (int64_t)bytes)) ||
        !esp32_mquickjs_set_property_ref(ctx, stats, "totalUs", JS_NewInt64(ctx, (int64_t)total_us))) {
        JS_PopGCRef(ctx, &stats_ref);
        return JS_EXCEPTION;
    }

    return JS_PopGCRef(ctx, &stats_ref);
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

void esp32_mquickjs_deinit_i2c_runtime(void)
{
    int32_t i;

    for (i = 0; i < (int32_t)SOC_I2C_NUM; ++i) {
        if (s_i2c_slots[i].allocated) {
            i2c_cleanup_slot(&s_i2c_slots[i]);
        }
    }
    i2c_reset_slots();
}

static bool i2c_register_future_drivers(JSContext *ctx,
                                        esp32_mquickjs_runtime_t *runtime);

bool esp32_mquickjs_init_i2c_runtime(JSContext *ctx,
                                     esp32_mquickjs_runtime_t *runtime)
{
    esp32_mquickjs_deinit_i2c_runtime();
    return i2c_register_future_drivers(ctx, runtime);
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
        if (slot->busy) {
            return JS_ThrowInternalError(ctx,
                                         "I2CBus.close() refused while an operation is pending");
        }
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

typedef enum {
    I2C_FUTURE_SCAN,
    I2C_FUTURE_WRITE,
    I2C_FUTURE_WRITE_CHUNKS,
    I2C_FUTURE_READ,
    I2C_FUTURE_WRITE_READ,
} i2c_future_kind_t;

struct esp32_mquickjs_future_driver_state {
    i2c_future_kind_t kind;
    JSContext *ctx;
    JSGCRef owner_ref;
    esp32_mquickjs_i2c_bus_ref_t bus_ref;
    esp32_mquickjs_runtime_t *runtime;
    esp32_mquickjs_future_token_t token;
    uint16_t address;
    uint32_t timeout_ms;
    uint32_t chunk_count;
    uint32_t completed_chunks;
    uint32_t scan_count;
    uint32_t *chunk_lengths;
    uint8_t scan_addresses[0x78 - 0x03];
    uint8_t *write_data;
    size_t write_length;
    uint8_t *read_data;
    size_t read_length;
    uint64_t total_us;
    esp_err_t err;
    volatile bool completed;
    bool owner_retained;
    bool started;
    bool cancelled;
};

static void i2c_future_release_prepare_state(
    esp32_mquickjs_future_driver_state_t *state)
{
    if (state == NULL) {
        return;
    }
    if (state->owner_retained) {
        JS_DeleteGCRef(state->ctx, &state->owner_ref);
    }
    heap_caps_free(state->chunk_lengths);
    heap_caps_free(state->write_data);
    heap_caps_free(state->read_data);
    heap_caps_free(state);
}

static esp32_mquickjs_future_driver_state_t *i2c_future_allocate(
    JSContext *ctx,
    JSValue this_value,
    i2c_future_kind_t kind)
{
    esp32_mquickjs_future_driver_state_t *state;
    esp32_mquickjs_i2c_slot_t *slot = NULL;
    JSValue *owner;

    state = heap_caps_calloc(1, sizeof(*state), MALLOC_CAP_8BIT);
    if (state == NULL) {
        JS_ThrowOutOfMemory(ctx);
        return NULL;
    }
    if (i2c_get_this_slot(ctx,
                          this_value,
                          "I2CBus Future operation",
                          &state->bus_ref,
                          &slot) != 0) {
        heap_caps_free(state);
        return NULL;
    }
    if (slot->busy) {
        heap_caps_free(state);
        JS_ThrowInternalError(ctx, "I2C bus is busy");
        return NULL;
    }
    state->kind = kind;
    state->ctx = ctx;
    state->timeout_ms = slot->timeout_ms;
    owner = JS_AddGCRef(ctx, &state->owner_ref);
    *owner = this_value;
    state->owner_retained = true;
    return state;
}

static bool i2c_future_copy_bytes(JSContext *ctx,
                                  JSValue value,
                                  const char *api_name,
                                  uint8_t **out_data,
                                  size_t *out_length)
{
    esp32_mquickjs_byte_source_t source;
    uint8_t *owned = NULL;
    JSValue error = JS_UNDEFINED;
    uint8_t *copy = NULL;

    if (!esp32_mquickjs_get_byte_source(ctx,
                                       value,
                                       api_name,
                                       &source,
                                       &owned,
                                       &error)) {
        if (JS_IsUndefined(error)) {
            JS_ThrowTypeError(ctx, "%s expects byte data", api_name);
        } else if (!JS_IsException(error)) {
            (void)JS_Throw(ctx, error);
        }
        return false;
    }
    if (source.length > 0) {
        copy = heap_caps_malloc(source.length, MALLOC_CAP_8BIT);
        if (copy == NULL) {
            esp32_mquickjs_release_byte_source(owned);
            JS_ThrowOutOfMemory(ctx);
            return false;
        }
        memcpy(copy, source.data, source.length);
    }
    *out_data = copy;
    *out_length = source.length;
    esp32_mquickjs_release_byte_source(owned);
    return true;
}

static bool i2c_scan_future_prepare(
    JSContext *ctx,
    JSGCRef *this_ref,
    int argc,
    JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **out_state)
{
    (void)argv;
    if (out_state == NULL || argc != 0) {
        JS_ThrowTypeError(ctx, "I2CBus.scan() expects no arguments");
        return false;
    }
    *out_state = i2c_future_allocate(ctx, this_ref->val, I2C_FUTURE_SCAN);
    return *out_state != NULL;
}

static bool i2c_write_future_prepare(
    JSContext *ctx,
    JSGCRef *this_ref,
    int argc,
    JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **out_state)
{
    esp32_mquickjs_future_driver_state_t *state;

    if (out_state == NULL || argc != 2 ||
        !js_value_to_i2c_address(ctx, argv[0].val, &(uint16_t){0})) {
        JS_ThrowTypeError(ctx,
                          "I2CBus.write(addr, data) expects a 7-bit address and byte data");
        return false;
    }
    state = i2c_future_allocate(ctx, this_ref->val, I2C_FUTURE_WRITE);
    if (state == NULL) {
        return false;
    }
    (void)js_value_to_i2c_address(ctx, argv[0].val, &state->address);
    if (!i2c_future_copy_bytes(ctx, argv[1].val, "I2CBus.write(addr, data)",
                               &state->write_data, &state->write_length)) {
        i2c_future_release_prepare_state(state);
        return false;
    }
    *out_state = state;
    return true;
}

static bool i2c_write_chunks_future_prepare(
    JSContext *ctx,
    JSGCRef *this_ref,
    int argc,
    JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **out_state)
{
    esp32_mquickjs_future_driver_state_t *state;
    JSValue error = JS_UNDEFINED;
    uint32_t index;
    size_t total = 0;

    if (out_state == NULL || argc != 2 ||
        !js_value_to_i2c_address(ctx, argv[0].val, &(uint16_t){0})) {
        JS_ThrowTypeError(ctx,
                          "I2CBus.writeChunks(addr, chunks) expects a 7-bit address and byte-source chunks");
        return false;
    }
    state = i2c_future_allocate(ctx, this_ref->val, I2C_FUTURE_WRITE_CHUNKS);
    if (state == NULL) {
        return false;
    }
    (void)js_value_to_i2c_address(ctx, argv[0].val, &state->address);
    if (!esp32_mquickjs_get_byte_source_array_length(
            ctx, argv[1].val, "I2CBus.writeChunks(addr, chunks)",
            &state->chunk_count, &error)) {
        if (JS_IsUndefined(error)) {
            JS_ThrowTypeError(ctx,
                              "I2CBus.writeChunks(addr, chunks) expects an array-like object");
        }
        i2c_future_release_prepare_state(state);
        return false;
    }
    if (state->chunk_count > 0) {
        state->chunk_lengths = heap_caps_calloc(state->chunk_count,
                                                sizeof(*state->chunk_lengths),
                                                MALLOC_CAP_8BIT);
        if (state->chunk_lengths == NULL) {
            i2c_future_release_prepare_state(state);
            JS_ThrowOutOfMemory(ctx);
            return false;
        }
    }
    for (index = 0; index < state->chunk_count; ++index) {
        esp32_mquickjs_byte_source_chunk_t chunk;
        uint8_t *grown;

        if (!esp32_mquickjs_get_byte_source_chunk(
                ctx, argv[1].val, index, "I2CBus.writeChunks(addr, chunks)",
                &chunk, &error)) {
            i2c_future_release_prepare_state(state);
            return false;
        }
        if (chunk.source.length > SIZE_MAX - total) {
            esp32_mquickjs_release_byte_source_chunk(ctx, &chunk);
            i2c_future_release_prepare_state(state);
            JS_ThrowRangeError(ctx, "I2CBus.writeChunks() byte length overflow");
            return false;
        }
        state->chunk_lengths[index] = (uint32_t)chunk.source.length;
        if (chunk.source.length > 0) {
            grown = heap_caps_realloc(state->write_data,
                                      total + chunk.source.length,
                                      MALLOC_CAP_8BIT);
            if (grown == NULL) {
                esp32_mquickjs_release_byte_source_chunk(ctx, &chunk);
                i2c_future_release_prepare_state(state);
                JS_ThrowOutOfMemory(ctx);
                return false;
            }
            state->write_data = grown;
            memcpy(state->write_data + total,
                   chunk.source.data,
                   chunk.source.length);
            total += chunk.source.length;
        }
        esp32_mquickjs_release_byte_source_chunk(ctx, &chunk);
    }
    state->write_length = total;
    *out_state = state;
    return true;
}

static bool i2c_read_future_prepare(
    JSContext *ctx,
    JSGCRef *this_ref,
    int argc,
    JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **out_state)
{
    esp32_mquickjs_future_driver_state_t *state;
    uint32_t read_length;

    if (out_state == NULL || argc != 2 ||
        !js_value_to_i2c_address(ctx, argv[0].val, &(uint16_t){0}) ||
        !js_value_to_u32(ctx, argv[1].val, &read_length)) {
        JS_ThrowTypeError(ctx,
                          "I2CBus.read(addr, length) expects a 7-bit address and byte length");
        return false;
    }
    state = i2c_future_allocate(ctx, this_ref->val, I2C_FUTURE_READ);
    if (state == NULL) {
        return false;
    }
    (void)js_value_to_i2c_address(ctx, argv[0].val, &state->address);
    state->read_length = read_length;
    if (read_length > 0) {
        state->read_data = heap_caps_malloc(read_length, MALLOC_CAP_8BIT);
        if (state->read_data == NULL) {
            i2c_future_release_prepare_state(state);
            JS_ThrowOutOfMemory(ctx);
            return false;
        }
    }
    *out_state = state;
    return true;
}

static bool i2c_write_read_future_prepare(
    JSContext *ctx,
    JSGCRef *this_ref,
    int argc,
    JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **out_state)
{
    esp32_mquickjs_future_driver_state_t *state;
    uint32_t read_length;

    if (out_state == NULL || argc != 3 ||
        !js_value_to_i2c_address(ctx, argv[0].val, &(uint16_t){0}) ||
        !js_value_to_u32(ctx, argv[2].val, &read_length)) {
        JS_ThrowTypeError(ctx,
                          "I2CBus.writeRead(addr, writeData, readLength) expects a 7-bit address, write bytes, and read length");
        return false;
    }
    state = i2c_future_allocate(ctx, this_ref->val, I2C_FUTURE_WRITE_READ);
    if (state == NULL) {
        return false;
    }
    (void)js_value_to_i2c_address(ctx, argv[0].val, &state->address);
    if (!i2c_future_copy_bytes(ctx, argv[1].val,
                               "I2CBus.writeRead(addr, writeData, readLength)",
                               &state->write_data, &state->write_length)) {
        i2c_future_release_prepare_state(state);
        return false;
    }
    state->read_length = read_length;
    if (read_length > 0) {
        state->read_data = heap_caps_malloc(read_length, MALLOC_CAP_8BIT);
        if (state->read_data == NULL) {
            i2c_future_release_prepare_state(state);
            JS_ThrowOutOfMemory(ctx);
            return false;
        }
    }
    *out_state = state;
    return true;
}

static void i2c_future_worker(void *opaque)
{
    esp32_mquickjs_future_driver_state_t *state = opaque;
    esp32_mquickjs_i2c_slot_t *slot = i2c_get_slot(&state->bus_ref);
    i2c_master_dev_handle_t device = NULL;
    int64_t started_us = esp_timer_get_time();

    if (slot == NULL) {
        state->err = ESP_ERR_INVALID_STATE;
        state->completed = true;
        return;
    }
    if (state->kind == I2C_FUTURE_SCAN) {
        uint16_t address;

        for (address = 0x03; address <= 0x77; ++address) {
            esp_err_t err = i2c_master_probe(slot->bus_handle,
                                             address,
                                             (int)state->timeout_ms);
            if (err == ESP_OK) {
                state->scan_addresses[state->scan_count++] = (uint8_t)address;
            } else if (err != ESP_ERR_NOT_FOUND && err != ESP_ERR_INVALID_RESPONSE) {
                state->err = err;
                break;
            }
        }
        state->total_us = (uint64_t)(esp_timer_get_time() - started_us);
        state->completed = true;
        return;
    }
    state->err = i2c_with_device(slot, state->address, &device);
    if (state->err != ESP_OK) {
        state->completed = true;
        return;
    }
    if (state->kind == I2C_FUTURE_WRITE) {
        state->err = i2c_master_transmit(device,
                                         state->write_data,
                                         state->write_length,
                                         (int)state->timeout_ms);
    } else if (state->kind == I2C_FUTURE_WRITE_CHUNKS) {
        uint32_t index;
        size_t offset = 0;

        for (index = 0; index < state->chunk_count; ++index) {
            uint32_t length = state->chunk_lengths[index];

            if (length == 0) {
                continue;
            }
            state->err = i2c_master_transmit(device,
                                             state->write_data + offset,
                                             length,
                                             (int)state->timeout_ms);
            if (state->err != ESP_OK) {
                break;
            }
            state->completed_chunks++;
            offset += length;
        }
    } else if (state->kind == I2C_FUTURE_READ) {
        state->err = i2c_master_receive(device,
                                        state->read_data,
                                        state->read_length,
                                        (int)state->timeout_ms);
    } else {
        state->err = i2c_master_transmit_receive(device,
                                                 state->write_data,
                                                 state->write_length,
                                                 state->read_data,
                                                 state->read_length,
                                                 (int)state->timeout_ms);
    }
    (void)i2c_master_bus_rm_device(device);
    state->total_us = (uint64_t)(esp_timer_get_time() - started_us);
    state->completed = true;
}

static bool i2c_future_start(JSContext *ctx,
                             esp32_mquickjs_runtime_t *runtime,
                             esp32_mquickjs_future_token_t token,
                             esp32_mquickjs_future_driver_state_t *state)
{
    esp32_mquickjs_i2c_slot_t *slot = state != NULL
        ? i2c_get_slot(&state->bus_ref) : NULL;

    if (state == NULL || slot == NULL) {
        JS_ThrowReferenceError(ctx, "I2C bus closed before operation start");
        return false;
    }
    if (slot->busy) {
        JS_ThrowInternalError(ctx, "I2C bus is busy");
        return false;
    }
    slot->busy = true;
    state->runtime = runtime;
    state->token = token;
    state->started = true;
    if (!esp32_mquickjs_future_submit_worker(runtime, token,
                                             i2c_future_worker, state)) {
        JS_ThrowInternalError(ctx, "I2C Future worker queue is busy");
        return false;
    }
    return true;
}

static esp32_mquickjs_future_poll_t i2c_future_poll(
    esp32_mquickjs_future_driver_state_t *state)
{
    return state != NULL && state->completed
        ? ESP32_MQUICKJS_FUTURE_READY
        : ESP32_MQUICKJS_FUTURE_PENDING;
}

static JSValue i2c_future_finish(JSContext *ctx,
                                 esp32_mquickjs_future_driver_state_t *state)
{
    if (state == NULL || state->cancelled) {
        return JS_ThrowInternalError(ctx, "I2C operation cancelled");
    }
    if (state->err != ESP_OK) {
        return i2c_throw_error(ctx, state->err, "I2CBus operation failed");
    }
    if (state->kind == I2C_FUTURE_SCAN) {
        JSGCRef array_ref;
        JSValue *array = JS_PushGCRef(ctx, &array_ref);
        uint32_t index;

        *array = JS_NewArray(ctx, state->scan_count);
        for (index = 0; !JS_IsException(*array) && index < state->scan_count; ++index) {
            if (JS_IsException(JS_SetPropertyUint32(
                    ctx, *array, index, JS_NewInt32(ctx, state->scan_addresses[index])))) {
                JS_PopGCRef(ctx, &array_ref);
                return JS_EXCEPTION;
            }
        }
        return JS_PopGCRef(ctx, &array_ref);
    }
    if (state->kind == I2C_FUTURE_WRITE) {
        return JS_NewInt32(ctx, (int32_t)state->write_length);
    }
    if (state->kind == I2C_FUTURE_WRITE_CHUNKS) {
        return i2c_make_write_chunks_stats(ctx,
                                           state->completed_chunks,
                                           state->write_length,
                                           state->total_us);
    }
    return js_bytes_to_array(ctx, state->read_data, state->read_length);
}

static bool i2c_future_cancel(esp32_mquickjs_future_driver_state_t *state)
{
    if (state == NULL || state->completed || state->cancelled) {
        return false;
    }
    state->cancelled = true;
    return true;
}

static void i2c_future_destroy(esp32_mquickjs_future_driver_state_t *state)
{
    esp32_mquickjs_i2c_slot_t *slot;

    if (state == NULL) {
        return;
    }
    slot = i2c_get_slot(&state->bus_ref);
    if (slot != NULL && state->started) {
        slot->busy = false;
    }
    i2c_future_release_prepare_state(state);
}

static uint32_t i2c_future_timeout_ms(
    const esp32_mquickjs_future_driver_state_t *state)
{
    return state != NULL ? state->timeout_ms : 0;
}

#define I2C_FUTURE_DRIVER(name, prepare_fn) \
    static const esp32_mquickjs_future_driver_t name = { \
        .prepare = prepare_fn, \
        .start = i2c_future_start, \
        .poll = i2c_future_poll, \
        .finish = i2c_future_finish, \
        .cancel = i2c_future_cancel, \
        .destroy = i2c_future_destroy, \
        .timeout_ms = i2c_future_timeout_ms, \
    }

I2C_FUTURE_DRIVER(s_i2c_scan_driver, i2c_scan_future_prepare);
I2C_FUTURE_DRIVER(s_i2c_write_driver, i2c_write_future_prepare);
I2C_FUTURE_DRIVER(s_i2c_write_chunks_driver, i2c_write_chunks_future_prepare);
I2C_FUTURE_DRIVER(s_i2c_read_driver, i2c_read_future_prepare);
I2C_FUTURE_DRIVER(s_i2c_write_read_driver, i2c_write_read_future_prepare);

#undef I2C_FUTURE_DRIVER

static JSValue i2c_future_call_and_wait(JSContext *ctx,
                                        JSValue receiver,
                                        const char *method_name,
                                        int argc,
                                        JSValue *argv)
{
    JSGCRef receiver_ref;
    JSGCRef method_ref;
    JSValue *rooted_receiver = JS_PushGCRef(ctx, &receiver_ref);
    JSValue *method = JS_PushGCRef(ctx, &method_ref);
    JSValue result;

    *rooted_receiver = receiver;
    *method = JS_GetPropertyStr(ctx, *rooted_receiver, method_name);
    result = JS_IsException(*method)
        ? JS_EXCEPTION
        : esp32_mquickjs_future_call_and_wait(ctx,
                                              esp32_mquickjs_get_active_runtime(),
                                              *method,
                                              *rooted_receiver,
                                              argc,
                                              argv);
    JS_PopGCRef(ctx, &method_ref);
    JS_PopGCRef(ctx, &receiver_ref);
    return result;
}

static bool i2c_register_future_drivers(JSContext *ctx,
                                        esp32_mquickjs_runtime_t *runtime)
{
    static const char *names[] = { "scan", "write", "writeChunks", "read", "writeRead" };
    static const esp32_mquickjs_future_driver_t *drivers[] = {
        &s_i2c_scan_driver,
        &s_i2c_write_driver,
        &s_i2c_write_chunks_driver,
        &s_i2c_read_driver,
        &s_i2c_write_read_driver,
    };
    JSGCRef object_ref;
    JSValue *object = JS_PushGCRef(ctx, &object_ref);
    size_t index;
    bool result = true;

    *object = JS_NewObjectClassUser(ctx, JS_CLASS_I2C_BUS);
    for (index = 0; result && index < sizeof(names) / sizeof(names[0]); ++index) {
        JSGCRef method_ref;
        JSValue *method = JS_PushGCRef(ctx, &method_ref);

        *method = JS_IsException(*object)
            ? JS_EXCEPTION
            : JS_GetPropertyStr(ctx, *object, names[index]);
        result = !JS_IsException(*method) &&
                 esp32_mquickjs_future_register_driver(ctx, runtime,
                                                       *method, drivers[index]);
        JS_PopGCRef(ctx, &method_ref);
    }
    if (!result && !JS_IsException(*object)) {
        JS_ThrowInternalError(ctx, "failed to register I2C Future drivers");
    }
    JS_PopGCRef(ctx, &object_ref);
    return result;
}

JSValue js_i2c_bus_scan(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    return i2c_future_call_and_wait(ctx, *this_val, "scan", argc, argv);
}

JSValue js_i2c_bus_write(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    return i2c_future_call_and_wait(ctx, *this_val, "write", argc, argv);
}

JSValue js_i2c_bus_write_chunks(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    return i2c_future_call_and_wait(ctx, *this_val, "writeChunks", argc, argv);
}

JSValue js_i2c_bus_read(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    return i2c_future_call_and_wait(ctx, *this_val, "read", argc, argv);
}

JSValue js_i2c_bus_writeRead(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    return i2c_future_call_and_wait(ctx, *this_val, "writeRead", argc, argv);
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
