#include "esp32_mquickjs_spi.h"
#include "esp32_mquickjs_memory.h"

#if CONFIG_ESP32_MQUICKJS_FEATURE_SPI

#include "utils/esp32_mquickjs_byte_source.h"
#include "esp32_mquickjs_core.h"
#include "esp32_mquickjs_future.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_memory_utils.h"
#include "esp_timer.h"
#include "soc/soc_caps.h"

#define ESP32_MQUICKJS_SPI_DEFAULT_HOST_NUMBER \
    esp32_mquickjs_profile_int_or("ESP32QJS_SPI_HOST", 2)
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
    uint16_t future_reservations;
    bool release_pending;
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
    bool busy;
    uint16_t future_reservations;
    bool release_pending;
    spi_device_handle_t handle;
    uint8_t *tx_dma_buffers[2];
    size_t tx_dma_capacities[2];
} esp32_mquickjs_spi_device_slot_t;

static esp32_mquickjs_spi_bus_slot_t s_spi_bus_slots[ESP32_MQUICKJS_SPI_BUS_SLOT_COUNT];
static esp32_mquickjs_spi_device_slot_t s_spi_device_slots[ESP32_MQUICKJS_SPI_DEVICE_SLOT_COUNT];
static uint32_t s_spi_next_generation = 1;

static bool spi_register_future_drivers(JSContext *ctx,
                                        esp32_mquickjs_runtime_t *runtime);

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

static void spi_free_tx_dma_buffers(esp32_mquickjs_spi_device_slot_t *slot)
{
    if (slot == NULL) {
        return;
    }
    heap_caps_free(slot->tx_dma_buffers[0]);
    heap_caps_free(slot->tx_dma_buffers[1]);
    slot->tx_dma_buffers[0] = NULL;
    slot->tx_dma_buffers[1] = NULL;
    slot->tx_dma_capacities[0] = 0;
    slot->tx_dma_capacities[1] = 0;
}

static void spi_cleanup_device_slot(esp32_mquickjs_spi_device_slot_t *slot)
{
    esp32_mquickjs_spi_bus_slot_t *parent;
    int32_t slot_id;

    if (slot == NULL || !slot->allocated || slot->busy ||
        slot->future_reservations > 0) {
        return;
    }

    slot_id = slot->slot_id;
    if (slot->handle != NULL) {
        spi_bus_remove_device(slot->handle);
    }
    spi_free_tx_dma_buffers(slot);
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

    if (slot == NULL || !slot->allocated ||
        slot->future_reservations > 0) {
        return;
    }

    slot_id = slot->slot_id;
    for (i = 0; i < (int32_t)ESP32_MQUICKJS_SPI_DEVICE_SLOT_COUNT; ++i) {
        esp32_mquickjs_spi_device_slot_t *device_slot = &s_spi_device_slots[i];

        if (!device_slot->allocated || device_slot->parent_bus_slot_id != slot->slot_id ||
            device_slot->parent_bus_generation != slot->generation) {
            continue;
        }
        if (device_slot->busy || device_slot->future_reservations > 0) {
            return;
        }
    }
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

    if (!esp32_mquickjs_set_property_ref(ctx, status_obj, "opened",
                                     JS_NewBool(slot != NULL && slot->allocated)) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "host",
                                     JS_NewInt32(ctx, slot != NULL ? slot->host_number : ESP32_MQUICKJS_SPI_DEFAULT_HOST_NUMBER)) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "sclk",
                                     JS_NewInt32(ctx, slot != NULL ? slot->sclk_pin : ESP32_MQUICKJS_SPI_DEFAULT_SCLK_PIN)) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "mosi",
                                     JS_NewInt32(ctx, slot != NULL ? slot->mosi_pin : ESP32_MQUICKJS_SPI_DEFAULT_MOSI_PIN)) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "miso",
                                     JS_NewInt32(ctx, slot != NULL ? slot->miso_pin : ESP32_MQUICKJS_SPI_DEFAULT_MISO_PIN)) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "maxTransferSize",
                                     JS_NewUint32(ctx, slot != NULL ? slot->max_transfer_size
                                                                    : ESP32_MQUICKJS_SPI_DEFAULT_MAX_TRANSFER_SIZE)) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "deviceCount",
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

    if (!esp32_mquickjs_set_property_ref(ctx, status_obj, "opened",
                                     JS_NewBool(device_slot != NULL && device_slot->allocated)) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "host",
                                     JS_NewInt32(ctx, bus_slot != NULL ? bus_slot->host_number : ESP32_MQUICKJS_SPI_DEFAULT_HOST_NUMBER)) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "cs",
                                     JS_NewInt32(ctx, device_slot != NULL ? device_slot->cs_pin : ESP32_MQUICKJS_SPI_DEFAULT_CS_PIN)) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "mode",
                                     JS_NewInt32(ctx, device_slot != NULL ? device_slot->mode : 0)) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "freqHz",
                                     JS_NewUint32(ctx, device_slot != NULL ? device_slot->freq_hz
                                                                           : ESP32_MQUICKJS_SPI_DEFAULT_FREQ_HZ)) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "queueSize",
                                     JS_NewUint32(ctx, device_slot != NULL ? device_slot->queue_size
                                                                           : ESP32_MQUICKJS_SPI_DEFAULT_QUEUE_SIZE)) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "csHigh",
                                     JS_NewBool(device_slot != NULL && device_slot->cs_high)) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "lsbFirst",
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

typedef struct {
    uint32_t queue_depth;
} spi_write_chunks_options_t;

static bool spi_parse_write_chunks_options(JSContext *ctx,
                                           JSValue options,
                                           const char *api_name,
                                           spi_write_chunks_options_t *out_options)
{
    out_options->queue_depth = 2;

    if (JS_IsUndefined(options) || JS_IsNull(options)) {
        return true;
    }
    if (JS_GetClassID(ctx, options) < 0) {
        JS_ThrowTypeError(ctx, "%s options must be an object", api_name);
        return false;
    }

    {
        JSGCRef property_ref;
        JSValue *property = JS_PushGCRef(ctx, &property_ref);

        *property = JS_GetPropertyStr(ctx, options, "queueDepth");
        if (JS_IsException(*property)) {
            JS_PopGCRef(ctx, &property_ref);
            return false;
        }
        if (!JS_IsUndefined(*property) && !JS_IsNull(*property) &&
            (!js_value_to_u32(ctx, *property, &out_options->queue_depth) || out_options->queue_depth == 0U)) {
            JS_PopGCRef(ctx, &property_ref);
            JS_ThrowTypeError(ctx, "%s option 'queueDepth' expects a positive integer", api_name);
            return false;
        }
        JS_PopGCRef(ctx, &property_ref);
    }
    return true;
}

static uint32_t spi_clamp_queue_depth(const esp32_mquickjs_spi_device_slot_t *device_slot, uint32_t requested_depth)
{
    uint32_t depth = requested_depth;

    if (device_slot != NULL && depth > device_slot->queue_size) {
        depth = device_slot->queue_size;
    }
    if (depth > 2U) {
        depth = 2U;
    }
    if (depth < 1U) {
        depth = 1U;
    }
    return depth;
}

static bool spi_byte_span_can_dma(const esp32_mquickjs_byte_span_t *span)
{
    if (span == NULL || span->length == 0) {
        return true;
    }
    return span->dma_capable &&
           span->data != NULL &&
           esp_ptr_dma_capable(span->data) &&
           (((uintptr_t)span->data) & 3U) == 0U &&
           (span->length & 3U) == 0U;
}

static void spi_mark_external_dma(spi_transaction_t *transaction)
{
#if SOC_PSRAM_DMA_CAPABLE
    if (transaction != NULL &&
        (esp_ptr_external_ram(transaction->tx_buffer) ||
         esp_ptr_external_ram(transaction->rx_buffer))) {
        transaction->flags |= SPI_TRANS_DMA_USE_PSRAM;
    }
#else
    (void)transaction;
#endif
}

typedef struct {
    JSGCRef owner_ref;
    bool rooted;
} spi_span_owner_root_t;

static void spi_root_span_owner(JSContext *ctx, spi_span_owner_root_t *root, JSValue owner)
{
    JSValue *owner_value;

    if (root == NULL || JS_IsUndefined(owner)) {
        return;
    }
    owner_value = JS_AddGCRef(ctx, &root->owner_ref);
    *owner_value = owner;
    root->rooted = true;
}

static void spi_release_span_owner(JSContext *ctx, spi_span_owner_root_t *root)
{
    if (root == NULL || !root->rooted) {
        return;
    }
    JS_DeleteGCRef(ctx, &root->owner_ref);
    root->rooted = false;
}

static bool spi_ensure_tx_dma_buffer(esp32_mquickjs_spi_device_slot_t *device_slot,
                                     uint32_t slot_index,
                                     size_t length)
{
    uint8_t *buffer;

    if (device_slot == NULL || slot_index >= 2U) {
        return false;
    }
    if (device_slot->tx_dma_buffers[slot_index] != NULL &&
        device_slot->tx_dma_capacities[slot_index] >= length) {
        return true;
    }

    buffer = esp32_mquickjs_memory_payload_alloc(
        length, ESP32_MQUICKJS_MEMORY_DMA_EXTERNAL);
    if (buffer == NULL) {
        return false;
    }
    heap_caps_free(device_slot->tx_dma_buffers[slot_index]);
    device_slot->tx_dma_buffers[slot_index] = buffer;
    device_slot->tx_dma_capacities[slot_index] = length;
    return true;
}

static JSValue spi_make_write_chunks_stats(JSContext *ctx,
                                           uint32_t chunks,
                                           size_t bytes,
                                           uint64_t prep_us,
                                           uint64_t queue_us,
                                           uint64_t wait_us,
                                           uint64_t total_us,
                                           uint32_t queue_depth,
                                           bool direct)
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
        !esp32_mquickjs_set_property_ref(ctx, stats, "prepUs", JS_NewInt64(ctx, (int64_t)prep_us)) ||
        !esp32_mquickjs_set_property_ref(ctx, stats, "queueUs", JS_NewInt64(ctx, (int64_t)queue_us)) ||
        !esp32_mquickjs_set_property_ref(ctx, stats, "waitUs", JS_NewInt64(ctx, (int64_t)wait_us)) ||
        !esp32_mquickjs_set_property_ref(ctx, stats, "transferUs",
                                     JS_NewInt64(ctx, (int64_t)(total_us > prep_us ? total_us - prep_us : 0U))) ||
        !esp32_mquickjs_set_property_ref(ctx, stats, "totalUs", JS_NewInt64(ctx, (int64_t)total_us)) ||
        !esp32_mquickjs_set_property_ref(ctx, stats, "queueDepth", JS_NewUint32(ctx, queue_depth)) ||
        !esp32_mquickjs_set_property_ref(ctx, stats, "direct", direct ? JS_TRUE : JS_FALSE)) {
        JS_PopGCRef(ctx, &stats_ref);
        return JS_EXCEPTION;
    }

    return JS_PopGCRef(ctx, &stats_ref);
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

void esp32_mquickjs_deinit_spi_runtime(void)
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

bool esp32_mquickjs_init_spi_runtime(JSContext *ctx,
                                     esp32_mquickjs_runtime_t *runtime)
{
    esp32_mquickjs_deinit_spi_runtime();
    return spi_register_future_drivers(ctx, runtime);
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
        slot->release_pending = true;
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
        if (slot->future_reservations > 0) {
            return JS_ThrowInternalError(
                ctx, "SPIBus.close() refused while an operation is pending");
        }
        spi_cleanup_bus_slot(slot);
        if (slot->allocated) {
            return JS_ThrowInternalError(
                ctx, "SPIBus.close() refused while an operation is pending");
        }
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
        slot->release_pending = true;
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
        if (slot->busy || slot->future_reservations > 0) {
            return JS_ThrowInternalError(ctx,
                                         "SPIDevice.close() refused while a transaction is pending");
        }
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

typedef enum {
    SPI_FUTURE_TRANSFER,
    SPI_FUTURE_WRITE,
    SPI_FUTURE_READ,
    SPI_FUTURE_WRITE_CHUNKS,
    SPI_FUTURE_WRITE_SOURCE,
} spi_future_kind_t;

struct esp32_mquickjs_future_driver_state {
    spi_future_kind_t kind;
    JSContext *ctx;
    JSGCRef owner_ref;
    esp32_mquickjs_spi_device_ref_t device_ref;
    esp32_mquickjs_runtime_t *runtime;
    esp32_mquickjs_future_token_t token;
    esp_timer_handle_t poll_timer;
    spi_transaction_t transaction;
    spi_transaction_t bulk_transactions[2];
    spi_span_owner_root_t bulk_owners[2];
    esp32_mquickjs_byte_span_source_t span_source;
    esp32_mquickjs_byte_span_t pending_span;
    JSGCRef source_error_ref;
    uint8_t *tx_data;
    uint8_t *rx_data;
    uint32_t *chunk_lengths;
    size_t length;
    size_t chunk_offset;
    size_t bytes_written;
    uint32_t chunk_count;
    uint32_t next_chunk;
    uint32_t chunks_written;
    uint32_t queue_depth;
    uint32_t queue_head;
    uint32_t queue_tail;
    uint32_t in_flight;
    uint64_t prep_us;
    uint64_t queue_us;
    uint64_t total_us;
    int64_t total_start_us;
    esp_err_t err;
    bool owner_retained;
    bool device_reserved;
    bool bus_reserved;
    bool span_source_opened;
    bool pending_span_ready;
    bool source_done;
    bool source_error_retained;
    bool direct;
    bool started;
    bool queued;
    _Atomic bool completed;
    bool cancelled;
};

static void spi_future_release(esp32_mquickjs_future_driver_state_t *state)
{
    esp32_mquickjs_spi_device_slot_t *device;
    esp32_mquickjs_spi_bus_slot_t *bus;
    int32_t index;

    if (state == NULL) {
        return;
    }
    if (state->poll_timer != NULL) {
        (void)esp_timer_stop(state->poll_timer);
        (void)esp_timer_delete(state->poll_timer);
    }
    for (index = 0; index < 2; ++index) {
        spi_release_span_owner(state->ctx, &state->bulk_owners[index]);
    }
    if (state->span_source_opened) {
        esp32_mquickjs_byte_span_source_close(state->ctx,
                                               &state->span_source);
        state->span_source_opened = false;
    }
    if (state->source_error_retained) {
        JS_DeleteGCRef(state->ctx, &state->source_error_ref);
        state->source_error_retained = false;
    }
    device = spi_get_device_slot(&state->device_ref);
    bus = device != NULL
        ? spi_get_bus_slot_by_ids(device->parent_bus_slot_id,
                                  device->parent_bus_generation)
        : NULL;
    if (device != NULL && state->device_reserved &&
        device->future_reservations > 0) {
        device->future_reservations--;
    }
    state->device_reserved = false;
    if (bus != NULL && state->bus_reserved &&
        bus->future_reservations > 0) {
        bus->future_reservations--;
    }
    state->bus_reserved = false;
    if (state->owner_retained) {
        JS_DeleteGCRef(state->ctx, &state->owner_ref);
        state->owner_retained = false;
    }
    heap_caps_free(state->tx_data);
    heap_caps_free(state->rx_data);
    heap_caps_free(state->chunk_lengths);
    if (bus != NULL) {
        for (index = 0;
             index < (int32_t)ESP32_MQUICKJS_SPI_DEVICE_SLOT_COUNT;
             ++index) {
            esp32_mquickjs_spi_device_slot_t *candidate =
                &s_spi_device_slots[index];

            if (candidate->allocated && candidate->release_pending &&
                !candidate->busy && candidate->future_reservations == 0 &&
                candidate->parent_bus_slot_id == bus->slot_id &&
                candidate->parent_bus_generation == bus->generation) {
                spi_cleanup_device_slot(candidate);
            }
        }
        if (bus->release_pending && bus->future_reservations == 0) {
            spi_cleanup_bus_slot(bus);
        }
    }
    heap_caps_free(state);
}

static esp32_mquickjs_future_driver_state_t *spi_future_allocate(
    JSContext *ctx,
    JSValue this_value,
    spi_future_kind_t kind)
{
    esp32_mquickjs_future_driver_state_t *state;
    esp32_mquickjs_spi_device_slot_t *device = NULL;
    esp32_mquickjs_spi_bus_slot_t *bus = NULL;
    JSValue *owner;

    state = heap_caps_calloc(1, sizeof(*state), MALLOC_CAP_8BIT);
    if (state == NULL) {
        JS_ThrowOutOfMemory(ctx);
        return NULL;
    }
    if (spi_get_this_device_slots(ctx, this_value, "SPI Future operation",
                                  &state->device_ref, &device, &bus) != 0) {
        heap_caps_free(state);
        return NULL;
    }
    if (device->future_reservations == UINT16_MAX ||
        bus->future_reservations == UINT16_MAX) {
        heap_caps_free(state);
        JS_ThrowInternalError(ctx, "SPI operation reservation limit reached");
        return NULL;
    }
    atomic_init(&state->completed, false);
    state->kind = kind;
    state->ctx = ctx;
    state->direct = true;
    device->future_reservations++;
    bus->future_reservations++;
    state->device_reserved = true;
    state->bus_reserved = true;
    owner = JS_AddGCRef(ctx, &state->owner_ref);
    *owner = this_value;
    state->owner_retained = true;
    return state;
}

static bool spi_future_allocate_buffers(
    JSContext *ctx,
    esp32_mquickjs_future_driver_state_t *state,
    const uint8_t *source,
    size_t length,
    uint8_t fill_byte,
    bool receive)
{
    esp32_mquickjs_spi_device_slot_t *device = spi_get_device_slot(&state->device_ref);
    esp32_mquickjs_spi_bus_slot_t *bus = device != NULL
        ? spi_get_bus_slot_by_ids(device->parent_bus_slot_id,
                                  device->parent_bus_generation)
        : NULL;

    if (device == NULL || bus == NULL) {
        JS_ThrowReferenceError(ctx, "SPI device is closed");
        return false;
    }
    if (length > bus->max_transfer_size) {
        JS_ThrowRangeError(ctx,
                           "SPI transfer length (%u) exceeds SPIBus maxTransferSize (%u)",
                           (unsigned)length,
                           (unsigned)bus->max_transfer_size);
        return false;
    }
    state->length = length;
    if (length == 0) {
        return true;
    }
    state->tx_data = esp32_mquickjs_memory_payload_alloc(
        length, ESP32_MQUICKJS_MEMORY_DMA_EXTERNAL);
    if (state->tx_data == NULL) {
        JS_ThrowOutOfMemory(ctx);
        return false;
    }
    if (source != NULL) {
        memcpy(state->tx_data, source, length);
    } else {
        memset(state->tx_data, fill_byte, length);
    }
    if (receive) {
        state->rx_data = esp32_mquickjs_memory_payload_alloc(
            length, ESP32_MQUICKJS_MEMORY_DMA_EXTERNAL);
        if (state->rx_data == NULL) {
            JS_ThrowOutOfMemory(ctx);
            return false;
        }
    }
    state->transaction.length = length * 8U;
    state->transaction.rxlength = receive ? length * 8U : 0;
    state->transaction.tx_buffer = state->tx_data;
    state->transaction.rx_buffer = state->rx_data;
    spi_mark_external_dma(&state->transaction);
    return true;
}

static bool spi_source_future_prepare(
    JSContext *ctx,
    JSGCRef *this_ref,
    int argc,
    JSGCRef *argv,
    spi_future_kind_t kind,
    esp32_mquickjs_future_driver_state_t **out_state)
{
    esp32_mquickjs_future_driver_state_t *state;
    esp32_mquickjs_byte_source_t source;
    uint8_t *owned = NULL;
    JSValue error = JS_UNDEFINED;
    bool result;

    if (out_state == NULL || argc != 1 ||
        !esp32_mquickjs_get_byte_source(ctx,
                                       argc > 0 ? argv[0].val : JS_UNDEFINED,
                                       kind == SPI_FUTURE_TRANSFER
                                           ? "SPIDevice.transfer(data)"
                                           : "SPIDevice.write(data)",
                                       &source,
                                       &owned,
                                       &error)) {
        if (JS_IsUndefined(error)) {
            JS_ThrowTypeError(ctx, "SPI operation expects an array-like byte sequence");
        }
        return false;
    }
    state = spi_future_allocate(ctx, this_ref->val, kind);
    if (state == NULL) {
        esp32_mquickjs_release_byte_source(owned);
        return false;
    }
    result = spi_future_allocate_buffers(ctx, state,
                                         source.data, source.length, 0,
                                         kind == SPI_FUTURE_TRANSFER);
    esp32_mquickjs_release_byte_source(owned);
    if (!result) {
        spi_future_release(state);
        return false;
    }
    *out_state = state;
    return true;
}

static bool spi_transfer_future_prepare(
    JSContext *ctx, JSGCRef *this_ref, int argc, JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **out_state)
{
    return spi_source_future_prepare(ctx, this_ref, argc, argv,
                                     SPI_FUTURE_TRANSFER, out_state);
}

static bool spi_write_future_prepare(
    JSContext *ctx, JSGCRef *this_ref, int argc, JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **out_state)
{
    return spi_source_future_prepare(ctx, this_ref, argc, argv,
                                     SPI_FUTURE_WRITE, out_state);
}

static bool spi_read_future_prepare(
    JSContext *ctx,
    JSGCRef *this_ref,
    int argc,
    JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **out_state)
{
    esp32_mquickjs_future_driver_state_t *state;
    uint32_t length;
    uint32_t fill = 0;

    if (out_state == NULL || argc < 1 || argc > 2 ||
        !js_value_to_u32(ctx, argv[0].val, &length) ||
        (argc == 2 && !JS_IsUndefined(argv[1].val) &&
         (!js_value_to_u32(ctx, argv[1].val, &fill) || fill > 0xffU))) {
        JS_ThrowTypeError(ctx,
                          "SPIDevice.read(length, fillByte?) expects a byte length and fillByte 0-255");
        return false;
    }
    state = spi_future_allocate(ctx, this_ref->val, SPI_FUTURE_READ);
    if (state == NULL) {
        return false;
    }
    if (!spi_future_allocate_buffers(ctx, state, NULL, length,
                                     (uint8_t)fill, true)) {
        spi_future_release(state);
        return false;
    }
    *out_state = state;
    return true;
}

static bool spi_write_chunks_future_prepare(
    JSContext *ctx,
    JSGCRef *this_ref,
    int argc,
    JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **out_state)
{
    esp32_mquickjs_future_driver_state_t *state;
    esp32_mquickjs_spi_device_slot_t *device;
    esp32_mquickjs_spi_bus_slot_t *bus;
    spi_write_chunks_options_t options;
    JSValue error = JS_UNDEFINED;
    uint32_t index;
    size_t total = 0;

    if (out_state == NULL || argc < 1 || argc > 2) {
        JS_ThrowTypeError(
            ctx,
            "SPIDevice.writeChunks(chunks, options?) expects byte-source chunks");
        return false;
    }
    state = spi_future_allocate(ctx, this_ref->val,
                                SPI_FUTURE_WRITE_CHUNKS);
    if (state == NULL) {
        return false;
    }
    device = spi_get_device_slot(&state->device_ref);
    bus = device != NULL
        ? spi_get_bus_slot_by_ids(device->parent_bus_slot_id,
                                  device->parent_bus_generation)
        : NULL;
    if (bus == NULL ||
        !spi_parse_write_chunks_options(
            ctx, argc == 2 ? argv[1].val : JS_UNDEFINED,
            "SPIDevice.writeChunks()", &options) ||
        !esp32_mquickjs_get_byte_source_array_length(
            ctx, argv[0].val, "SPIDevice.writeChunks(chunks)",
            &state->chunk_count, &error)) {
        if (!JS_HasException(ctx)) {
            JS_ThrowTypeError(
                ctx,
                "SPIDevice.writeChunks(chunks) expects an array-like object");
        }
        spi_future_release(state);
        return false;
    }
    state->queue_depth = spi_clamp_queue_depth(device, options.queue_depth);
    if (state->chunk_count > 0) {
        state->chunk_lengths = heap_caps_calloc(
            state->chunk_count, sizeof(*state->chunk_lengths),
            MALLOC_CAP_8BIT);
        if (state->chunk_lengths == NULL) {
            spi_future_release(state);
            JS_ThrowOutOfMemory(ctx);
            return false;
        }
    }
    for (index = 0; index < state->chunk_count; ++index) {
        esp32_mquickjs_byte_source_chunk_t chunk;
        uint8_t *grown;

        if (!esp32_mquickjs_get_byte_source_chunk(
                ctx, argv[0].val, index,
                "SPIDevice.writeChunks(chunks)", &chunk, &error)) {
            if (!JS_HasException(ctx)) {
                JS_ThrowTypeError(
                    ctx,
                    "SPIDevice.writeChunks(chunks) expects byte-source chunks");
            }
            spi_future_release(state);
            return false;
        }
        if (chunk.source.length > bus->max_transfer_size ||
            chunk.source.length > UINT32_MAX ||
            chunk.source.length > SIZE_MAX - total) {
            size_t chunk_length = chunk.source.length;

            esp32_mquickjs_release_byte_source_chunk(ctx, &chunk);
            spi_future_release(state);
            JS_ThrowRangeError(
                ctx,
                "SPIDevice.writeChunks() chunk length (%u) exceeds SPIBus maxTransferSize (%u)",
                (unsigned)chunk_length,
                (unsigned)bus->max_transfer_size);
            return false;
        }
        state->chunk_lengths[index] = (uint32_t)chunk.source.length;
        if (chunk.source.length > 0) {
            grown = esp32_mquickjs_memory_payload_realloc(
                state->tx_data, total + chunk.source.length,
                ESP32_MQUICKJS_MEMORY_DMA_EXTERNAL);
            if (grown == NULL) {
                esp32_mquickjs_release_byte_source_chunk(ctx, &chunk);
                spi_future_release(state);
                JS_ThrowOutOfMemory(ctx);
                return false;
            }
            state->tx_data = grown;
            memcpy(state->tx_data + total, chunk.source.data,
                   chunk.source.length);
            total += chunk.source.length;
        }
        esp32_mquickjs_release_byte_source_chunk(ctx, &chunk);
    }
    state->length = total;
    *out_state = state;
    return true;
}

static bool spi_write_source_future_prepare(
    JSContext *ctx,
    JSGCRef *this_ref,
    int argc,
    JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **out_state)
{
    esp32_mquickjs_future_driver_state_t *state;
    esp32_mquickjs_spi_device_slot_t *device;
    spi_write_chunks_options_t options;
    JSValue error = JS_UNDEFINED;

    if (out_state == NULL || argc < 1 || argc > 2) {
        JS_ThrowTypeError(
            ctx,
            "SPIDevice.writeSource(source, options?) expects a ByteSpanSource");
        return false;
    }
    state = spi_future_allocate(ctx, this_ref->val,
                                SPI_FUTURE_WRITE_SOURCE);
    if (state == NULL) {
        return false;
    }
    device = spi_get_device_slot(&state->device_ref);
    if (device == NULL ||
        !spi_parse_write_chunks_options(
            ctx, argc == 2 ? argv[1].val : JS_UNDEFINED,
            "SPIDevice.writeSource()", &options) ||
        !esp32_mquickjs_open_byte_span_source(
            ctx, argv[0].val, "SPIDevice.writeSource(source)",
            &state->span_source, &error)) {
        if (!JS_HasException(ctx)) {
            JS_ThrowTypeError(
                ctx,
                "SPIDevice.writeSource(source) expects a ByteSpanSource");
        }
        spi_future_release(state);
        return false;
    }
    state->span_source_opened = true;
    state->queue_depth = spi_clamp_queue_depth(device, options.queue_depth);
    *out_state = state;
    return true;
}

static void spi_future_retain_source_exception(
    esp32_mquickjs_future_driver_state_t *state,
    const char *fallback)
{
    JSValue *error;

    if (state == NULL || state->source_error_retained) {
        return;
    }
    if (!JS_HasException(state->ctx)) {
        (void)JS_ThrowInternalError(state->ctx, "%s", fallback);
    }
    error = JS_AddGCRef(state->ctx, &state->source_error_ref);
    *error = JS_GetException(state->ctx);
    state->source_error_retained = true;
}

static void spi_future_timer(void *opaque)
{
    esp32_mquickjs_future_driver_state_t *state = opaque;

    if (state != NULL &&
        !atomic_load_explicit(&state->completed, memory_order_acquire)) {
        (void)esp32_mquickjs_future_wake(state->runtime, state->token);
    }
}

static bool spi_future_is_bulk(
    const esp32_mquickjs_future_driver_state_t *state)
{
    return state != NULL &&
           (state->kind == SPI_FUTURE_WRITE_CHUNKS ||
            state->kind == SPI_FUTURE_WRITE_SOURCE);
}

static bool spi_future_prepare_pending_span(
    esp32_mquickjs_future_driver_state_t *state)
{
    unsigned empty_spans = 0;

    while (state != NULL && !state->source_done &&
           !state->pending_span_ready) {
        if (!esp32_mquickjs_byte_span_source_next(
                state->ctx, &state->span_source, &state->pending_span)) {
            if (JS_HasException(state->ctx)) {
                spi_future_retain_source_exception(
                    state,
                    "SPIDevice.writeSource() source iteration failed");
            }
            state->source_done = true;
            return false;
        }
        if (state->pending_span.length == 0) {
            if (++empty_spans > 16U) {
                (void)JS_ThrowInternalError(
                    state->ctx,
                    "SPIDevice.writeSource() produced too many empty spans");
                spi_future_retain_source_exception(
                    state,
                    "SPIDevice.writeSource() source iteration failed");
                state->source_done = true;
                return false;
            }
            continue;
        }
        state->pending_span_ready = true;
    }
    return state != NULL && state->pending_span_ready;
}

static void spi_future_bulk_step(
    esp32_mquickjs_future_driver_state_t *state,
    esp32_mquickjs_spi_device_slot_t *device)
{
    esp32_mquickjs_spi_bus_slot_t *bus = device != NULL
        ? spi_get_bus_slot_by_ids(device->parent_bus_slot_id,
                                  device->parent_bus_generation)
        : NULL;
    spi_transaction_t *completed = NULL;

    if (state == NULL || device == NULL || bus == NULL) {
        if (state != NULL) {
            state->err = ESP_ERR_INVALID_STATE;
            atomic_store_explicit(&state->completed, true,
                                  memory_order_release);
        }
        return;
    }

    while (state->in_flight > 0) {
        esp_err_t err = spi_device_get_trans_result(
            device->handle, &completed, 0);

        if (err == ESP_ERR_TIMEOUT) {
            break;
        }
        spi_release_span_owner(state->ctx,
                               &state->bulk_owners[state->queue_head]);
        state->queue_head =
            (state->queue_head + 1U) % state->queue_depth;
        state->in_flight--;
        if (err != ESP_OK && state->err == ESP_OK) {
            state->err = err;
        }
    }

    while (state->err == ESP_OK && !state->cancelled &&
           state->in_flight < state->queue_depth) {
        const uint8_t *data;
        size_t length;
        bool source_direct = false;
        uint32_t slot_index = state->queue_tail;
        spi_transaction_t *transaction =
            &state->bulk_transactions[slot_index];
        int64_t started_us;
        esp_err_t err;

        if (state->kind == SPI_FUTURE_WRITE_CHUNKS) {
            while (state->next_chunk < state->chunk_count &&
                   state->chunk_lengths[state->next_chunk] == 0) {
                state->next_chunk++;
            }
            if (state->next_chunk >= state->chunk_count) {
                state->source_done = true;
                break;
            }
            length = state->chunk_lengths[state->next_chunk];
            data = state->tx_data + state->chunk_offset;
        } else {
            if (!spi_future_prepare_pending_span(state)) {
                break;
            }
            length = state->pending_span.length;
            data = state->pending_span.data;
            if (data == NULL || length > bus->max_transfer_size) {
                if (data == NULL) {
                    (void)JS_ThrowInternalError(
                        state->ctx,
                        "SPIDevice.writeSource() received a non-empty span with null data");
                } else {
                    (void)JS_ThrowRangeError(
                        state->ctx,
                        "SPIDevice.writeSource() span length (%u) exceeds SPIBus maxTransferSize (%u)",
                        (unsigned)length,
                        (unsigned)bus->max_transfer_size);
                }
                spi_future_retain_source_exception(
                    state,
                    "SPIDevice.writeSource() received an invalid span");
                state->source_done = true;
                break;
            }
            source_direct = spi_byte_span_can_dma(&state->pending_span);
        }

        memset(transaction, 0, sizeof(*transaction));
        transaction->length = length * 8U;
        if ((state->kind == SPI_FUTURE_WRITE_SOURCE && source_direct) ||
            (state->kind == SPI_FUTURE_WRITE_CHUNKS &&
             data != NULL && esp_ptr_dma_capable(data) &&
             (((uintptr_t)data) & 3U) == 0U && (length & 3U) == 0U)) {
            transaction->tx_buffer = data;
        } else {
            started_us = esp_timer_get_time();
            if (!spi_ensure_tx_dma_buffer(device, slot_index, length)) {
                state->err = ESP_ERR_NO_MEM;
                break;
            }
            memcpy(device->tx_dma_buffers[slot_index], data, length);
            state->prep_us +=
                (uint64_t)(esp_timer_get_time() - started_us);
            transaction->tx_buffer = device->tx_dma_buffers[slot_index];
            state->direct = false;
        }
        spi_mark_external_dma(transaction);
        started_us = esp_timer_get_time();
        err = spi_device_queue_trans(device->handle, transaction, 0);
        state->queue_us +=
            (uint64_t)(esp_timer_get_time() - started_us);
        if (err == ESP_ERR_TIMEOUT) {
            break;
        }
        if (err != ESP_OK) {
            state->err = err;
            break;
        }
        if (state->kind == SPI_FUTURE_WRITE_SOURCE && source_direct) {
            spi_root_span_owner(state->ctx,
                                &state->bulk_owners[slot_index],
                                state->pending_span.owner);
        }
        state->queue_tail =
            (state->queue_tail + 1U) % state->queue_depth;
        state->in_flight++;
        state->chunks_written++;
        state->bytes_written += length;
        if (state->kind == SPI_FUTURE_WRITE_CHUNKS) {
            state->chunk_offset += length;
            state->next_chunk++;
        } else {
            state->pending_span_ready = false;
            esp32_mquickjs_byte_span_clear(&state->pending_span);
        }
    }

    if ((state->source_done || state->cancelled || state->err != ESP_OK) &&
        state->in_flight == 0) {
        state->total_us =
            (uint64_t)(esp_timer_get_time() - state->total_start_us);
        atomic_store_explicit(&state->completed, true,
                              memory_order_release);
    }
}

static void spi_future_step(esp32_mquickjs_future_driver_state_t *state)
{
    esp32_mquickjs_spi_device_slot_t *device;
    spi_transaction_t *completed = NULL;

    if (state == NULL ||
        atomic_load_explicit(&state->completed, memory_order_acquire)) {
        return;
    }
    device = spi_get_device_slot(&state->device_ref);
    if (device == NULL) {
        state->err = ESP_ERR_INVALID_STATE;
        atomic_store_explicit(&state->completed, true,
                              memory_order_release);
        return;
    }
    if (spi_future_is_bulk(state)) {
        spi_future_bulk_step(state, device);
        return;
    }
    if (state->length == 0 || (state->cancelled && !state->queued)) {
        atomic_store_explicit(&state->completed, true,
                              memory_order_release);
        return;
    }
    if (!state->queued) {
        state->err = spi_device_queue_trans(device->handle,
                                            &state->transaction,
                                            0);
        if (state->err == ESP_ERR_TIMEOUT) {
            state->err = ESP_OK;
            return;
        }
        if (state->err != ESP_OK) {
            atomic_store_explicit(&state->completed, true,
                                  memory_order_release);
            return;
        }
        state->queued = true;
    }
    state->err = spi_device_get_trans_result(device->handle, &completed, 0);
    if (state->err == ESP_ERR_TIMEOUT) {
        state->err = ESP_OK;
        return;
    }
    atomic_store_explicit(&state->completed, true, memory_order_release);
}

static bool spi_future_start(JSContext *ctx,
                             esp32_mquickjs_runtime_t *runtime,
                             esp32_mquickjs_future_token_t token,
                             esp32_mquickjs_future_driver_state_t *state)
{
    esp32_mquickjs_spi_device_slot_t *device = state != NULL
        ? spi_get_device_slot(&state->device_ref) : NULL;
    esp_timer_create_args_t args = {
        .callback = spi_future_timer,
        .arg = state,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "mqjs_spi",
        .skip_unhandled_events = true,
    };

    if (state == NULL || device == NULL) {
        JS_ThrowReferenceError(ctx, "SPI device closed before transaction start");
        return false;
    }
    if (device->busy) {
        JS_ThrowInternalError(ctx, "SPI device is busy");
        return false;
    }
    device->busy = true;
    state->runtime = runtime;
    state->token = token;
    state->started = true;
    state->total_start_us = esp_timer_get_time();
    spi_future_step(state);
    if (!atomic_load_explicit(&state->completed, memory_order_acquire) &&
        (esp_timer_create(&args, &state->poll_timer) != ESP_OK ||
         esp_timer_start_periodic(state->poll_timer, 1000U) != ESP_OK)) {
        JS_ThrowInternalError(ctx, "failed to start SPI completion poller");
        return false;
    }
    (void)esp32_mquickjs_future_wake(runtime, token);
    return true;
}

static esp32_mquickjs_future_poll_t spi_future_poll(
    esp32_mquickjs_future_driver_state_t *state)
{
    spi_future_step(state);
    return state != NULL && atomic_load_explicit(
                                &state->completed, memory_order_acquire)
        ? ESP32_MQUICKJS_FUTURE_READY
        : ESP32_MQUICKJS_FUTURE_PENDING;
}

static JSValue spi_future_finish(JSContext *ctx,
                                 esp32_mquickjs_future_driver_state_t *state)
{
    if (state == NULL || state->cancelled) {
        return JS_ThrowInternalError(ctx, "SPI transaction cancelled");
    }
    if (state->source_error_retained) {
        return JS_Throw(ctx, state->source_error_ref.val);
    }
    if (state->err != ESP_OK) {
        return spi_throw_error(ctx, state->err, "SPI transaction failed");
    }
    if (spi_future_is_bulk(state)) {
        uint64_t wait_us = state->total_us > state->prep_us + state->queue_us
            ? state->total_us - state->prep_us - state->queue_us : 0U;

        return spi_make_write_chunks_stats(
            ctx, state->chunks_written, state->bytes_written,
            state->prep_us, state->queue_us, wait_us,
            state->total_us, state->queue_depth, state->direct);
    }
    if (state->kind == SPI_FUTURE_WRITE) {
        return JS_NewInt32(ctx, (int32_t)state->length);
    }
    {
        JSValue result = esp32_mquickjs_new_owned_byte_view(
            ctx, state->rx_data, state->length);

        if (!JS_IsException(result)) {
            state->rx_data = NULL;
        }
        return result;
    }
}

static esp32_mquickjs_cancel_result_t spi_future_cancel(
    esp32_mquickjs_future_driver_state_t *state)
{
    if (state == NULL ||
        atomic_load_explicit(&state->completed, memory_order_acquire) ||
        state->cancelled) {
        return ESP32_MQUICKJS_CANCEL_REJECTED;
    }
    state->cancelled = true;
    if ((!spi_future_is_bulk(state) && !state->queued) ||
        (spi_future_is_bulk(state) && state->in_flight == 0)) {
        atomic_store_explicit(&state->completed, true,
                              memory_order_release);
    }
    if (state->runtime != NULL) {
        (void)esp32_mquickjs_future_wake(state->runtime, state->token);
    }
    return (spi_future_is_bulk(state) ? state->in_flight > 0 : state->queued)
        ? ESP32_MQUICKJS_CANCEL_REQUESTED
        : ESP32_MQUICKJS_CANCELLED;
}

static void spi_future_destroy(esp32_mquickjs_future_driver_state_t *state)
{
    esp32_mquickjs_spi_device_slot_t *device = state != NULL
        ? spi_get_device_slot(&state->device_ref) : NULL;

    if (device != NULL && state->started) {
        device->busy = false;
    }
    spi_future_release(state);
}

static esp32_mquickjs_resource_key_t spi_future_resource_key(
    const esp32_mquickjs_future_driver_state_t *state)
{
    esp32_mquickjs_spi_device_slot_t *device = state != NULL
        ? spi_get_device_slot(&state->device_ref) : NULL;

    return device != NULL
        ? spi_get_bus_slot_by_ids(device->parent_bus_slot_id,
                                  device->parent_bus_generation)
        : NULL;
}

#define SPI_FUTURE_DRIVER(name, prepare_fn) \
    static const esp32_mquickjs_future_driver_t name = { \
        .capture = prepare_fn, \
        .start = spi_future_start, \
        .poll = spi_future_poll, \
        .finish = spi_future_finish, \
        .cancel = spi_future_cancel, \
        .destroy = spi_future_destroy, \
        .resource_key = spi_future_resource_key, \
    }

SPI_FUTURE_DRIVER(s_spi_transfer_driver, spi_transfer_future_prepare);
SPI_FUTURE_DRIVER(s_spi_write_driver, spi_write_future_prepare);
SPI_FUTURE_DRIVER(s_spi_read_driver, spi_read_future_prepare);
SPI_FUTURE_DRIVER(s_spi_write_chunks_driver,
                  spi_write_chunks_future_prepare);
SPI_FUTURE_DRIVER(s_spi_write_source_driver,
                  spi_write_source_future_prepare);

#undef SPI_FUTURE_DRIVER

static JSValue spi_future_call_and_wait(JSContext *ctx,
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

static bool spi_register_future_drivers(JSContext *ctx,
                                        esp32_mquickjs_runtime_t *runtime)
{
    static const char *names[] = {
        "transfer", "write", "read", "writeChunks", "writeSource"
    };
    static const esp32_mquickjs_future_driver_t *drivers[] = {
        &s_spi_transfer_driver,
        &s_spi_write_driver,
        &s_spi_read_driver,
        &s_spi_write_chunks_driver,
        &s_spi_write_source_driver,
    };
    JSGCRef object_ref;
    JSValue *object = JS_PushGCRef(ctx, &object_ref);
    size_t index;
    bool result = true;

    *object = JS_NewObjectClassUser(ctx, JS_CLASS_SPI_DEVICE);
    for (index = 0; result && index < sizeof(names) / sizeof(names[0]);
         ++index) {
        JSGCRef method_ref;
        JSValue *method = JS_PushGCRef(ctx, &method_ref);

        *method = JS_IsException(*object)
            ? JS_EXCEPTION : JS_GetPropertyStr(ctx, *object, names[index]);
        result = !JS_IsException(*method) &&
                 esp32_mquickjs_future_register_driver(
                     ctx, runtime, *method, drivers[index]);
        JS_PopGCRef(ctx, &method_ref);
    }
    if (!result && !JS_IsException(*object)) {
        JS_ThrowInternalError(ctx, "failed to register SPI Future drivers");
    }
    JS_PopGCRef(ctx, &object_ref);
    return result;
}

JSValue js_spi_device_transfer(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    return spi_future_call_and_wait(ctx, *this_val, "transfer", argc, argv);
}

JSValue js_spi_device_write(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    return spi_future_call_and_wait(ctx, *this_val, "write", argc, argv);
}

JSValue js_spi_device_write_chunks(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    return spi_future_call_and_wait(ctx, *this_val, "writeChunks", argc, argv);
}

JSValue js_spi_device_write_source(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    return spi_future_call_and_wait(ctx, *this_val, "writeSource", argc, argv);
}

JSValue js_spi_device_read(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    return spi_future_call_and_wait(ctx, *this_val, "read", argc, argv);
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
