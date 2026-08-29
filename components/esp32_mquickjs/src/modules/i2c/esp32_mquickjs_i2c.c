#include "esp32_mquickjs_i2c.h"

#if CONFIG_ESP32_MQUICKJS_FEATURE_I2C

#include "utils/esp32_mquickjs_byte_source.h"
#include "esp32_mquickjs_core.h"
#include "esp32_mquickjs_future.h"
#include "esp32_mquickjs_i2c_bus_resources.h"
#include "esp32_mquickjs_peripheral_lease.h"

#include <stdbool.h>
#include <limits.h>
#include <stdint.h>
#include <stdatomic.h>
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
    int32_t bus_id;
    uint32_t bus_generation;
    uint32_t device_generation;
} esp32_mquickjs_i2c_device_ref_t;

typedef struct esp32_mquickjs_i2c_device_slot {
    uint32_t generation;
    uint16_t address;
    uint32_t freq_hz;
    uint32_t timeout_ms;
    uint16_t future_reservations;
    bool release_pending;
    i2c_master_dev_handle_t handle;
    struct esp32_mquickjs_i2c_device_slot *next;
} esp32_mquickjs_i2c_device_slot_t;

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
    bool release_pending;
    uint16_t future_reservations;
    i2c_master_bus_handle_t bus_handle;
    esp32_mquickjs_i2c_device_slot_t *devices;
    esp32_mquickjs_peripheral_lease_t lease;
} esp32_mquickjs_i2c_slot_t;

static esp32_mquickjs_i2c_slot_t s_i2c_slots[SOC_I2C_NUM];
static uint32_t s_i2c_next_generation = 1;
static uint32_t s_i2c_next_device_generation = 1;

static esp_err_t i2c_cleanup_slot(esp32_mquickjs_i2c_slot_t *slot);
static esp_err_t i2c_cleanup_device(esp32_mquickjs_i2c_slot_t *bus,
                                    esp32_mquickjs_i2c_device_slot_t *device);

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
        if (!s_i2c_slots[i].allocated) {
            i2c_init_slot(&s_i2c_slots[i], i);
        }
    }
}

static uint32_t i2c_take_generation(void)
{
    uint32_t generation = s_i2c_next_generation++;

    if (generation == 0) {
        generation = s_i2c_next_generation++;
    }
    return generation;
}

static uint32_t i2c_take_device_generation(void)
{
    uint32_t generation = s_i2c_next_device_generation++;

    if (generation == 0) {
        generation = s_i2c_next_device_generation++;
    }
    return generation;
}

typedef struct {
    esp32_mquickjs_i2c_slot_t *slot;
    i2c_master_bus_config_t config;
} i2c_bus_resource_context_t;

static bool i2c_bus_resource_acquire_lease(void *opaque)
{
    i2c_bus_resource_context_t *context = opaque;

    return context != NULL && context->slot != NULL &&
           esp32_mquickjs_peripheral_lease_acquire(
               ESP32_MQUICKJS_PERIPHERAL_I2C_PORT,
               context->slot->bus_id,
               ESP32_MQUICKJS_PERIPHERAL_OWNER_I2C,
               &context->slot->lease);
}

static void i2c_bus_resource_release_lease(void *opaque)
{
    i2c_bus_resource_context_t *context = opaque;

    if (context != NULL && context->slot != NULL) {
        esp32_mquickjs_peripheral_lease_release(&context->slot->lease);
    }
}

static int i2c_bus_resource_create(void *opaque, void **out_bus)
{
    i2c_bus_resource_context_t *context = opaque;
    i2c_master_bus_handle_t bus = NULL;
    esp_err_t err;

    if (context == NULL || context->slot == NULL || out_bus == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    context->config.i2c_port = (i2c_port_num_t)context->slot->bus_id;
    err = i2c_new_master_bus(&context->config, &bus);
    *out_bus = bus;
    return err;
}

static int i2c_bus_resource_delete(void *bus, void *opaque)
{
    (void)opaque;
    return i2c_del_master_bus((i2c_master_bus_handle_t)bus);
}

static esp32_mquickjs_i2c_bus_resource_ops_t i2c_bus_resource_ops(
    i2c_bus_resource_context_t *context)
{
    return (esp32_mquickjs_i2c_bus_resource_ops_t){
        .acquire_lease = i2c_bus_resource_acquire_lease,
        .release_lease = i2c_bus_resource_release_lease,
        .create_bus = i2c_bus_resource_create,
        .delete_bus = i2c_bus_resource_delete,
        .opaque = context,
    };
}

static esp32_mquickjs_i2c_slot_t *i2c_alloc_slot(
    const i2c_master_bus_config_t *config,
    int *out_resource_result)
{
    int32_t i;

    if (out_resource_result != NULL) {
        *out_resource_result = ESP32_MQUICKJS_I2C_BUS_RESOURCE_UNAVAILABLE;
    }
    if (config == NULL) {
        if (out_resource_result != NULL) {
            *out_resource_result = ESP_ERR_INVALID_ARG;
        }
        return NULL;
    }
    for (i = 0; i < (int32_t)SOC_I2C_NUM; ++i) {
        esp32_mquickjs_i2c_slot_t *slot = &s_i2c_slots[i];
        i2c_bus_resource_context_t context;
        esp32_mquickjs_i2c_bus_resources_t resources = {0};
        esp32_mquickjs_i2c_bus_resource_ops_t ops;
        int result;

        if (slot->allocated && slot->release_pending && !slot->busy) {
            (void)i2c_cleanup_slot(slot);
        }
        if (slot->allocated) {
            continue;
        }
        i2c_init_slot(slot, i);
        context = (i2c_bus_resource_context_t){
            .slot = slot,
            .config = *config,
        };
        ops = i2c_bus_resource_ops(&context);
        result = esp32_mquickjs_i2c_bus_resources_init(&resources, &ops);
        if (result == ESP32_MQUICKJS_I2C_BUS_RESOURCE_UNAVAILABLE) {
            continue;
        }
        if (result != 0) {
            if (resources.bus != NULL || resources.lease_acquired) {
                slot->bus_handle =
                    (i2c_master_bus_handle_t)resources.bus;
                slot->allocated = true;
                slot->release_pending = true;
            }
            if (out_resource_result != NULL) {
                *out_resource_result = result;
            }
            return NULL;
        }
        slot->bus_handle = (i2c_master_bus_handle_t)resources.bus;
        slot->allocated = true;
        slot->generation = i2c_take_generation();
        if (out_resource_result != NULL) {
            *out_resource_result = 0;
        }
        return slot;
    }
    return NULL;
}

static esp_err_t i2c_cleanup_slot(esp32_mquickjs_i2c_slot_t *slot)
{
    i2c_bus_resource_context_t context;
    esp32_mquickjs_i2c_bus_resources_t resources;
    esp32_mquickjs_i2c_bus_resource_ops_t ops;
    int32_t bus_id;
    esp32_mquickjs_i2c_device_slot_t *device;
    esp_err_t err;

    if (slot == NULL || !slot->allocated) {
        return ESP_OK;
    }
    if (slot->busy || slot->future_reservations > 0) {
        return ESP_ERR_INVALID_STATE;
    }

    bus_id = slot->bus_id;
    for (device = slot->devices; device != NULL; device = device->next) {
        if (device->future_reservations > 0) {
            return ESP_ERR_INVALID_STATE;
        }
    }
    while (slot->devices != NULL) {
        err = i2c_cleanup_device(slot, slot->devices);
        if (err != ESP_OK) {
            return err;
        }
    }
    context = (i2c_bus_resource_context_t){.slot = slot};
    resources = (esp32_mquickjs_i2c_bus_resources_t){
        .bus = slot->bus_handle,
        .lease_acquired =
            esp32_mquickjs_peripheral_lease_is_held(&slot->lease),
    };
    ops = i2c_bus_resource_ops(&context);
    err = esp32_mquickjs_i2c_bus_resources_deinit(&resources, &ops);
    slot->bus_handle = (i2c_master_bus_handle_t)resources.bus;
    if (err != ESP_OK) {
        return err;
    }
    i2c_init_slot(slot, bus_id);
    return ESP_OK;
}

static esp_err_t i2c_cleanup_device(esp32_mquickjs_i2c_slot_t *bus,
                                    esp32_mquickjs_i2c_device_slot_t *device)
{
    esp32_mquickjs_i2c_device_slot_t **link;
    esp_err_t err;

    if (bus == NULL || device == NULL) {
        return ESP_OK;
    }
    if (device->future_reservations > 0 || bus->busy) {
        return ESP_ERR_INVALID_STATE;
    }
    if (device->handle != NULL) {
        err = i2c_master_bus_rm_device(device->handle);
        if (err != ESP_OK) {
            return err;
        }
        device->handle = NULL;
    }
    for (link = &bus->devices; *link != NULL; link = &(*link)->next) {
        if (*link == device) {
            *link = device->next;
            heap_caps_free(device);
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
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
    const esp32_mquickjs_i2c_device_slot_t *device;
    uint32_t device_count = 0;

    if (slot != NULL) {
        for (device = slot->devices; device != NULL; device = device->next) {
            device_count++;
        }
    }

    status_obj = JS_PushGCRef(ctx, &status_ref);
    *status_obj = JS_NewObject(ctx);
    if (JS_IsException(*status_obj)) {
        JS_PopGCRef(ctx, &status_ref);
        return JS_EXCEPTION;
    }

    if (!esp32_mquickjs_set_property_ref(ctx, status_obj, "opened",
                                     JS_NewBool(slot != NULL && slot->allocated)) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "controller",
                                     JS_NewInt32(ctx, slot != NULL ? slot->bus_id : -1)) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "sda",
                                     JS_NewInt32(ctx, slot != NULL ? (int32_t)slot->sda_pin : ESP32_MQUICKJS_I2C_DEFAULT_SDA_PIN)) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "scl",
                                     JS_NewInt32(ctx, slot != NULL ? (int32_t)slot->scl_pin : ESP32_MQUICKJS_I2C_DEFAULT_SCL_PIN)) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "freqHz",
                                     JS_NewUint32(ctx, slot != NULL ? slot->freq_hz : ESP32_MQUICKJS_I2C_DEFAULT_FREQ_HZ)) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "timeoutMs",
                                     JS_NewUint32(ctx, slot != NULL ? slot->timeout_ms : ESP32_MQUICKJS_I2C_DEFAULT_TIMEOUT_MS)) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "internalPullup",
                                     JS_NewBool(slot != NULL ? slot->internal_pullup : ESP32_MQUICKJS_I2C_ENABLE_INTERNAL_PULLUP)) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "deviceCount",
                                     JS_NewUint32(ctx, device_count))) {
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

static esp32_mquickjs_i2c_device_slot_t *i2c_get_device(
    const esp32_mquickjs_i2c_device_ref_t *ref,
    esp32_mquickjs_i2c_slot_t **out_bus)
{
    esp32_mquickjs_i2c_bus_ref_t bus_ref;
    esp32_mquickjs_i2c_slot_t *bus;
    esp32_mquickjs_i2c_device_slot_t *device;

    if (ref == NULL) {
        return NULL;
    }
    bus_ref.bus_id = ref->bus_id;
    bus_ref.generation = ref->bus_generation;
    bus = i2c_get_slot(&bus_ref);
    if (bus == NULL) {
        return NULL;
    }
    for (device = bus->devices; device != NULL; device = device->next) {
        if (device->generation == ref->device_generation) {
            if (out_bus != NULL) {
                *out_bus = bus;
            }
            return device;
        }
    }
    return NULL;
}

static JSValue i2c_make_device_object(
    JSContext *ctx,
    const esp32_mquickjs_i2c_slot_t *bus,
    const esp32_mquickjs_i2c_device_slot_t *device)
{
    JSGCRef object_ref;
    JSValue *object = JS_PushGCRef(ctx, &object_ref);
    esp32_mquickjs_i2c_device_ref_t *ref;

    *object = JS_NewObjectClassUser(ctx, JS_CLASS_I2C_DEVICE);
    if (JS_IsException(*object) || bus == NULL || device == NULL) {
        JS_PopGCRef(ctx, &object_ref);
        return JS_EXCEPTION;
    }
    ref = heap_caps_malloc(sizeof(*ref), MALLOC_CAP_8BIT);
    if (ref == NULL) {
        JS_PopGCRef(ctx, &object_ref);
        return JS_ThrowOutOfMemory(ctx);
    }
    ref->bus_id = bus->bus_id;
    ref->bus_generation = bus->generation;
    ref->device_generation = device->generation;
    JS_SetOpaque(ctx, *object, ref);
    return JS_PopGCRef(ctx, &object_ref);
}

static int i2c_device_ref_from_object(
    JSContext *ctx,
    JSValue value,
    const char *api_name,
    esp32_mquickjs_i2c_device_ref_t *out_ref)
{
    const esp32_mquickjs_i2c_device_ref_t *ref;

    if (out_ref == NULL ||
        JS_GetClassID(ctx, value) != JS_CLASS_I2C_DEVICE) {
        JS_ThrowTypeError(ctx, "%s expects a valid I2CDevice instance",
                          api_name);
        return -1;
    }
    ref = JS_GetOpaque(ctx, value);
    if (ref == NULL) {
        JS_ThrowTypeError(ctx, "%s expects a valid I2CDevice instance",
                          api_name);
        return -1;
    }
    *out_ref = *ref;
    return 0;
}

static int i2c_get_bound_device(
    JSContext *ctx,
    JSValue value,
    const char *api_name,
    esp32_mquickjs_i2c_device_ref_t *out_ref,
    esp32_mquickjs_i2c_slot_t **out_bus,
    esp32_mquickjs_i2c_device_slot_t **out_device)
{
    esp32_mquickjs_i2c_slot_t *bus = NULL;
    esp32_mquickjs_i2c_device_slot_t *device;

    if (i2c_device_ref_from_object(ctx, value, api_name, out_ref) != 0) {
        return -1;
    }
    device = i2c_get_device(out_ref, &bus);
    if (device == NULL) {
        JS_ThrowReferenceError(ctx,
                               "%s failed because the I2C device is closed",
                               api_name);
        return -1;
    }
    if (out_bus != NULL) {
        *out_bus = bus;
    }
    if (out_device != NULL) {
        *out_device = device;
    }
    return 0;
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

static JSValue i2c_make_device_status(
    JSContext *ctx,
    const esp32_mquickjs_i2c_slot_t *bus,
    const esp32_mquickjs_i2c_device_slot_t *device)
{
    JSGCRef status_ref;
    JSValue *status = JS_PushGCRef(ctx, &status_ref);

    *status = JS_NewObject(ctx);
    if (JS_IsException(*status) ||
        !esp32_mquickjs_set_property_ref(ctx, status, "opened", JS_TRUE) ||
        !esp32_mquickjs_set_property_ref(
            ctx, status, "controller", JS_NewInt32(ctx, bus->bus_id)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, status, "address", JS_NewUint32(ctx, device->address)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, status, "freqHz", JS_NewUint32(ctx, device->freq_hz)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, status, "timeoutMs", JS_NewUint32(ctx, device->timeout_ms))) {
        JS_PopGCRef(ctx, &status_ref);
        return JS_EXCEPTION;
    }
    return JS_PopGCRef(ctx, &status_ref);
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

static JSValue i2c_open_bus(JSContext *ctx, int argc, JSValue *argv)
{
    gpio_num_t sda_pin = (gpio_num_t)ESP32_MQUICKJS_I2C_DEFAULT_SDA_PIN;
    gpio_num_t scl_pin = (gpio_num_t)ESP32_MQUICKJS_I2C_DEFAULT_SCL_PIN;
    uint32_t freq_hz = ESP32_MQUICKJS_I2C_DEFAULT_FREQ_HZ;
    uint32_t timeout_ms = ESP32_MQUICKJS_I2C_DEFAULT_TIMEOUT_MS;
    bool internal_pullup = ESP32_MQUICKJS_I2C_ENABLE_INTERNAL_PULLUP;
    i2c_master_bus_config_t bus_config = {0};
    esp32_mquickjs_i2c_slot_t *slot;
    JSValue result;
    int resource_result;

    if (argc > 1) {
        return JS_ThrowTypeError(ctx,
                                 "i2c.openBus(options?) expects at most one options object");
    }
    if (argc >= 1 && !JS_IsUndefined(argv[0])) {
        JSGCRef property_ref;
        JSValue *property;

        if (JS_GetClassID(ctx, argv[0]) < 0 || JS_IsArray(ctx, argv[0])) {
            return JS_ThrowTypeError(ctx,
                                     "i2c.openBus(options?) expects an object with sda/scl/freqHz/timeoutMs/internalPullup");
        }

        property = JS_PushGCRef(ctx, &property_ref);

        *property = JS_GetPropertyStr(ctx, argv[0], "sda");
        if (JS_IsException(*property)) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_EXCEPTION;
        }
        if (!JS_IsUndefined(*property) && !js_value_to_gpio_num(ctx, *property, &sda_pin)) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_ThrowTypeError(ctx, "i2c.openBus({ sda }) expects a valid GPIO");
        }

        *property = JS_GetPropertyStr(ctx, argv[0], "scl");
        if (JS_IsException(*property)) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_EXCEPTION;
        }
        if (!JS_IsUndefined(*property) && !js_value_to_gpio_num(ctx, *property, &scl_pin)) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_ThrowTypeError(ctx, "i2c.openBus({ scl }) expects a valid GPIO");
        }

        *property = JS_GetPropertyStr(ctx, argv[0], "freqHz");
        if (JS_IsException(*property)) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_EXCEPTION;
        }
        if (!JS_IsUndefined(*property) &&
            (!js_value_to_u32(ctx, *property, &freq_hz) || freq_hz == 0)) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_ThrowTypeError(ctx, "i2c.openBus({ freqHz }) expects a positive integer");
        }

        *property = JS_GetPropertyStr(ctx, argv[0], "timeoutMs");
        if (JS_IsException(*property)) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_EXCEPTION;
        }
        if (!JS_IsUndefined(*property) && !js_value_to_u32(ctx, *property, &timeout_ms)) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_ThrowTypeError(ctx, "i2c.openBus({ timeoutMs }) expects a non-negative integer");
        }

        *property = JS_GetPropertyStr(ctx, argv[0], "internalPullup");
        if (JS_IsException(*property)) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_EXCEPTION;
        }
        if (!JS_IsUndefined(*property) && !js_value_to_bool(ctx, *property, &internal_pullup)) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_ThrowTypeError(ctx, "i2c.openBus({ internalPullup }) expects a boolean-like value");
        }
        JS_PopGCRef(ctx, &property_ref);
    }

    if (!GPIO_IS_VALID_GPIO(sda_pin) || !GPIO_IS_VALID_GPIO(scl_pin)) {
        return JS_ThrowTypeError(
            ctx,
            "i2c.openBus() requires configured default SDA/SCL GPIOs or explicit { sda, scl } overrides");
    }

    bus_config.sda_io_num = sda_pin;
    bus_config.scl_io_num = scl_pin;
    bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_config.glitch_ignore_cnt = 7;
    bus_config.flags.enable_internal_pullup = internal_pullup ? 1U : 0U;

    slot = i2c_alloc_slot(&bus_config, &resource_result);
    if (slot == NULL) {
        if (resource_result ==
            ESP32_MQUICKJS_I2C_BUS_RESOURCE_UNAVAILABLE) {
            return JS_ThrowInternalError(
                ctx,
                "i2c.openBus() failed: no available I2C bus slots");
        }
        return i2c_throw_error(ctx, (esp_err_t)resource_result,
                               "i2c.openBus() failed");
    }

    slot->sda_pin = sda_pin;
    slot->scl_pin = scl_pin;
    slot->freq_hz = freq_hz;
    slot->timeout_ms = timeout_ms;
    slot->internal_pullup = internal_pullup;

    result = i2c_make_bus_object(ctx, slot);
    if (JS_IsException(result)) {
        slot->release_pending = true;
        (void)i2c_cleanup_slot(slot);
    }
    return result;
}

bool esp32_mquickjs_deinit_i2c_runtime(void)
{
    int32_t i;

    for (i = 0; i < (int32_t)SOC_I2C_NUM; ++i) {
        if (s_i2c_slots[i].allocated) {
            esp_err_t err;

            s_i2c_slots[i].release_pending = true;
            err = i2c_cleanup_slot(&s_i2c_slots[i]);
            if (err != ESP_OK) {
                return false;
            }
        }
    }
    i2c_reset_slots();
    return true;
}

static bool i2c_register_future_drivers(JSContext *ctx,
                                        esp32_mquickjs_runtime_t *runtime);

bool esp32_mquickjs_init_i2c_runtime(JSContext *ctx,
                                     esp32_mquickjs_runtime_t *runtime)
{
    if (!esp32_mquickjs_deinit_i2c_runtime()) {
        JS_ThrowInternalError(ctx, "I2C runtime cleanup failed");
        return false;
    }
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
        slot->release_pending = true;
        (void)i2c_cleanup_slot(slot);
    }
    heap_caps_free(bus_ref);
}

JSValue js_i2c_bus_close(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    esp32_mquickjs_i2c_bus_ref_t bus_ref;
    esp32_mquickjs_i2c_bus_ref_t *bus_ref_ptr;
    esp32_mquickjs_i2c_slot_t *slot;
    esp_err_t err;

    (void)argc;
    (void)argv;

    if (i2c_bus_ref_from_object(ctx, *this_val, "I2CBus.close()", &bus_ref) != 0) {
        return JS_EXCEPTION;
    }
    slot = i2c_get_slot(&bus_ref);
    if (slot != NULL) {
        if (slot->busy || slot->future_reservations > 0) {
            return JS_ThrowInternalError(ctx,
                                         "I2CBus.close() refused while an operation is pending");
        }
        err = i2c_cleanup_slot(slot);
        if (err != ESP_OK) {
            return i2c_throw_error(ctx, err, "I2CBus.close() failed");
        }
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

JSValue js_i2c_bus_open_device(JSContext *ctx, JSValue *this_val,
                               int argc, JSValue *argv)
{
    esp32_mquickjs_i2c_bus_ref_t bus_ref;
    esp32_mquickjs_i2c_slot_t *bus;
    esp32_mquickjs_i2c_device_slot_t *device;
    i2c_device_config_t config = {0};
    uint16_t address;
    uint32_t freq_hz;
    uint32_t timeout_ms;
    JSGCRef property_ref;
    JSValue *property;
    JSValue result;
    esp_err_t err;

    if (i2c_get_this_slot(ctx, *this_val, "I2CBus.openDevice()",
                          &bus_ref, &bus) != 0) {
        return JS_EXCEPTION;
    }
    if (argc != 1 || JS_GetClassID(ctx, argv[0]) < 0 ||
        JS_IsArray(ctx, argv[0])) {
        return JS_ThrowTypeError(
            ctx,
            "I2CBus.openDevice(options) expects { address, freqHz?, timeoutMs? }");
    }
    freq_hz = bus->freq_hz;
    timeout_ms = bus->timeout_ms;
    property = JS_PushGCRef(ctx, &property_ref);
    *property = JS_GetPropertyStr(ctx, argv[0], "address");
    if (JS_IsException(*property) ||
        !js_value_to_i2c_address(ctx, *property, &address)) {
        JS_PopGCRef(ctx, &property_ref);
        return JS_HasException(ctx)
                   ? JS_EXCEPTION
                   : JS_ThrowTypeError(
                         ctx,
                         "I2CBus.openDevice({ address }) expects a 7-bit address in 0x03..0x77");
    }
    *property = JS_GetPropertyStr(ctx, argv[0], "freqHz");
    if (JS_IsException(*property) ||
        (!JS_IsUndefined(*property) &&
         (!js_value_to_u32(ctx, *property, &freq_hz) || freq_hz == 0))) {
        JS_PopGCRef(ctx, &property_ref);
        return JS_HasException(ctx)
                   ? JS_EXCEPTION
                   : JS_ThrowTypeError(
                         ctx,
                         "I2CBus.openDevice({ freqHz }) expects a positive integer");
    }
    *property = JS_GetPropertyStr(ctx, argv[0], "timeoutMs");
    if (JS_IsException(*property) ||
        (!JS_IsUndefined(*property) &&
         (!js_value_to_u32(ctx, *property, &timeout_ms) ||
          timeout_ms == 0 || timeout_ms > (uint32_t)INT_MAX ||
          timeout_ms > UINT32_MAX / 1000U))) {
        JS_PopGCRef(ctx, &property_ref);
        return JS_HasException(ctx)
                   ? JS_EXCEPTION
                   : JS_ThrowTypeError(
                         ctx,
                         "I2CBus.openDevice({ timeoutMs }) expects a positive supported integer");
    }
    JS_PopGCRef(ctx, &property_ref);

    device = heap_caps_calloc(1, sizeof(*device), MALLOC_CAP_8BIT);
    if (device == NULL) {
        return JS_ThrowOutOfMemory(ctx);
    }
    config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    config.device_address = address;
    config.scl_speed_hz = freq_hz;
    config.scl_wait_us = timeout_ms * 1000U;
    config.flags.disable_ack_check = 0;
    err = i2c_master_bus_add_device(bus->bus_handle, &config,
                                    &device->handle);
    if (err != ESP_OK) {
        heap_caps_free(device);
        return i2c_throw_error(ctx, err, "I2CBus.openDevice() failed");
    }
    device->generation = i2c_take_device_generation();
    device->address = address;
    device->freq_hz = freq_hz;
    device->timeout_ms = timeout_ms;
    device->next = bus->devices;
    bus->devices = device;

    result = i2c_make_device_object(ctx, bus, device);
    if (JS_IsException(result)) {
        (void)i2c_cleanup_device(bus, device);
    }
    return result;
}

JSValue js_i2c_device_constructor(JSContext *ctx, JSValue *this_val,
                                  int argc, JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_ThrowTypeError(
        ctx, "I2CDevice cannot be constructed directly; use I2CBus.openDevice()");
}

void js_i2c_device_finalizer(JSContext *ctx, void *opaque)
{
    esp32_mquickjs_i2c_device_ref_t *ref = opaque;
    esp32_mquickjs_i2c_slot_t *bus = NULL;
    esp32_mquickjs_i2c_device_slot_t *device;

    (void)ctx;
    if (ref == NULL) {
        return;
    }
    device = i2c_get_device(ref, &bus);
    if (device != NULL) {
        device->release_pending = true;
        if (device->future_reservations == 0 && !bus->busy) {
            (void)i2c_cleanup_device(bus, device);
        }
    }
    heap_caps_free(ref);
}

JSValue js_i2c_device_close(JSContext *ctx, JSValue *this_val,
                            int argc, JSValue *argv)
{
    esp32_mquickjs_i2c_device_ref_t ref;
    esp32_mquickjs_i2c_device_ref_t *opaque;
    esp32_mquickjs_i2c_slot_t *bus = NULL;
    esp32_mquickjs_i2c_device_slot_t *device;
    esp_err_t err;

    (void)argv;
    if (argc != 0 ||
        i2c_device_ref_from_object(ctx, *this_val, "I2CDevice.close()",
                                   &ref) != 0) {
        return argc != 0
                   ? JS_ThrowTypeError(ctx,
                                       "I2CDevice.close() expects no arguments")
                   : JS_EXCEPTION;
    }
    device = i2c_get_device(&ref, &bus);
    if (device != NULL) {
        if (device->future_reservations > 0 || bus->busy) {
            return JS_ThrowInternalError(
                ctx,
                "I2CDevice.close() refused while an operation is pending");
        }
        err = i2c_cleanup_device(bus, device);
        if (err != ESP_OK) {
            return i2c_throw_error(ctx, err, "I2CDevice.close() failed");
        }
    }
    opaque = JS_GetOpaque(ctx, *this_val);
    if (opaque != NULL) {
        opaque->bus_id = -1;
        opaque->bus_generation = 0;
        opaque->device_generation = 0;
    }
    return JS_TRUE;
}

JSValue js_i2c_device_status(JSContext *ctx, JSValue *this_val,
                             int argc, JSValue *argv)
{
    esp32_mquickjs_i2c_device_ref_t ref;
    esp32_mquickjs_i2c_slot_t *bus;
    esp32_mquickjs_i2c_device_slot_t *device;

    (void)argv;
    if (argc != 0) {
        return JS_ThrowTypeError(ctx,
                                 "I2CDevice.status() expects no arguments");
    }
    if (i2c_get_bound_device(ctx, *this_val, "I2CDevice.status()", &ref,
                             &bus, &device) != 0) {
        return JS_EXCEPTION;
    }
    return i2c_make_device_status(ctx, bus, device);
}

typedef enum {
    I2C_FUTURE_SCAN,
    I2C_FUTURE_WRITE,
    I2C_FUTURE_WRITE_BATCH,
    I2C_FUTURE_WRITE_SEGMENTS,
    I2C_FUTURE_READ,
    I2C_FUTURE_WRITE_READ,
} i2c_future_kind_t;

struct esp32_mquickjs_future_driver_state {
    i2c_future_kind_t kind;
    JSContext *ctx;
    JSGCRef owner_ref;
    esp32_mquickjs_i2c_bus_ref_t bus_ref;
    esp32_mquickjs_i2c_device_ref_t device_ref;
    esp32_mquickjs_runtime_t *runtime;
    esp32_mquickjs_future_token_t token;
    uint32_t timeout_ms;
    uint32_t chunk_count;
    uint32_t completed_chunks;
    uint32_t segment_count;
    uint32_t scan_count;
    uint32_t *chunk_lengths;
    i2c_master_transmit_multi_buffer_info_t *segment_buffers;
    uint8_t scan_addresses[0x78 - 0x03];
    uint8_t *write_data;
    size_t write_length;
    uint8_t *read_data;
    size_t read_length;
    uint64_t total_us;
    esp_err_t err;
    _Atomic bool completed;
    bool owner_retained;
    bool bus_reserved;
    bool device_reserved;
    bool started;
    bool cancelled;
};

static void i2c_future_release_owner(
    esp32_mquickjs_future_driver_state_t *state)
{
    if (state != NULL && state->owner_retained) {
        JS_DeleteGCRef(state->ctx, &state->owner_ref);
        state->owner_retained = false;
    }
}

static void i2c_future_release_bus(
    esp32_mquickjs_future_driver_state_t *state)
{
    esp32_mquickjs_i2c_slot_t *slot;
    esp32_mquickjs_i2c_device_slot_t *device;
    esp32_mquickjs_i2c_device_slot_t *next;

    if (state == NULL) {
        return;
    }
    slot = i2c_get_slot(&state->bus_ref);
    if (slot != NULL && state->started) {
        slot->busy = false;
    }
    device = i2c_get_device(&state->device_ref, NULL);
    if (device != NULL && state->device_reserved &&
        device->future_reservations > 0) {
        device->future_reservations--;
    }
    state->device_reserved = false;
    if (slot != NULL && state->bus_reserved &&
        slot->future_reservations > 0) {
        slot->future_reservations--;
    }
    state->bus_reserved = false;
    state->started = false;
    i2c_future_release_owner(state);
    if (slot != NULL && !slot->busy) {
        for (device = slot->devices; device != NULL; device = next) {
            next = device->next;
            if (device->release_pending &&
                device->future_reservations == 0) {
                (void)i2c_cleanup_device(slot, device);
            }
        }
    }
    if (slot != NULL && slot->release_pending && !slot->busy &&
        slot->future_reservations == 0) {
        (void)i2c_cleanup_slot(slot);
    }
}

static void i2c_future_release_prepare_state(
    esp32_mquickjs_future_driver_state_t *state)
{
    if (state == NULL) {
        return;
    }
    i2c_future_release_bus(state);
    heap_caps_free(state->chunk_lengths);
    heap_caps_free(state->segment_buffers);
    heap_caps_free(state->write_data);
    heap_caps_free(state->read_data);
    heap_caps_free(state);
}

static esp32_mquickjs_future_driver_state_t *i2c_future_allocate_bus(
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
    atomic_init(&state->completed, false);
    if (i2c_get_this_slot(ctx,
                          this_value,
                          "I2CBus Future operation",
                          &state->bus_ref,
                          &slot) != 0) {
        heap_caps_free(state);
        return NULL;
    }
    if (slot->future_reservations == UINT16_MAX) {
        heap_caps_free(state);
        JS_ThrowInternalError(ctx,
                              "I2CBus operation reservation limit reached");
        return NULL;
    }
    state->kind = kind;
    state->ctx = ctx;
    state->timeout_ms = slot->timeout_ms;
    slot->future_reservations++;
    state->bus_reserved = true;
    owner = JS_AddGCRef(ctx, &state->owner_ref);
    *owner = this_value;
    state->owner_retained = true;
    return state;
}

static esp32_mquickjs_future_driver_state_t *i2c_future_allocate_device(
    JSContext *ctx,
    JSValue this_value,
    i2c_future_kind_t kind)
{
    esp32_mquickjs_future_driver_state_t *state;
    esp32_mquickjs_i2c_slot_t *bus;
    esp32_mquickjs_i2c_device_slot_t *device;
    JSValue *owner;

    state = heap_caps_calloc(1, sizeof(*state), MALLOC_CAP_8BIT);
    if (state == NULL) {
        JS_ThrowOutOfMemory(ctx);
        return NULL;
    }
    atomic_init(&state->completed, false);
    if (i2c_get_bound_device(ctx, this_value, "I2CDevice Future operation",
                             &state->device_ref, &bus, &device) != 0) {
        heap_caps_free(state);
        return NULL;
    }
    if (bus->future_reservations == UINT16_MAX ||
        device->future_reservations == UINT16_MAX) {
        heap_caps_free(state);
        JS_ThrowInternalError(
            ctx, "I2CDevice operation reservation limit reached");
        return NULL;
    }
    state->bus_ref.bus_id = state->device_ref.bus_id;
    state->bus_ref.generation = state->device_ref.bus_generation;
    state->kind = kind;
    state->ctx = ctx;
    state->timeout_ms = device->timeout_ms;
    bus->future_reservations++;
    device->future_reservations++;
    state->bus_reserved = true;
    state->device_reserved = true;
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
    *out_state = i2c_future_allocate_bus(ctx, this_ref->val, I2C_FUTURE_SCAN);
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

    if (out_state == NULL || argc != 1) {
        JS_ThrowTypeError(ctx,
                          "I2CDevice.write(data) expects one byte source");
        return false;
    }
    state = i2c_future_allocate_device(ctx, this_ref->val, I2C_FUTURE_WRITE);
    if (state == NULL) {
        return false;
    }
    if (!i2c_future_copy_bytes(ctx, argv[0].val, "I2CDevice.write(data)",
                               &state->write_data, &state->write_length)) {
        i2c_future_release_prepare_state(state);
        return false;
    }
    *out_state = state;
    return true;
}

static bool i2c_write_sources_future_prepare(
    JSContext *ctx,
    JSGCRef *this_ref,
    int argc,
    JSGCRef *argv,
    i2c_future_kind_t kind,
    const char *api_name,
    esp32_mquickjs_future_driver_state_t **out_state)
{
    esp32_mquickjs_future_driver_state_t *state;
    JSValue error = JS_UNDEFINED;
    uint32_t index;
    size_t total = 0;

    if (out_state == NULL || argc != 1) {
        JS_ThrowTypeError(ctx, "%s expects byte-source chunks",
                          api_name);
        return false;
    }
    state = i2c_future_allocate_device(ctx, this_ref->val, kind);
    if (state == NULL) {
        return false;
    }
    if (!esp32_mquickjs_get_byte_source_array_length(
            ctx, argv[0].val, api_name,
            &state->chunk_count, &error)) {
        if (JS_IsUndefined(error)) {
            JS_ThrowTypeError(ctx, "%s expects an array-like object", api_name);
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
                ctx, argv[0].val, index, api_name,
                &chunk, &error)) {
            i2c_future_release_prepare_state(state);
            return false;
        }
        if (chunk.source.length > SIZE_MAX - total) {
            esp32_mquickjs_release_byte_source_chunk(ctx, &chunk);
            i2c_future_release_prepare_state(state);
            JS_ThrowRangeError(ctx, "%s byte length overflow", api_name);
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
    if (kind == I2C_FUTURE_WRITE_SEGMENTS && state->chunk_count > 0) {
        size_t offset = 0;

        state->segment_buffers = heap_caps_calloc(
            state->chunk_count, sizeof(*state->segment_buffers), MALLOC_CAP_8BIT);
        if (state->segment_buffers == NULL) {
            i2c_future_release_prepare_state(state);
            JS_ThrowOutOfMemory(ctx);
            return false;
        }
        for (index = 0; index < state->chunk_count; ++index) {
            uint32_t length = state->chunk_lengths[index];

            if (length > 0) {
                state->segment_buffers[state->segment_count].write_buffer =
                    state->write_data + offset;
                state->segment_buffers[state->segment_count].buffer_size = length;
                ++state->segment_count;
            }
            offset += length;
        }
    }
    *out_state = state;
    return true;
}

static bool i2c_write_batch_future_prepare(
    JSContext *ctx, JSGCRef *this_ref, int argc, JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **out_state)
{
    return i2c_write_sources_future_prepare(
        ctx, this_ref, argc, argv, I2C_FUTURE_WRITE_BATCH,
        "I2CDevice.writeBatch(chunks)", out_state);
}

static bool i2c_write_segments_future_prepare(
    JSContext *ctx, JSGCRef *this_ref, int argc, JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **out_state)
{
    return i2c_write_sources_future_prepare(
        ctx, this_ref, argc, argv, I2C_FUTURE_WRITE_SEGMENTS,
        "I2CDevice.writeSegments(segments)", out_state);
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

    if (out_state == NULL || argc != 1 ||
        !js_value_to_u32(ctx, argv[0].val, &read_length)) {
        JS_ThrowTypeError(ctx,
                          "I2CDevice.read(length) expects a byte length");
        return false;
    }
    state = i2c_future_allocate_device(ctx, this_ref->val, I2C_FUTURE_READ);
    if (state == NULL) {
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

static bool i2c_write_read_future_prepare(
    JSContext *ctx,
    JSGCRef *this_ref,
    int argc,
    JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **out_state)
{
    esp32_mquickjs_future_driver_state_t *state;
    uint32_t read_length;

    if (out_state == NULL || argc != 2 ||
        !js_value_to_u32(ctx, argv[1].val, &read_length)) {
        JS_ThrowTypeError(ctx,
                          "I2CDevice.writeRead(writeData, readLength) expects write bytes and a read length");
        return false;
    }
    state = i2c_future_allocate_device(ctx, this_ref->val,
                                       I2C_FUTURE_WRITE_READ);
    if (state == NULL) {
        return false;
    }
    if (!i2c_future_copy_bytes(ctx, argv[0].val,
                               "I2CDevice.writeRead(writeData, readLength)",
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
    esp32_mquickjs_i2c_device_slot_t *device_slot =
        i2c_get_device(&state->device_ref, NULL);
    i2c_master_dev_handle_t device = device_slot != NULL
        ? device_slot->handle : NULL;
    int64_t started_us = esp_timer_get_time();

    if (slot == NULL) {
        state->err = ESP_ERR_INVALID_STATE;
        atomic_store_explicit(&state->completed, true, memory_order_release);
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
        atomic_store_explicit(&state->completed, true, memory_order_release);
        return;
    }
    if (device == NULL) {
        state->err = ESP_ERR_INVALID_STATE;
        atomic_store_explicit(&state->completed, true, memory_order_release);
        return;
    }
    if (state->kind == I2C_FUTURE_WRITE) {
        state->err = i2c_master_transmit(device,
                                         state->write_data,
                                         state->write_length,
                                         (int)state->timeout_ms);
    } else if (state->kind == I2C_FUTURE_WRITE_BATCH) {
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
    } else if (state->kind == I2C_FUTURE_WRITE_SEGMENTS) {
        state->err = state->segment_count == 0
                         ? ESP_OK
                         : i2c_master_multi_buffer_transmit(
                               device, state->segment_buffers,
                               state->segment_count, (int)state->timeout_ms);
        if (state->err == ESP_OK) {
            state->completed_chunks = state->segment_count;
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
    state->total_us = (uint64_t)(esp_timer_get_time() - started_us);
    atomic_store_explicit(&state->completed, true, memory_order_release);
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
    return state != NULL && atomic_load_explicit(
                                &state->completed, memory_order_acquire)
        ? ESP32_MQUICKJS_FUTURE_READY
        : ESP32_MQUICKJS_FUTURE_PENDING;
}

static JSValue i2c_future_finish(JSContext *ctx,
                                 esp32_mquickjs_future_driver_state_t *state)
{
    i2c_future_release_bus(state);
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
    if (state->kind == I2C_FUTURE_WRITE_SEGMENTS) {
        return JS_NewInt64(ctx, (int64_t)state->write_length);
    }
    if (state->kind == I2C_FUTURE_WRITE_BATCH) {
        return i2c_make_write_chunks_stats(ctx,
                                           state->completed_chunks,
                                           state->write_length,
                                           state->total_us);
    }
    {
        JSValue result = esp32_mquickjs_new_owned_byte_view(
            ctx, state->read_data, state->read_length);

        if (!JS_IsException(result)) {
            state->read_data = NULL;
        }
        return result;
    }
}

static esp32_mquickjs_cancel_result_t i2c_future_cancel(
    esp32_mquickjs_future_driver_state_t *state)
{
    (void)state;
    return ESP32_MQUICKJS_CANCEL_REJECTED;
}

static void i2c_future_destroy(esp32_mquickjs_future_driver_state_t *state)
{
    if (state == NULL) {
        return;
    }
    i2c_future_release_bus(state);
    i2c_future_release_prepare_state(state);
}

static uint32_t i2c_future_timeout_ms(
    const esp32_mquickjs_future_driver_state_t *state)
{
    return state != NULL ? state->timeout_ms : 0;
}

static esp32_mquickjs_resource_key_t i2c_future_resource_key(
    const esp32_mquickjs_future_driver_state_t *state)
{
    return state != NULL ? i2c_get_slot(&state->bus_ref) : NULL;
}

#define I2C_FUTURE_DRIVER(name, prepare_fn) \
    static const esp32_mquickjs_future_driver_t name = { \
        .capture = prepare_fn, \
        .start = i2c_future_start, \
        .poll = i2c_future_poll, \
        .finish = i2c_future_finish, \
        .cancel = i2c_future_cancel, \
        .destroy = i2c_future_destroy, \
        .timeout_ms = i2c_future_timeout_ms, \
        .resource_key = i2c_future_resource_key, \
    }

I2C_FUTURE_DRIVER(s_i2c_scan_driver, i2c_scan_future_prepare);
I2C_FUTURE_DRIVER(s_i2c_write_driver, i2c_write_future_prepare);
I2C_FUTURE_DRIVER(s_i2c_write_batch_driver, i2c_write_batch_future_prepare);
I2C_FUTURE_DRIVER(s_i2c_write_segments_driver, i2c_write_segments_future_prepare);
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
    static const char *device_names[] = {
        "write", "writeSegments", "writeBatch", "read", "writeRead"
    };
    static const esp32_mquickjs_future_driver_t *device_drivers[] = {
        &s_i2c_write_driver,
        &s_i2c_write_segments_driver,
        &s_i2c_write_batch_driver,
        &s_i2c_read_driver,
        &s_i2c_write_read_driver,
    };
    JSGCRef bus_ref;
    JSGCRef device_ref;
    JSGCRef method_ref;
    JSValue *bus = JS_PushGCRef(ctx, &bus_ref);
    JSValue *device = JS_PushGCRef(ctx, &device_ref);
    JSValue *method = JS_PushGCRef(ctx, &method_ref);
    size_t index;
    bool result;

    *bus = JS_NewObjectClassUser(ctx, JS_CLASS_I2C_BUS);
    *method = JS_IsException(*bus)
        ? JS_EXCEPTION : JS_GetPropertyStr(ctx, *bus, "scan");
    result = !JS_IsException(*method) &&
             esp32_mquickjs_future_register_driver(
                 ctx, runtime, *method, &s_i2c_scan_driver);
    *device = result
        ? JS_NewObjectClassUser(ctx, JS_CLASS_I2C_DEVICE) : JS_EXCEPTION;
    for (index = 0;
         result && index < sizeof(device_names) / sizeof(device_names[0]);
         ++index) {
        *method = JS_GetPropertyStr(ctx, *device, device_names[index]);
        result = !JS_IsException(*method) &&
                 esp32_mquickjs_future_register_driver(ctx, runtime,
                                                       *method,
                                                       device_drivers[index]);
    }
    if (!result && !JS_IsException(*bus)) {
        JS_ThrowInternalError(ctx, "failed to register I2C Future drivers");
    }
    JS_PopGCRef(ctx, &method_ref);
    JS_PopGCRef(ctx, &device_ref);
    JS_PopGCRef(ctx, &bus_ref);
    return result;
}

JSValue js_i2c_bus_scan(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    return i2c_future_call_and_wait(ctx, *this_val, "scan", argc, argv);
}

JSValue js_i2c_device_write(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    return i2c_future_call_and_wait(ctx, *this_val, "write", argc, argv);
}

JSValue js_i2c_device_write_batch(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    return i2c_future_call_and_wait(ctx, *this_val, "writeBatch", argc, argv);
}

JSValue js_i2c_device_write_segments(JSContext *ctx, JSValue *this_val,
                                     int argc, JSValue *argv)
{
    return i2c_future_call_and_wait(ctx, *this_val, "writeSegments", argc, argv);
}

JSValue js_i2c_device_read(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    return i2c_future_call_and_wait(ctx, *this_val, "read", argc, argv);
}

JSValue js_i2c_device_write_read(JSContext *ctx, JSValue *this_val,
                                 int argc, JSValue *argv)
{
    return i2c_future_call_and_wait(ctx, *this_val, "writeRead", argc, argv);
}

JSValue js_i2c_open_bus(JSContext *ctx, JSValue *this_val,
                        int argc, JSValue *argv)
{
    (void)this_val;
    return i2c_open_bus(ctx, argc, argv);
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
