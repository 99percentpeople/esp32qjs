#include "esp32_mquickjs_spi.h"

#if CONFIG_ESP32_MQUICKJS_FEATURE_SPI

#include "esp32_mquickjs_core.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_heap_caps.h"

#define ESP32_MQUICKJS_SPI_DEFAULT_HOST_NUMBER 2
#define ESP32_MQUICKJS_SPI_BUS_SLOT_COUNT (SPI_HOST_MAX - SPI2_HOST)
#define ESP32_MQUICKJS_SPI_DEVICE_SLOT_COUNT \
    (ESP32_MQUICKJS_SPI_BUS_SLOT_COUNT * ESP32_MQUICKJS_SPI_MAX_DEVICES_PER_BUS)

typedef struct {
    int32_t slot_id;
    uint32_t generation;
} esp32_mquickjs_spi_bus_ref_t;

typedef struct {
    int32_t slot_id;
    uint32_t generation;
} esp32_mquickjs_spi_device_ref_t;

typedef struct {
    bool allocated;
    int32_t slot_id;
    uint32_t generation;
    int32_t host_number;
    spi_host_device_t host_id;
    int32_t sclk_pin;
    int32_t mosi_pin;
    int32_t miso_pin;
    uint32_t max_transfer_size;
    uint32_t open_devices;
} esp32_mquickjs_spi_bus_slot_t;

typedef struct {
    bool allocated;
    int32_t slot_id;
    uint32_t generation;
    int32_t parent_bus_slot_id;
    uint32_t parent_bus_generation;
    int32_t cs_pin;
    uint8_t mode;
    uint32_t freq_hz;
    uint32_t queue_size;
    bool cs_high;
    bool lsb_first;
    spi_device_handle_t handle;
} esp32_mquickjs_spi_device_slot_t;

static esp32_mquickjs_spi_bus_slot_t s_spi_bus_slots[ESP32_MQUICKJS_SPI_BUS_SLOT_COUNT];
static esp32_mquickjs_spi_device_slot_t s_spi_device_slots[ESP32_MQUICKJS_SPI_DEVICE_SLOT_COUNT];
static uint32_t s_spi_next_generation = 1;

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

static bool js_value_to_gpio_num(JSContext *ctx, JSValue value, int32_t *out_pin, bool allow_negative_one)
{
    int pin = -1;

    if (JS_ToInt32(ctx, &pin, value) != 0) {
        return false;
    }
    if (allow_negative_one && pin == -1) {
        *out_pin = -1;
        return true;
    }
    if (pin < 0 || pin >= GPIO_NUM_MAX || !GPIO_IS_VALID_GPIO(pin)) {
        return false;
    }

    *out_pin = pin;
    return true;
}

static bool js_value_to_spi_host_number(JSContext *ctx, JSValue value, int32_t *out_host_number)
{
    uint32_t host_number = 0;

    if (!js_value_to_u32(ctx, value, &host_number)) {
        return false;
    }
    if (host_number == 2U) {
        *out_host_number = 2;
        return true;
    }
#if SOC_SPI_PERIPH_NUM > 2
    if (host_number == 3U) {
        *out_host_number = 3;
        return true;
    }
#endif
    return false;
}

static bool js_value_to_spi_mode(JSContext *ctx, JSValue value, uint8_t *out_mode)
{
    uint32_t mode = 0;

    if (!js_value_to_u32(ctx, value, &mode) || mode > 3U) {
        return false;
    }
    *out_mode = (uint8_t)mode;
    return true;
}

static JSValue spi_throw_error(JSContext *ctx, esp_err_t err, const char *message)
{
    return JS_ThrowInternalError(ctx, "%s: %s", message, esp_err_to_name(err));
}

static spi_host_device_t spi_slot_id_to_host_id(int32_t slot_id)
{
    if (slot_id <= 0) {
        return SPI2_HOST;
    }
#if SOC_SPI_PERIPH_NUM > 2
    return SPI3_HOST;
#else
    return SPI2_HOST;
#endif
}

static bool spi_host_number_to_ids(int32_t host_number,
                                   int32_t *out_slot_id,
                                   spi_host_device_t *out_host_id)
{
    if (host_number == 2) {
        if (out_slot_id != NULL) {
            *out_slot_id = 0;
        }
        if (out_host_id != NULL) {
            *out_host_id = SPI2_HOST;
        }
        return true;
    }
#if SOC_SPI_PERIPH_NUM > 2
    if (host_number == 3) {
        if (out_slot_id != NULL) {
            *out_slot_id = 1;
        }
        if (out_host_id != NULL) {
            *out_host_id = SPI3_HOST;
        }
        return true;
    }
#endif
    return false;
}

static void spi_init_bus_slot(esp32_mquickjs_spi_bus_slot_t *slot, int32_t slot_id)
{
    memset(slot, 0, sizeof(*slot));
    slot->slot_id = slot_id;
    slot->host_number = slot_id + 2;
    slot->host_id = spi_slot_id_to_host_id(slot_id);
    slot->sclk_pin = ESP32_MQUICKJS_SPI_DEFAULT_SCLK_PIN;
    slot->mosi_pin = ESP32_MQUICKJS_SPI_DEFAULT_MOSI_PIN;
    slot->miso_pin = ESP32_MQUICKJS_SPI_DEFAULT_MISO_PIN;
    slot->max_transfer_size = ESP32_MQUICKJS_SPI_DEFAULT_MAX_TRANSFER_SIZE;
}

static void spi_init_device_slot(esp32_mquickjs_spi_device_slot_t *slot, int32_t slot_id)
{
    memset(slot, 0, sizeof(*slot));
    slot->slot_id = slot_id;
    slot->parent_bus_slot_id = -1;
    slot->cs_pin = ESP32_MQUICKJS_SPI_DEFAULT_CS_PIN;
}

static void spi_reset_slots(void)
{
    int32_t i;

    for (i = 0; i < (int32_t)ESP32_MQUICKJS_SPI_BUS_SLOT_COUNT; ++i) {
        spi_init_bus_slot(&s_spi_bus_slots[i], i);
    }
    for (i = 0; i < (int32_t)ESP32_MQUICKJS_SPI_DEVICE_SLOT_COUNT; ++i) {
        spi_init_device_slot(&s_spi_device_slots[i], i);
    }
    s_spi_next_generation = 1;
}

static uint32_t spi_take_generation(void)
{
    uint32_t generation = s_spi_next_generation++;

    if (generation == 0) {
        generation = s_spi_next_generation++;
    }
    return generation;
}

static esp32_mquickjs_spi_bus_slot_t *spi_get_bus_slot_by_ids(int32_t slot_id, uint32_t generation)
{
    esp32_mquickjs_spi_bus_slot_t *slot;

    if (slot_id < 0 || slot_id >= (int32_t)ESP32_MQUICKJS_SPI_BUS_SLOT_COUNT) {
        return NULL;
    }

    slot = &s_spi_bus_slots[slot_id];
    if (!slot->allocated || slot->generation != generation) {
        return NULL;
    }
    return slot;
}

static esp32_mquickjs_spi_bus_slot_t *spi_get_bus_slot(const esp32_mquickjs_spi_bus_ref_t *ref)
{
    if (ref == NULL) {
        return NULL;
    }
    return spi_get_bus_slot_by_ids(ref->slot_id, ref->generation);
}

static esp32_mquickjs_spi_device_slot_t *spi_get_device_slot(const esp32_mquickjs_spi_device_ref_t *ref)
{
    esp32_mquickjs_spi_device_slot_t *slot;

    if (ref == NULL || ref->slot_id < 0 || ref->slot_id >= (int32_t)ESP32_MQUICKJS_SPI_DEVICE_SLOT_COUNT) {
        return NULL;
    }

    slot = &s_spi_device_slots[ref->slot_id];
    if (!slot->allocated || slot->generation != ref->generation) {
        return NULL;
    }
    return slot;
}

static void spi_cleanup_device_slot(esp32_mquickjs_spi_device_slot_t *slot)
{
    esp32_mquickjs_spi_bus_slot_t *parent;
    int32_t slot_id;

    if (slot == NULL || !slot->allocated) {
        return;
    }

    slot_id = slot->slot_id;
    if (slot->handle != NULL) {
        spi_bus_remove_device(slot->handle);
    }
    parent = spi_get_bus_slot_by_ids(slot->parent_bus_slot_id, slot->parent_bus_generation);
    if (parent != NULL && parent->open_devices > 0) {
        parent->open_devices--;
    }
    spi_init_device_slot(slot, slot_id);
}

static void spi_cleanup_bus_slot(esp32_mquickjs_spi_bus_slot_t *slot)
{
    int32_t i;
    int32_t slot_id;

    if (slot == NULL || !slot->allocated) {
        return;
    }

    slot_id = slot->slot_id;
    for (i = 0; i < (int32_t)ESP32_MQUICKJS_SPI_DEVICE_SLOT_COUNT; ++i) {
        esp32_mquickjs_spi_device_slot_t *device_slot = &s_spi_device_slots[i];

        if (!device_slot->allocated || device_slot->parent_bus_slot_id != slot->slot_id ||
            device_slot->parent_bus_generation != slot->generation) {
            continue;
        }
        spi_cleanup_device_slot(device_slot);
    }

    if (slot->sclk_pin >= 0) {
        spi_bus_free(slot->host_id);
    }
    spi_init_bus_slot(slot, slot_id);
}

static esp32_mquickjs_spi_bus_slot_t *spi_alloc_bus_slot(int32_t host_number)
{
    int32_t slot_id = -1;
    spi_host_device_t host_id = SPI2_HOST;
    esp32_mquickjs_spi_bus_slot_t *slot;

    if (!spi_host_number_to_ids(host_number, &slot_id, &host_id)) {
        return NULL;
    }

    slot = &s_spi_bus_slots[slot_id];
    if (slot->allocated) {
        return NULL;
    }

    spi_init_bus_slot(slot, slot_id);
    slot->allocated = true;
    slot->generation = spi_take_generation();
    slot->host_number = host_number;
    slot->host_id = host_id;
    return slot;
}

static esp32_mquickjs_spi_device_slot_t *spi_alloc_device_slot(esp32_mquickjs_spi_bus_slot_t *bus_slot)
{
    int32_t i;

    if (bus_slot == NULL || !bus_slot->allocated ||
        bus_slot->open_devices >= ESP32_MQUICKJS_SPI_MAX_DEVICES_PER_BUS) {
        return NULL;
    }

    for (i = 0; i < (int32_t)ESP32_MQUICKJS_SPI_DEVICE_SLOT_COUNT; ++i) {
        esp32_mquickjs_spi_device_slot_t *slot = &s_spi_device_slots[i];

        if (slot->allocated) {
            continue;
        }
        spi_init_device_slot(slot, i);
        slot->allocated = true;
        slot->generation = spi_take_generation();
        slot->parent_bus_slot_id = bus_slot->slot_id;
        slot->parent_bus_generation = bus_slot->generation;
        bus_slot->open_devices++;
        return slot;
    }

    return NULL;
}

static int spi_bus_ref_from_object(JSContext *ctx,
                                   JSValue bus_value,
                                   const char *api_name,
                                   esp32_mquickjs_spi_bus_ref_t *out_ref)
{
    const esp32_mquickjs_spi_bus_ref_t *bus_ref;

    if (out_ref == NULL || JS_GetClassID(ctx, bus_value) != JS_CLASS_SPI_BUS) {
        JS_ThrowTypeError(ctx, "%s expects a valid SPIBus instance", api_name);
        return -1;
    }
    bus_ref = JS_GetOpaque(ctx, bus_value);
    if (bus_ref == NULL) {
        JS_ThrowTypeError(ctx, "%s expects a valid SPIBus instance", api_name);
        return -1;
    }
    *out_ref = *bus_ref;
    return 0;
}

static int spi_device_ref_from_object(JSContext *ctx,
                                      JSValue device_value,
                                      const char *api_name,
                                      esp32_mquickjs_spi_device_ref_t *out_ref)
{
    const esp32_mquickjs_spi_device_ref_t *device_ref;

    if (out_ref == NULL || JS_GetClassID(ctx, device_value) != JS_CLASS_SPI_DEVICE) {
        JS_ThrowTypeError(ctx, "%s expects a valid SPIDevice instance", api_name);
        return -1;
    }
    device_ref = JS_GetOpaque(ctx, device_value);
    if (device_ref == NULL) {
        JS_ThrowTypeError(ctx, "%s expects a valid SPIDevice instance", api_name);
        return -1;
    }
    *out_ref = *device_ref;
    return 0;
}

static int spi_get_this_bus_slot(JSContext *ctx,
                                 JSValue this_value,
                                 const char *api_name,
                                 esp32_mquickjs_spi_bus_ref_t *out_ref,
                                 esp32_mquickjs_spi_bus_slot_t **out_slot)
{
    esp32_mquickjs_spi_bus_slot_t *slot;

    if (spi_bus_ref_from_object(ctx, this_value, api_name, out_ref) != 0) {
        return -1;
    }
    slot = spi_get_bus_slot(out_ref);
    if (slot == NULL) {
        JS_ThrowReferenceError(ctx, "%s failed because the SPI bus is closed", api_name);
        return -1;
    }
    if (out_slot != NULL) {
        *out_slot = slot;
    }
    return 0;
}

static int spi_get_this_device_slots(JSContext *ctx,
                                     JSValue this_value,
                                     const char *api_name,
                                     esp32_mquickjs_spi_device_ref_t *out_ref,
                                     esp32_mquickjs_spi_device_slot_t **out_device_slot,
                                     esp32_mquickjs_spi_bus_slot_t **out_bus_slot)
{
    esp32_mquickjs_spi_device_slot_t *device_slot;
    esp32_mquickjs_spi_bus_slot_t *bus_slot;

    if (spi_device_ref_from_object(ctx, this_value, api_name, out_ref) != 0) {
        return -1;
    }
    device_slot = spi_get_device_slot(out_ref);
    if (device_slot == NULL) {
        JS_ThrowReferenceError(ctx, "%s failed because the SPI device is closed", api_name);
        return -1;
    }
    bus_slot = spi_get_bus_slot_by_ids(device_slot->parent_bus_slot_id, device_slot->parent_bus_generation);
    if (bus_slot == NULL) {
        JS_ThrowReferenceError(ctx, "%s failed because the parent SPI bus is closed", api_name);
        return -1;
    }
    if (out_device_slot != NULL) {
        *out_device_slot = device_slot;
    }
    if (out_bus_slot != NULL) {
        *out_bus_slot = bus_slot;
    }
    return 0;
}

static JSValue spi_make_bus_status_object(JSContext *ctx, const esp32_mquickjs_spi_bus_slot_t *slot)
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
        !esp32_mquickjs_set_property(ctx, *status_obj, "host",
                                     JS_NewInt32(ctx, slot != NULL ? slot->host_number : ESP32_MQUICKJS_SPI_DEFAULT_HOST_NUMBER)) ||
        !esp32_mquickjs_set_property(ctx, *status_obj, "sclk",
                                     JS_NewInt32(ctx, slot != NULL ? slot->sclk_pin : ESP32_MQUICKJS_SPI_DEFAULT_SCLK_PIN)) ||
        !esp32_mquickjs_set_property(ctx, *status_obj, "mosi",
                                     JS_NewInt32(ctx, slot != NULL ? slot->mosi_pin : ESP32_MQUICKJS_SPI_DEFAULT_MOSI_PIN)) ||
        !esp32_mquickjs_set_property(ctx, *status_obj, "miso",
                                     JS_NewInt32(ctx, slot != NULL ? slot->miso_pin : ESP32_MQUICKJS_SPI_DEFAULT_MISO_PIN)) ||
        !esp32_mquickjs_set_property(ctx, *status_obj, "maxTransferSize",
                                     JS_NewUint32(ctx, slot != NULL ? slot->max_transfer_size
                                                                    : ESP32_MQUICKJS_SPI_DEFAULT_MAX_TRANSFER_SIZE)) ||
        !esp32_mquickjs_set_property(ctx, *status_obj, "deviceCount",
                                     JS_NewUint32(ctx, slot != NULL ? slot->open_devices : 0))) {
        JS_PopGCRef(ctx, &status_ref);
        return JS_EXCEPTION;
    }

    return JS_PopGCRef(ctx, &status_ref);
}

static JSValue spi_make_device_status_object(JSContext *ctx,
                                             const esp32_mquickjs_spi_device_slot_t *device_slot,
                                             const esp32_mquickjs_spi_bus_slot_t *bus_slot)
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
                                     JS_NewBool(device_slot != NULL && device_slot->allocated)) ||
        !esp32_mquickjs_set_property(ctx, *status_obj, "host",
                                     JS_NewInt32(ctx, bus_slot != NULL ? bus_slot->host_number : ESP32_MQUICKJS_SPI_DEFAULT_HOST_NUMBER)) ||
        !esp32_mquickjs_set_property(ctx, *status_obj, "cs",
                                     JS_NewInt32(ctx, device_slot != NULL ? device_slot->cs_pin : ESP32_MQUICKJS_SPI_DEFAULT_CS_PIN)) ||
        !esp32_mquickjs_set_property(ctx, *status_obj, "mode",
                                     JS_NewInt32(ctx, device_slot != NULL ? device_slot->mode : 0)) ||
        !esp32_mquickjs_set_property(ctx, *status_obj, "freqHz",
                                     JS_NewUint32(ctx, device_slot != NULL ? device_slot->freq_hz
                                                                           : ESP32_MQUICKJS_SPI_DEFAULT_FREQ_HZ)) ||
        !esp32_mquickjs_set_property(ctx, *status_obj, "queueSize",
                                     JS_NewUint32(ctx, device_slot != NULL ? device_slot->queue_size
                                                                           : ESP32_MQUICKJS_SPI_DEFAULT_QUEUE_SIZE)) ||
        !esp32_mquickjs_set_property(ctx, *status_obj, "csHigh",
                                     JS_NewBool(device_slot != NULL && device_slot->cs_high)) ||
        !esp32_mquickjs_set_property(ctx, *status_obj, "lsbFirst",
                                     JS_NewBool(device_slot != NULL && device_slot->lsb_first))) {
        JS_PopGCRef(ctx, &status_ref);
        return JS_EXCEPTION;
    }

    return JS_PopGCRef(ctx, &status_ref);
}

static JSValue spi_make_bus_object(JSContext *ctx, const esp32_mquickjs_spi_bus_slot_t *slot)
{
    JSGCRef bus_ref;
    JSValue *bus_obj;
    esp32_mquickjs_spi_bus_ref_t *bus_data;

    bus_obj = JS_PushGCRef(ctx, &bus_ref);
    *bus_obj = JS_NewObjectClassUser(ctx, JS_CLASS_SPI_BUS);
    if (JS_IsException(*bus_obj) || slot == NULL) {
        JS_PopGCRef(ctx, &bus_ref);
        return JS_EXCEPTION;
    }

    bus_data = heap_caps_malloc(sizeof(*bus_data), MALLOC_CAP_8BIT);
    if (bus_data == NULL) {
        JS_PopGCRef(ctx, &bus_ref);
        return JS_ThrowOutOfMemory(ctx);
    }
    bus_data->slot_id = slot->slot_id;
    bus_data->generation = slot->generation;
    JS_SetOpaque(ctx, *bus_obj, bus_data);

    return JS_PopGCRef(ctx, &bus_ref);
}

static JSValue spi_make_device_object(JSContext *ctx, const esp32_mquickjs_spi_device_slot_t *slot)
{
    JSGCRef device_ref;
    JSValue *device_obj;
    esp32_mquickjs_spi_device_ref_t *device_data;

    device_obj = JS_PushGCRef(ctx, &device_ref);
    *device_obj = JS_NewObjectClassUser(ctx, JS_CLASS_SPI_DEVICE);
    if (JS_IsException(*device_obj) || slot == NULL) {
        JS_PopGCRef(ctx, &device_ref);
        return JS_EXCEPTION;
    }

    device_data = heap_caps_malloc(sizeof(*device_data), MALLOC_CAP_8BIT);
    if (device_data == NULL) {
        JS_PopGCRef(ctx, &device_ref);
        return JS_ThrowOutOfMemory(ctx);
    }
    device_data->slot_id = slot->slot_id;
    device_data->generation = slot->generation;
    JS_SetOpaque(ctx, *device_obj, device_data);

    return JS_PopGCRef(ctx, &device_ref);
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
        if (JS_IsException(*item) || !js_value_to_u32(ctx, *item, &raw_byte) || raw_byte > 0xffU) {
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

static JSValue spi_transmit(JSContext *ctx,
                            const esp32_mquickjs_spi_bus_slot_t *bus_slot,
                            const esp32_mquickjs_spi_device_slot_t *device_slot,
                            const uint8_t *tx_bytes,
                            size_t length,
                            bool expect_rx)
{
    spi_transaction_t transaction = {0};
    uint8_t *rx_bytes = NULL;
    JSValue result;
    esp_err_t err;

    if (length == 0) {
        return expect_rx ? JS_NewArray(ctx, 0) : JS_NewInt32(ctx, 0);
    }
    if (bus_slot != NULL && length > bus_slot->max_transfer_size) {
        return JS_ThrowRangeError(ctx,
                                  "SPI transfer length (%u) exceeds SPIBus maxTransferSize (%u)",
                                  (unsigned)length,
                                  (unsigned)bus_slot->max_transfer_size);
    }

    if (expect_rx) {
        rx_bytes = heap_caps_malloc(length, MALLOC_CAP_8BIT);
        if (rx_bytes == NULL) {
            return JS_ThrowOutOfMemory(ctx);
        }
    }

    transaction.length = length * 8U;
    transaction.rxlength = expect_rx ? (length * 8U) : 0U;
    transaction.tx_buffer = tx_bytes;
    transaction.rx_buffer = rx_bytes;

    err = spi_device_transmit(device_slot->handle, &transaction);
    if (err != ESP_OK) {
        heap_caps_free(rx_bytes);
        return spi_throw_error(ctx, err, "SPI transaction failed");
    }

    if (!expect_rx) {
        heap_caps_free(rx_bytes);
        return JS_NewInt32(ctx, (int32_t)length);
    }

    result = js_bytes_to_array(ctx, rx_bytes, length);
    heap_caps_free(rx_bytes);
    return result;
}

static JSValue spi_open_bus(JSContext *ctx, int argc, JSValue *argv)
{
    int32_t host_number = ESP32_MQUICKJS_SPI_DEFAULT_HOST_NUMBER;
    int32_t sclk_pin = ESP32_MQUICKJS_SPI_DEFAULT_SCLK_PIN;
    int32_t mosi_pin = ESP32_MQUICKJS_SPI_DEFAULT_MOSI_PIN;
    int32_t miso_pin = ESP32_MQUICKJS_SPI_DEFAULT_MISO_PIN;
    uint32_t max_transfer_size = ESP32_MQUICKJS_SPI_DEFAULT_MAX_TRANSFER_SIZE;
    spi_bus_config_t bus_config = {0};
    esp32_mquickjs_spi_bus_slot_t *slot;
    JSValue result;
    esp_err_t err;

    if (argc >= 1 && !JS_IsUndefined(argv[0])) {
        JSGCRef property_ref;
        JSValue *property;

        if (JS_GetClassID(ctx, argv[0]) < 0) {
            return JS_ThrowTypeError(ctx,
                                     "spi.openBus(options?) expects an object with host?/sclk?/mosi?/miso?/maxTransferSize?");
        }

        property = JS_PushGCRef(ctx, &property_ref);

        *property = JS_GetPropertyStr(ctx, argv[0], "host");
        if (JS_IsException(*property)) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_EXCEPTION;
        }
        if (!JS_IsUndefined(*property) && !js_value_to_spi_host_number(ctx, *property, &host_number)) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_ThrowTypeError(ctx, "spi.openBus({ host }) expects spi.HOST_2 or spi.HOST_3");
        }

        *property = JS_GetPropertyStr(ctx, argv[0], "sclk");
        if (JS_IsException(*property)) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_EXCEPTION;
        }
        if (!JS_IsUndefined(*property) && !js_value_to_gpio_num(ctx, *property, &sclk_pin, false)) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_ThrowTypeError(ctx, "spi.openBus({ sclk }) expects a valid GPIO");
        }

        *property = JS_GetPropertyStr(ctx, argv[0], "mosi");
        if (JS_IsException(*property)) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_EXCEPTION;
        }
        if (!JS_IsUndefined(*property) && !js_value_to_gpio_num(ctx, *property, &mosi_pin, true)) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_ThrowTypeError(ctx, "spi.openBus({ mosi }) expects a valid GPIO or -1");
        }

        *property = JS_GetPropertyStr(ctx, argv[0], "miso");
        if (JS_IsException(*property)) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_EXCEPTION;
        }
        if (!JS_IsUndefined(*property) && !js_value_to_gpio_num(ctx, *property, &miso_pin, true)) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_ThrowTypeError(ctx, "spi.openBus({ miso }) expects a valid GPIO or -1");
        }

        *property = JS_GetPropertyStr(ctx, argv[0], "maxTransferSize");
        if (JS_IsException(*property)) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_EXCEPTION;
        }
        if (!JS_IsUndefined(*property) &&
            (!js_value_to_u32(ctx, *property, &max_transfer_size) || max_transfer_size == 0U)) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_ThrowTypeError(ctx, "spi.openBus({ maxTransferSize }) expects a positive integer");
        }

        JS_PopGCRef(ctx, &property_ref);
    }

    if (!GPIO_IS_VALID_GPIO(sclk_pin)) {
        return JS_ThrowTypeError(ctx, "spi.openBus() requires a valid default SCLK GPIO or an explicit { sclk } override");
    }

    slot = spi_alloc_bus_slot(host_number);
    if (slot == NULL) {
        return JS_ThrowInternalError(ctx, "spi.openBus() failed: selected SPI host is unavailable or already in use");
    }

    bus_config.mosi_io_num = mosi_pin;
    bus_config.miso_io_num = miso_pin;
    bus_config.sclk_io_num = sclk_pin;
    bus_config.quadwp_io_num = -1;
    bus_config.quadhd_io_num = -1;
    bus_config.data4_io_num = -1;
    bus_config.data5_io_num = -1;
    bus_config.data6_io_num = -1;
    bus_config.data7_io_num = -1;
    bus_config.max_transfer_sz = (int)max_transfer_size;
    bus_config.flags = SPICOMMON_BUSFLAG_MASTER | SPICOMMON_BUSFLAG_SCLK;
    if (mosi_pin >= 0) {
        bus_config.flags |= SPICOMMON_BUSFLAG_MOSI;
    }
    if (miso_pin >= 0) {
        bus_config.flags |= SPICOMMON_BUSFLAG_MISO;
    }

    err = spi_bus_initialize(slot->host_id, &bus_config, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        spi_cleanup_bus_slot(slot);
        return spi_throw_error(ctx, err, "spi.openBus() failed");
    }

    slot->sclk_pin = sclk_pin;
    slot->mosi_pin = mosi_pin;
    slot->miso_pin = miso_pin;
    slot->max_transfer_size = max_transfer_size;

    result = spi_make_bus_object(ctx, slot);
    if (JS_IsException(result)) {
        spi_cleanup_bus_slot(slot);
    }
    return result;
}

void esp32_mquickjs_init_spi_runtime(void)
{
    int32_t i;

    for (i = 0; i < (int32_t)ESP32_MQUICKJS_SPI_BUS_SLOT_COUNT; ++i) {
        if (s_spi_bus_slots[i].allocated) {
            spi_cleanup_bus_slot(&s_spi_bus_slots[i]);
        }
    }
    for (i = 0; i < (int32_t)ESP32_MQUICKJS_SPI_DEVICE_SLOT_COUNT; ++i) {
        if (s_spi_device_slots[i].allocated) {
            spi_cleanup_device_slot(&s_spi_device_slots[i]);
        }
    }
    spi_reset_slots();
}

JSValue js_spi_bus_constructor(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_ThrowTypeError(ctx, "SPIBus cannot be constructed directly");
}

void js_spi_bus_finalizer(JSContext *ctx, void *opaque)
{
    esp32_mquickjs_spi_bus_ref_t *bus_ref = opaque;
    esp32_mquickjs_spi_bus_slot_t *slot;

    (void)ctx;

    if (bus_ref == NULL) {
        return;
    }

    slot = spi_get_bus_slot(bus_ref);
    if (slot != NULL) {
        spi_cleanup_bus_slot(slot);
    }
    heap_caps_free(bus_ref);
}

JSValue js_spi_bus_close(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    esp32_mquickjs_spi_bus_ref_t bus_ref;
    esp32_mquickjs_spi_bus_ref_t *bus_ref_ptr;
    esp32_mquickjs_spi_bus_slot_t *slot;

    (void)argc;
    (void)argv;

    if (spi_bus_ref_from_object(ctx, *this_val, "SPIBus.close()", &bus_ref) != 0) {
        return JS_EXCEPTION;
    }
    slot = spi_get_bus_slot(&bus_ref);
    if (slot != NULL) {
        spi_cleanup_bus_slot(slot);
    }
    bus_ref_ptr = JS_GetOpaque(ctx, *this_val);
    if (bus_ref_ptr != NULL) {
        bus_ref_ptr->slot_id = -1;
        bus_ref_ptr->generation = 0;
    }
    return JS_TRUE;
}

JSValue js_spi_bus_status(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    esp32_mquickjs_spi_bus_ref_t bus_ref;
    esp32_mquickjs_spi_bus_slot_t *slot = NULL;

    (void)argc;
    (void)argv;

    if (spi_get_this_bus_slot(ctx, *this_val, "SPIBus.status()", &bus_ref, &slot) != 0) {
        return JS_EXCEPTION;
    }
    return spi_make_bus_status_object(ctx, slot);
}

JSValue js_spi_bus_open_device(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    esp32_mquickjs_spi_bus_ref_t bus_ref;
    esp32_mquickjs_spi_bus_slot_t *bus_slot = NULL;
    esp32_mquickjs_spi_device_slot_t *device_slot;
    spi_device_interface_config_t device_config = {0};
    int32_t cs_pin = ESP32_MQUICKJS_SPI_DEFAULT_CS_PIN;
    uint8_t mode = 0;
    uint32_t freq_hz = ESP32_MQUICKJS_SPI_DEFAULT_FREQ_HZ;
    uint32_t queue_size = ESP32_MQUICKJS_SPI_DEFAULT_QUEUE_SIZE;
    bool cs_high = false;
    bool lsb_first = false;
    JSValue result;
    esp_err_t err;

    if (spi_get_this_bus_slot(ctx, *this_val, "SPIBus.openDevice()", &bus_ref, &bus_slot) != 0) {
        return JS_EXCEPTION;
    }

    if (argc >= 1 && !JS_IsUndefined(argv[0])) {
        JSGCRef property_ref;
        JSValue *property;

        if (JS_GetClassID(ctx, argv[0]) < 0) {
            return JS_ThrowTypeError(ctx,
                                     "SPIBus.openDevice(options?) expects an object with cs?/freqHz?/mode?/queueSize?/csHigh?/lsbFirst?");
        }

        property = JS_PushGCRef(ctx, &property_ref);

        *property = JS_GetPropertyStr(ctx, argv[0], "cs");
        if (JS_IsException(*property)) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_EXCEPTION;
        }
        if (!JS_IsUndefined(*property) && !js_value_to_gpio_num(ctx, *property, &cs_pin, true)) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_ThrowTypeError(ctx, "SPIBus.openDevice({ cs }) expects a valid GPIO or -1");
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
        if (!JS_IsUndefined(*property) && (!js_value_to_u32(ctx, *property, &freq_hz) || freq_hz == 0U)) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_ThrowTypeError(ctx, "SPIBus.openDevice({ freqHz }) expects a positive integer");
        }

        *property = JS_GetPropertyStr(ctx, argv[0], "mode");
        if (JS_IsException(*property)) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_EXCEPTION;
        }
        if (!JS_IsUndefined(*property) && !js_value_to_spi_mode(ctx, *property, &mode)) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_ThrowTypeError(ctx, "SPIBus.openDevice({ mode }) expects 0, 1, 2, or 3");
        }

        *property = JS_GetPropertyStr(ctx, argv[0], "queueSize");
        if (JS_IsException(*property)) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_EXCEPTION;
        }
        if (!JS_IsUndefined(*property) && (!js_value_to_u32(ctx, *property, &queue_size) || queue_size == 0U)) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_ThrowTypeError(ctx, "SPIBus.openDevice({ queueSize }) expects a positive integer");
        }

        *property = JS_GetPropertyStr(ctx, argv[0], "csHigh");
        if (JS_IsException(*property)) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_EXCEPTION;
        }
        if (!JS_IsUndefined(*property) && !js_value_to_bool(ctx, *property, &cs_high)) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_ThrowTypeError(ctx, "SPIBus.openDevice({ csHigh }) expects a boolean-like value");
        }

        *property = JS_GetPropertyStr(ctx, argv[0], "lsbFirst");
        if (JS_IsException(*property)) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_EXCEPTION;
        }
        if (!JS_IsUndefined(*property) && !js_value_to_bool(ctx, *property, &lsb_first)) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_ThrowTypeError(ctx, "SPIBus.openDevice({ lsbFirst }) expects a boolean-like value");
        }

        JS_PopGCRef(ctx, &property_ref);
    }

    device_slot = spi_alloc_device_slot(bus_slot);
    if (device_slot == NULL) {
        return JS_ThrowInternalError(ctx, "SPIBus.openDevice() failed: no available SPIDevice slots");
    }

    device_config.mode = mode;
    device_config.clock_source = SPI_CLK_SRC_DEFAULT;
    device_config.clock_speed_hz = (int)freq_hz;
    device_config.spics_io_num = cs_pin;
    device_config.queue_size = (int)queue_size;
    device_config.flags = 0;
    if (cs_high) {
        device_config.flags |= SPI_DEVICE_POSITIVE_CS;
    }
    if (lsb_first) {
        device_config.flags |= SPI_DEVICE_BIT_LSBFIRST;
    }

    err = spi_bus_add_device(bus_slot->host_id, &device_config, &device_slot->handle);
    if (err != ESP_OK) {
        spi_cleanup_device_slot(device_slot);
        return spi_throw_error(ctx, err, "SPIBus.openDevice() failed");
    }

    device_slot->cs_pin = cs_pin;
    device_slot->mode = mode;
    device_slot->freq_hz = freq_hz;
    device_slot->queue_size = queue_size;
    device_slot->cs_high = cs_high;
    device_slot->lsb_first = lsb_first;

    result = spi_make_device_object(ctx, device_slot);
    if (JS_IsException(result)) {
        spi_cleanup_device_slot(device_slot);
    }
    return result;
}

JSValue js_spi_device_constructor(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_ThrowTypeError(ctx, "SPIDevice cannot be constructed directly");
}

void js_spi_device_finalizer(JSContext *ctx, void *opaque)
{
    esp32_mquickjs_spi_device_ref_t *device_ref = opaque;
    esp32_mquickjs_spi_device_slot_t *slot;

    (void)ctx;

    if (device_ref == NULL) {
        return;
    }

    slot = spi_get_device_slot(device_ref);
    if (slot != NULL) {
        spi_cleanup_device_slot(slot);
    }
    heap_caps_free(device_ref);
}

JSValue js_spi_device_close(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    esp32_mquickjs_spi_device_ref_t device_ref;
    esp32_mquickjs_spi_device_ref_t *device_ref_ptr;
    esp32_mquickjs_spi_device_slot_t *slot;

    (void)argc;
    (void)argv;

    if (spi_device_ref_from_object(ctx, *this_val, "SPIDevice.close()", &device_ref) != 0) {
        return JS_EXCEPTION;
    }
    slot = spi_get_device_slot(&device_ref);
    if (slot != NULL) {
        spi_cleanup_device_slot(slot);
    }
    device_ref_ptr = JS_GetOpaque(ctx, *this_val);
    if (device_ref_ptr != NULL) {
        device_ref_ptr->slot_id = -1;
        device_ref_ptr->generation = 0;
    }
    return JS_TRUE;
}

JSValue js_spi_device_status(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    esp32_mquickjs_spi_device_ref_t device_ref;
    esp32_mquickjs_spi_device_slot_t *device_slot = NULL;
    esp32_mquickjs_spi_bus_slot_t *bus_slot = NULL;

    (void)argc;
    (void)argv;

    if (spi_get_this_device_slots(ctx,
                                  *this_val,
                                  "SPIDevice.status()",
                                  &device_ref,
                                  &device_slot,
                                  &bus_slot) != 0) {
        return JS_EXCEPTION;
    }
    return spi_make_device_status_object(ctx, device_slot, bus_slot);
}

JSValue js_spi_device_transfer(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    esp32_mquickjs_spi_device_ref_t device_ref;
    esp32_mquickjs_spi_device_slot_t *device_slot = NULL;
    esp32_mquickjs_spi_bus_slot_t *bus_slot = NULL;
    uint8_t *bytes = NULL;
    size_t length = 0;
    JSValue error = JS_UNDEFINED;
    JSValue result;

    if (spi_get_this_device_slots(ctx,
                                  *this_val,
                                  "SPIDevice.transfer()",
                                  &device_ref,
                                  &device_slot,
                                  &bus_slot) != 0) {
        return JS_EXCEPTION;
    }
    if (argc < 1 ||
        !js_value_to_byte_array(ctx, argv[0], "SPIDevice.transfer(data)", &bytes, &length, &error)) {
        return JS_IsUndefined(error)
                   ? JS_ThrowTypeError(ctx, "SPIDevice.transfer(data) expects an array-like byte sequence")
                   : error;
    }

    result = spi_transmit(ctx, bus_slot, device_slot, bytes, length, true);
    heap_caps_free(bytes);
    return result;
}

JSValue js_spi_device_write(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    esp32_mquickjs_spi_device_ref_t device_ref;
    esp32_mquickjs_spi_device_slot_t *device_slot = NULL;
    esp32_mquickjs_spi_bus_slot_t *bus_slot = NULL;
    uint8_t *bytes = NULL;
    size_t length = 0;
    JSValue error = JS_UNDEFINED;
    JSValue result;

    if (spi_get_this_device_slots(ctx,
                                  *this_val,
                                  "SPIDevice.write()",
                                  &device_ref,
                                  &device_slot,
                                  &bus_slot) != 0) {
        return JS_EXCEPTION;
    }
    if (argc < 1 ||
        !js_value_to_byte_array(ctx, argv[0], "SPIDevice.write(data)", &bytes, &length, &error)) {
        return JS_IsUndefined(error)
                   ? JS_ThrowTypeError(ctx, "SPIDevice.write(data) expects an array-like byte sequence")
                   : error;
    }

    result = spi_transmit(ctx, bus_slot, device_slot, bytes, length, false);
    heap_caps_free(bytes);
    return result;
}

JSValue js_spi_device_read(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    esp32_mquickjs_spi_device_ref_t device_ref;
    esp32_mquickjs_spi_device_slot_t *device_slot = NULL;
    esp32_mquickjs_spi_bus_slot_t *bus_slot = NULL;
    uint32_t length = 0;
    uint32_t fill_byte = 0;
    uint8_t *tx_bytes;
    JSValue result;

    if (spi_get_this_device_slots(ctx,
                                  *this_val,
                                  "SPIDevice.read()",
                                  &device_ref,
                                  &device_slot,
                                  &bus_slot) != 0) {
        return JS_EXCEPTION;
    }
    if (argc < 1 || !js_value_to_u32(ctx, argv[0], &length)) {
        return JS_ThrowTypeError(ctx, "SPIDevice.read(length, fillByte?) expects a byte length");
    }
    if (argc >= 2 && !JS_IsUndefined(argv[1]) &&
        (!js_value_to_u32(ctx, argv[1], &fill_byte) || fill_byte > 0xffU)) {
        return JS_ThrowTypeError(ctx, "SPIDevice.read(length, fillByte?) expects fillByte in the range 0-255");
    }
    if (length == 0) {
        return JS_NewArray(ctx, 0);
    }

    tx_bytes = heap_caps_malloc(length, MALLOC_CAP_8BIT);
    if (tx_bytes == NULL) {
        return JS_ThrowOutOfMemory(ctx);
    }
    memset(tx_bytes, (int)fill_byte, length);

    result = spi_transmit(ctx, bus_slot, device_slot, tx_bytes, length, true);
    heap_caps_free(tx_bytes);
    return result;
}

JSValue js_spi_open_bus(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    return spi_open_bus(ctx, argc, argv);
}

JSValue js_spi_get_host_2(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_NewInt32(ctx, 2);
}

JSValue js_spi_get_host_3(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_NewInt32(ctx, 3);
}

JSValue js_spi_get_default_host(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_NewInt32(ctx, ESP32_MQUICKJS_SPI_DEFAULT_HOST_NUMBER);
}

JSValue js_spi_get_default_sclk(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_NewInt32(ctx, ESP32_MQUICKJS_SPI_DEFAULT_SCLK_PIN);
}

JSValue js_spi_get_default_mosi(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_NewInt32(ctx, ESP32_MQUICKJS_SPI_DEFAULT_MOSI_PIN);
}

JSValue js_spi_get_default_miso(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_NewInt32(ctx, ESP32_MQUICKJS_SPI_DEFAULT_MISO_PIN);
}

JSValue js_spi_get_default_cs(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_NewInt32(ctx, ESP32_MQUICKJS_SPI_DEFAULT_CS_PIN);
}

JSValue js_spi_get_default_freq_hz(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_NewUint32(ctx, ESP32_MQUICKJS_SPI_DEFAULT_FREQ_HZ);
}

JSValue js_spi_get_default_queue_size(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_NewUint32(ctx, ESP32_MQUICKJS_SPI_DEFAULT_QUEUE_SIZE);
}

JSValue js_spi_get_default_max_transfer_size(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_NewUint32(ctx, ESP32_MQUICKJS_SPI_DEFAULT_MAX_TRANSFER_SIZE);
}

#endif
