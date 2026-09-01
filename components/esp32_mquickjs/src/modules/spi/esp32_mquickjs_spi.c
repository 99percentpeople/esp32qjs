#include "esp32_mquickjs_spi.h"
#include "esp32_mquickjs_memory.h"
#include "esp32_mquickjs_options.h"
#include "esp32_mquickjs_spi_native_resources.h"

#if CONFIG_ESP32_MQUICKJS_FEATURE_SPI

#include "utils/esp32_mquickjs_byte_source.h"
#include "esp32_mquickjs_core.h"
#include "esp32_mquickjs_dma_transfer.h"
#include "esp32_mquickjs_future.h"
#include "esp32_mquickjs_timer_resource.h"
#include "mquickjs_priv.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_attr.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_memory_utils.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "soc/soc_caps.h"

#define ESP32_MQUICKJS_SPI_DEFAULT_HOST_NUMBER \
    esp32_mquickjs_profile_int_or("ESP32QJS_SPI_HOST", 2)
#define ESP32_MQUICKJS_SPI_BUS_SLOT_COUNT (SPI_HOST_MAX - SPI2_HOST)
#define ESP32_MQUICKJS_SPI_DEVICE_SLOT_COUNT \
    (ESP32_MQUICKJS_SPI_BUS_SLOT_COUNT * ESP32_MQUICKJS_SPI_MAX_DEVICES_PER_BUS)
#define ESP32_MQUICKJS_SPI_DEFAULT_DMA_STAGING_BYTES 8192U
#define ESP32_MQUICKJS_SPI_DEFAULT_TIMEOUT_MS 1000U
#define ESP32_MQUICKJS_SPI_MAX_TIMEOUT_MS 60000U
#define ESP32_MQUICKJS_SPI_STAGING_BUFFER_COUNT 4U

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
    uint32_t dma_staging_bytes;
    uint32_t open_devices;
    uint16_t future_reservations;
    bool initialized;
    bool release_pending;
    uint8_t *staging_tx[ESP32_MQUICKJS_DMA_STAGING_SLOT_COUNT];
    uint8_t *staging_rx[ESP32_MQUICKJS_DMA_STAGING_SLOT_COUNT];
    esp32_mquickjs_dma_workspace_t dma_workspace;
    esp32_mquickjs_memory_dma_reservation_t staging_reservation;
} esp32_mquickjs_spi_bus_slot_t;

typedef struct {
    bool allocated;
    int32_t slot_id;
    uint32_t generation;
    int32_t parent_bus_slot_id;
    uint32_t parent_bus_generation;
    int32_t cs_pin;
    uint8_t mode;
    uint32_t requested_freq_hz;
    uint32_t actual_freq_hz;
    uint32_t queue_size;
    uint32_t timeout_ms;
    bool cs_high;
    bool lsb_first;
    bool direct_external_dma;
    bool faulted;
    bool busy;
    uint16_t future_reservations;
    bool release_pending;
    spi_device_handle_t handle;
    const char *last_error_code;
} esp32_mquickjs_spi_device_slot_t;

static esp32_mquickjs_spi_bus_slot_t s_spi_bus_slots[ESP32_MQUICKJS_SPI_BUS_SLOT_COUNT];
static esp32_mquickjs_spi_device_slot_t s_spi_device_slots[ESP32_MQUICKJS_SPI_DEVICE_SLOT_COUNT];
static uint32_t s_spi_next_generation = 1;

static bool spi_register_future_drivers(JSContext *ctx,
                                        esp32_mquickjs_runtime_t *runtime);
static void spi_future_transaction_done(spi_transaction_t *transaction);
static void spi_future_progress_timer_callback(void *opaque);
static JSValue spi_throw_operation_error(
    JSContext *ctx,
    const char *code,
    const char *operation,
    esp_err_t esp_code,
    size_t completed_bytes,
    esp32_mquickjs_dma_path_t path,
    const esp32_mquickjs_spi_device_slot_t *device);
static void spi_future_mark_device_fault(
    esp32_mquickjs_future_driver_state_t *state,
    esp32_mquickjs_spi_device_slot_t *device,
    const char *code,
    esp_err_t err);

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

static bool spi_validate_option_keys(JSContext *ctx,
                                     JSValue options,
                                     const char *api_name,
                                     const char *const *known,
                                     size_t known_count)
{
    return esp32_mquickjs_validate_plain_options(
        ctx, options, api_name, known, known_count);
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
    slot->dma_staging_bytes =
        slot->max_transfer_size < ESP32_MQUICKJS_SPI_DEFAULT_DMA_STAGING_BYTES
            ? slot->max_transfer_size
            : ESP32_MQUICKJS_SPI_DEFAULT_DMA_STAGING_BYTES;
}

static void spi_init_device_slot(esp32_mquickjs_spi_device_slot_t *slot, int32_t slot_id)
{
    memset(slot, 0, sizeof(*slot));
    slot->slot_id = slot_id;
    slot->parent_bus_slot_id = -1;
    slot->cs_pin = ESP32_MQUICKJS_SPI_DEFAULT_CS_PIN;
    slot->requested_freq_hz = ESP32_MQUICKJS_SPI_DEFAULT_FREQ_HZ;
    slot->actual_freq_hz = ESP32_MQUICKJS_SPI_DEFAULT_FREQ_HZ;
    slot->timeout_ms = ESP32_MQUICKJS_SPI_DEFAULT_TIMEOUT_MS;
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

static void *spi_allocate_dma_buffer(size_t bytes, void *opaque);
static void spi_release_dma_buffer(void *buffer, void *opaque);

static void spi_free_staging_workspace(esp32_mquickjs_spi_bus_slot_t *slot)
{
    if (slot == NULL) {
        return;
    }
    esp32_mquickjs_dma_workspace_release_buffers(
        slot->staging_tx, slot->staging_rx,
        spi_release_dma_buffer, NULL);
    memset(&slot->dma_workspace, 0, sizeof(slot->dma_workspace));
    if (slot->staging_reservation.state !=
        ESP32_MQUICKJS_MEMORY_DMA_IDLE) {
        (void)esp32_mquickjs_memory_release_driver_pinned(
            &slot->staging_reservation);
    }
}

static void *spi_allocate_dma_buffer(size_t bytes, void *opaque)
{
    (void)opaque;
    return heap_caps_malloc(
        bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
}

static void spi_release_dma_buffer(void *buffer, void *opaque)
{
    (void)opaque;
    heap_caps_free(buffer);
}

static int spi_resource_remove_device(void *device, void *opaque)
{
    (void)opaque;
    return spi_bus_remove_device((spi_device_handle_t)device);
}

static int spi_resource_free_bus(int host_id, void *opaque)
{
    (void)opaque;
    return spi_bus_free((spi_host_device_t)host_id);
}

static const esp32_mquickjs_spi_device_resource_ops_t
    s_spi_device_resource_ops = {
        .remove = spi_resource_remove_device,
        .opaque = NULL,
    };

static const esp32_mquickjs_spi_bus_resource_ops_t s_spi_bus_resource_ops = {
    .free = spi_resource_free_bus,
    .opaque = NULL,
};

static esp_err_t spi_cleanup_device_slot(esp32_mquickjs_spi_device_slot_t *slot)
{
    esp32_mquickjs_spi_bus_slot_t *parent;
    esp32_mquickjs_spi_device_resources_t resources;
    int32_t slot_id;
    esp_err_t err;

    if (slot == NULL || !slot->allocated) {
        return ESP_OK;
    }
    if (slot->busy || slot->future_reservations > 0) {
        return ESP_ERR_INVALID_STATE;
    }

    slot_id = slot->slot_id;
    resources = (esp32_mquickjs_spi_device_resources_t){
        .handle = slot->handle,
    };
    err = (esp_err_t)esp32_mquickjs_spi_device_resources_deinit(
        &resources, &s_spi_device_resource_ops);
    slot->handle = (spi_device_handle_t)resources.handle;
    if (err != ESP_OK) {
        return err;
    }
    parent = spi_get_bus_slot_by_ids(slot->parent_bus_slot_id, slot->parent_bus_generation);
    if (parent != NULL && parent->open_devices > 0) {
        parent->open_devices--;
    }
    spi_init_device_slot(slot, slot_id);
    return ESP_OK;
}

static esp_err_t spi_cleanup_bus_slot(esp32_mquickjs_spi_bus_slot_t *slot)
{
    esp32_mquickjs_spi_bus_resources_t resources;
    int32_t i;
    int32_t slot_id;
    esp_err_t err;

    if (slot == NULL || !slot->allocated) {
        return ESP_OK;
    }
    if (slot->future_reservations > 0) {
        return ESP_ERR_INVALID_STATE;
    }

    slot_id = slot->slot_id;
    for (i = 0; i < (int32_t)ESP32_MQUICKJS_SPI_DEVICE_SLOT_COUNT; ++i) {
        esp32_mquickjs_spi_device_slot_t *device_slot = &s_spi_device_slots[i];

        if (!device_slot->allocated || device_slot->parent_bus_slot_id != slot->slot_id ||
            device_slot->parent_bus_generation != slot->generation) {
            continue;
        }
        if (device_slot->busy || device_slot->future_reservations > 0) {
            return ESP_ERR_INVALID_STATE;
        }
    }
    for (i = 0; i < (int32_t)ESP32_MQUICKJS_SPI_DEVICE_SLOT_COUNT; ++i) {
        esp32_mquickjs_spi_device_slot_t *device_slot = &s_spi_device_slots[i];

        if (!device_slot->allocated || device_slot->parent_bus_slot_id != slot->slot_id ||
            device_slot->parent_bus_generation != slot->generation) {
            continue;
        }
        err = spi_cleanup_device_slot(device_slot);
        if (err != ESP_OK) {
            return err;
        }
    }

    resources = (esp32_mquickjs_spi_bus_resources_t){
        .initialized = slot->initialized,
        .host_id = (int)slot->host_id,
    };
    err = (esp_err_t)esp32_mquickjs_spi_bus_resources_deinit(
        &resources, &s_spi_bus_resource_ops);
    slot->initialized = resources.initialized;
    if (err != ESP_OK) {
        return err;
    }
    spi_free_staging_workspace(slot);
    spi_init_bus_slot(slot, slot_id);
    return ESP_OK;
}

static esp_err_t spi_cleanup_all(void)
{
    int32_t i;
    esp_err_t err;

    for (i = 0; i < (int32_t)ESP32_MQUICKJS_SPI_BUS_SLOT_COUNT; ++i) {
        err = spi_cleanup_bus_slot(&s_spi_bus_slots[i]);
        if (err != ESP_OK) {
            return err;
        }
    }
    for (i = 0; i < (int32_t)ESP32_MQUICKJS_SPI_DEVICE_SLOT_COUNT; ++i) {
        err = spi_cleanup_device_slot(&s_spi_device_slots[i]);
        if (err != ESP_OK) {
            return err;
        }
    }
    spi_reset_slots();
    return ESP_OK;
}

static esp_err_t spi_cleanup_pending_devices(
    esp32_mquickjs_spi_bus_slot_t *bus_slot)
{
    int32_t i;

    if (bus_slot == NULL || !bus_slot->allocated) {
        return ESP_ERR_INVALID_ARG;
    }
    for (i = 0; i < (int32_t)ESP32_MQUICKJS_SPI_DEVICE_SLOT_COUNT; ++i) {
        esp32_mquickjs_spi_device_slot_t *device_slot = &s_spi_device_slots[i];
        esp_err_t err;

        if (!device_slot->allocated || !device_slot->release_pending ||
            device_slot->parent_bus_slot_id != bus_slot->slot_id ||
            device_slot->parent_bus_generation != bus_slot->generation) {
            continue;
        }
        err = spi_cleanup_device_slot(device_slot);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
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
    if (slot->release_pending) {
        JS_ThrowReferenceError(ctx,
                               "%s failed because the SPI bus is closing",
                               api_name);
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
    if (device_slot->release_pending) {
        JS_ThrowReferenceError(ctx,
                               "%s failed because the SPI device is closing",
                               api_name);
        return -1;
    }
    bus_slot = spi_get_bus_slot_by_ids(device_slot->parent_bus_slot_id, device_slot->parent_bus_generation);
    if (bus_slot == NULL) {
        JS_ThrowReferenceError(ctx, "%s failed because the parent SPI bus is closed", api_name);
        return -1;
    }
    if (bus_slot->release_pending) {
        JS_ThrowReferenceError(
            ctx, "%s failed because the parent SPI bus is closing", api_name);
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
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "dmaStagingBytes",
                                     JS_NewUint32(ctx, slot != NULL ? slot->dma_staging_bytes
                                                                    : ESP32_MQUICKJS_SPI_DEFAULT_DMA_STAGING_BYTES)) ||
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
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "requestedFreqHz",
                                     JS_NewUint32(ctx, device_slot != NULL ? device_slot->requested_freq_hz
                                                                           : ESP32_MQUICKJS_SPI_DEFAULT_FREQ_HZ)) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "actualFreqHz",
                                     JS_NewUint32(ctx, device_slot != NULL ? device_slot->actual_freq_hz
                                                                           : ESP32_MQUICKJS_SPI_DEFAULT_FREQ_HZ)) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "queueSize",
                                     JS_NewUint32(ctx, device_slot != NULL ? device_slot->queue_size
                                                                           : ESP32_MQUICKJS_SPI_DEFAULT_QUEUE_SIZE)) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "csHigh",
                                     JS_NewBool(device_slot != NULL && device_slot->cs_high)) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "lsbFirst",
                                     JS_NewBool(device_slot != NULL && device_slot->lsb_first)) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "directExternalDma",
                                     JS_NewBool(device_slot != NULL && device_slot->direct_external_dma)) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "timeoutMs",
                                     JS_NewUint32(ctx, device_slot != NULL ? device_slot->timeout_ms
                                                                           : ESP32_MQUICKJS_SPI_DEFAULT_TIMEOUT_MS)) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "dmaStagingBytes",
                                     JS_NewUint32(ctx, bus_slot != NULL ? bus_slot->dma_staging_bytes
                                                                        : ESP32_MQUICKJS_SPI_DEFAULT_DMA_STAGING_BYTES)) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "faulted",
                                     JS_NewBool(device_slot != NULL && device_slot->faulted)) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "lastErrorCode",
                                     device_slot != NULL && device_slot->last_error_code != NULL
                                         ? JS_NewString(ctx, device_slot->last_error_code)
                                         : JS_NULL)) {
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
    uint32_t timeout_ms;
} spi_write_chunks_options_t;

static bool spi_parse_write_chunks_options(JSContext *ctx,
                                           JSValue options,
                                           const char *api_name,
                                           uint32_t default_timeout_ms,
                                           spi_write_chunks_options_t *out_options)
{
    static const char *const known_options[] = {
        "queueDepth", "timeoutMs",
    };

    out_options->queue_depth = 2;
    out_options->timeout_ms = default_timeout_ms;

    if (JS_IsUndefined(options) || JS_IsNull(options)) {
        return true;
    }
    if (JS_GetClassID(ctx, options) < 0) {
        JS_ThrowTypeError(ctx, "%s options must be an object", api_name);
        return false;
    }
    if (!spi_validate_option_keys(
            ctx, options, api_name, known_options,
            sizeof(known_options) / sizeof(known_options[0]))) {
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

        property = JS_PushGCRef(ctx, &property_ref);
        *property = JS_GetPropertyStr(ctx, options, "timeoutMs");
        if (JS_IsException(*property)) {
            JS_PopGCRef(ctx, &property_ref);
            return false;
        }
        if (!JS_IsUndefined(*property) && !JS_IsNull(*property) &&
            !esp32_mquickjs_value_to_bounded_u32(
                ctx, *property, 1U, ESP32_MQUICKJS_SPI_MAX_TIMEOUT_MS,
                &out_options->timeout_ms)) {
            JS_PopGCRef(ctx, &property_ref);
            JS_ThrowRangeError(
                ctx, "%s option 'timeoutMs' expects an integer in 1..60000",
                api_name);
            return false;
        }
        JS_PopGCRef(ctx, &property_ref);
    }
    return true;
}

static bool spi_parse_timeout_options(JSContext *ctx,
                                      JSValue options,
                                      const char *api_name,
                                      uint32_t default_timeout_ms,
                                      uint32_t *out_timeout_ms)
{
    static const char *const known_options[] = {"timeoutMs"};
    JSGCRef property_ref;
    JSValue *property;

    if (out_timeout_ms == NULL) {
        return false;
    }
    *out_timeout_ms = default_timeout_ms;
    if (JS_IsUndefined(options) || JS_IsNull(options)) {
        return true;
    }
    if (JS_GetClassID(ctx, options) < 0) {
        JS_ThrowTypeError(ctx, "%s options must be an object", api_name);
        return false;
    }
    if (!spi_validate_option_keys(
            ctx, options, api_name, known_options,
            sizeof(known_options) / sizeof(known_options[0]))) {
        return false;
    }
    property = JS_PushGCRef(ctx, &property_ref);
    *property = JS_GetPropertyStr(ctx, options, "timeoutMs");
    if (JS_IsException(*property)) {
        JS_PopGCRef(ctx, &property_ref);
        return false;
    }
    if (!JS_IsUndefined(*property) && !JS_IsNull(*property) &&
        !esp32_mquickjs_value_to_bounded_u32(
            ctx, *property, 1U, ESP32_MQUICKJS_SPI_MAX_TIMEOUT_MS,
            out_timeout_ms)) {
        JS_PopGCRef(ctx, &property_ref);
        JS_ThrowRangeError(
            ctx, "%s option 'timeoutMs' expects an integer in 1..60000",
            api_name);
        return false;
    }
    JS_PopGCRef(ctx, &property_ref);
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

static bool spi_buffer_can_dma(const void *data)
{
    if (data == NULL) {
        return false;
    }
    if (esp_ptr_dma_capable(data)) {
        return true;
    }
#if SOC_PSRAM_DMA_CAPABLE
    return esp_ptr_dma_ext_capable(data);
#else
    return false;
#endif
}

static esp32_mquickjs_dma_path_t spi_classify_buffer(
    const void *data,
    bool dma_hint,
    bool direct_external_dma)
{
    bool dma_capable = data != NULL && dma_hint && spi_buffer_can_dma(data);
    bool external = data != NULL && esp_ptr_external_ram(data);

    return esp32_mquickjs_dma_classify_source(
        dma_capable, external, direct_external_dma);
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

static JSValue spi_make_write_chunks_stats(JSContext *ctx,
                                           const esp32_mquickjs_dma_stats_t *dma,
                                           uint64_t copy_us,
                                           uint64_t queue_us,
                                           uint64_t wait_us,
                                           uint64_t transfer_us,
                                           uint64_t total_us,
                                           uint32_t queue_depth)
{
    JSGCRef stats_ref;
    JSValue *stats;

    stats = JS_PushGCRef(ctx, &stats_ref);
    *stats = JS_NewObject(ctx);
    if (JS_IsException(*stats)) {
        JS_PopGCRef(ctx, &stats_ref);
        return JS_EXCEPTION;
    }

    if (!esp32_mquickjs_set_property_ref(ctx, stats, "bytes", JS_NewInt64(ctx, (int64_t)dma->bytes)) ||
        !esp32_mquickjs_set_property_ref(ctx, stats, "sourceSpans", JS_NewUint32(ctx, dma->source_spans)) ||
        !esp32_mquickjs_set_property_ref(ctx, stats, "transactions", JS_NewUint32(ctx, dma->transactions)) ||
        !esp32_mquickjs_set_property_ref(ctx, stats, "path", JS_NewString(ctx, esp32_mquickjs_dma_path_name(esp32_mquickjs_dma_stats_path(dma)))) ||
        !esp32_mquickjs_set_property_ref(ctx, stats, "stagedBytes", JS_NewInt64(ctx, (int64_t)dma->staged_bytes)) ||
        !esp32_mquickjs_set_property_ref(ctx, stats, "copyUs", JS_NewInt64(ctx, (int64_t)copy_us)) ||
        !esp32_mquickjs_set_property_ref(ctx, stats, "queueUs", JS_NewInt64(ctx, (int64_t)queue_us)) ||
        !esp32_mquickjs_set_property_ref(ctx, stats, "waitUs", JS_NewInt64(ctx, (int64_t)wait_us)) ||
        !esp32_mquickjs_set_property_ref(ctx, stats, "transferUs", JS_NewInt64(ctx, (int64_t)transfer_us)) ||
        !esp32_mquickjs_set_property_ref(ctx, stats, "totalUs", JS_NewInt64(ctx, (int64_t)total_us)) ||
        !esp32_mquickjs_set_property_ref(ctx, stats, "queueDepth", JS_NewUint32(ctx, queue_depth))) {
        JS_PopGCRef(ctx, &stats_ref);
        return JS_EXCEPTION;
    }

    return JS_PopGCRef(ctx, &stats_ref);
}

static bool spi_allocate_staging_workspace(
    esp32_mquickjs_spi_bus_slot_t *slot,
    uint32_t staging_bytes)
{
    size_t total_bytes;
    if (slot == NULL || staging_bytes == 0 ||
        staging_bytes > SIZE_MAX / ESP32_MQUICKJS_SPI_STAGING_BUFFER_COUNT) {
        return false;
    }
    total_bytes = (size_t)staging_bytes *
                  ESP32_MQUICKJS_SPI_STAGING_BUFFER_COUNT;
    if (!esp32_mquickjs_memory_reserve_internal_dma(
            &slot->staging_reservation, "spi.staging",
            total_bytes, staging_bytes)) {
        return false;
    }
    if (!esp32_mquickjs_dma_workspace_allocate_buffers(
            slot->staging_tx, slot->staging_rx, staging_bytes,
            spi_allocate_dma_buffer, spi_release_dma_buffer, NULL)) {
        spi_free_staging_workspace(slot);
        return false;
    }
    if (!esp32_mquickjs_memory_commit_staging_pinned(
            &slot->staging_reservation, total_bytes, 1)) {
        spi_free_staging_workspace(slot);
        return false;
    }
    esp32_mquickjs_dma_workspace_init(
        &slot->dma_workspace,
        slot->staging_tx[0], slot->staging_rx[0],
        slot->staging_tx[1], slot->staging_rx[1], staging_bytes);
    slot->dma_staging_bytes = staging_bytes;
    return true;
}

static JSValue spi_open_bus(JSContext *ctx, int argc, JSValue *argv)
{
    int32_t host_number = ESP32_MQUICKJS_SPI_DEFAULT_HOST_NUMBER;
    int32_t slot_id = -1;
    int32_t sclk_pin = ESP32_MQUICKJS_SPI_DEFAULT_SCLK_PIN;
    int32_t mosi_pin = ESP32_MQUICKJS_SPI_DEFAULT_MOSI_PIN;
    int32_t miso_pin = ESP32_MQUICKJS_SPI_DEFAULT_MISO_PIN;
    uint32_t max_transfer_size = ESP32_MQUICKJS_SPI_DEFAULT_MAX_TRANSFER_SIZE;
    uint32_t dma_staging_bytes = 0;
    spi_bus_config_t bus_config = {0};
    esp32_mquickjs_spi_bus_slot_t *slot;
    JSValue result;
    esp_err_t err;

    if (argc > 1) {
        return JS_ThrowTypeError(ctx, "spi.openBus(options?) accepts at most one argument");
    }
    if (argc >= 1 && !JS_IsUndefined(argv[0])) {
        static const char *const known_options[] = {
            "host", "sclk", "mosi", "miso", "maxTransferSize",
            "dmaStagingBytes",
        };
        JSGCRef property_ref;
        JSValue *property;

        if (JS_GetClassID(ctx, argv[0]) < 0) {
            return JS_ThrowTypeError(ctx,
                                     "spi.openBus(options?) expects an options object");
        }
        if (!spi_validate_option_keys(
                ctx, argv[0], "spi.openBus()", known_options,
                sizeof(known_options) / sizeof(known_options[0]))) {
            return JS_EXCEPTION;
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

        *property = JS_GetPropertyStr(ctx, argv[0], "dmaStagingBytes");
        if (JS_IsException(*property)) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_EXCEPTION;
        }
        if (!JS_IsUndefined(*property) &&
            (!js_value_to_u32(ctx, *property, &dma_staging_bytes) ||
             dma_staging_bytes == 0U)) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_ThrowRangeError(
                ctx,
                "spi.openBus({ dmaStagingBytes }) expects a positive integer no larger than maxTransferSize");
        }

        JS_PopGCRef(ctx, &property_ref);
    }

    if (dma_staging_bytes == 0U) {
        dma_staging_bytes =
            max_transfer_size < ESP32_MQUICKJS_SPI_DEFAULT_DMA_STAGING_BYTES
                ? max_transfer_size
                : ESP32_MQUICKJS_SPI_DEFAULT_DMA_STAGING_BYTES;
    }
    if (dma_staging_bytes > max_transfer_size) {
        return JS_ThrowRangeError(
            ctx,
            "spi.openBus({ dmaStagingBytes }) must not exceed maxTransferSize");
    }

    if (!GPIO_IS_VALID_GPIO(sclk_pin)) {
        return JS_ThrowTypeError(ctx, "spi.openBus() requires a valid default SCLK GPIO or an explicit { sclk } override");
    }

    if (spi_host_number_to_ids(host_number, &slot_id, NULL) &&
        s_spi_bus_slots[slot_id].allocated &&
        s_spi_bus_slots[slot_id].release_pending) {
        err = spi_cleanup_bus_slot(&s_spi_bus_slots[slot_id]);
        if (err != ESP_OK) {
            return spi_throw_error(ctx, err, "spi.openBus() cleanup failed");
        }
    }
    slot = spi_alloc_bus_slot(host_number);
    if (slot == NULL) {
        return JS_ThrowInternalError(ctx, "spi.openBus() failed: selected SPI host is unavailable or already in use");
    }
    if (!spi_allocate_staging_workspace(slot, dma_staging_bytes)) {
        (void)spi_cleanup_bus_slot(slot);
        return spi_throw_operation_error(
            ctx, "DMA_STAGING_NO_MEMORY", "openBus", ESP_ERR_NO_MEM, 0,
            ESP32_MQUICKJS_DMA_PATH_STAGED_INTERNAL, NULL);
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
        (void)spi_cleanup_bus_slot(slot);
        return spi_throw_error(ctx, err, "spi.openBus() failed");
    }

    slot->initialized = true;

    slot->sclk_pin = sclk_pin;
    slot->mosi_pin = mosi_pin;
    slot->miso_pin = miso_pin;
    slot->max_transfer_size = max_transfer_size;

    result = spi_make_bus_object(ctx, slot);
    if (JS_IsException(result)) {
        slot->release_pending = true;
        (void)spi_cleanup_bus_slot(slot);
    }
    return result;
}

bool esp32_mquickjs_deinit_spi_runtime(void)
{
    esp_err_t err = spi_cleanup_all();

    return err == ESP_OK;
}

bool esp32_mquickjs_init_spi_runtime(JSContext *ctx,
                                     esp32_mquickjs_runtime_t *runtime)
{
    esp_err_t err = spi_cleanup_all();

    if (err != ESP_OK) {
        JS_ThrowInternalError(ctx,
                              "SPI runtime cleanup failed: %s",
                              esp_err_to_name(err));
        return false;
    }
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
        (void)spi_cleanup_bus_slot(slot);
    }
    heap_caps_free(bus_ref);
}

JSValue js_spi_bus_close(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    esp32_mquickjs_spi_bus_ref_t bus_ref;
    esp32_mquickjs_spi_bus_ref_t *bus_ref_ptr;
    esp32_mquickjs_spi_bus_slot_t *slot;
    int32_t i;
    esp_err_t err;

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
        for (i = 0; i < (int32_t)ESP32_MQUICKJS_SPI_DEVICE_SLOT_COUNT; ++i) {
            esp32_mquickjs_spi_device_slot_t *device_slot =
                &s_spi_device_slots[i];

            if (device_slot->allocated &&
                device_slot->parent_bus_slot_id == slot->slot_id &&
                device_slot->parent_bus_generation == slot->generation &&
                (device_slot->busy ||
                 device_slot->future_reservations > 0)) {
                return JS_ThrowInternalError(
                    ctx,
                    "SPIBus.close() refused while an operation is pending");
            }
        }
        slot->release_pending = true;
        err = spi_cleanup_bus_slot(slot);
        if (err != ESP_OK) {
            return spi_throw_error(ctx, err, "SPIBus.close() failed");
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
    uint32_t timeout_ms = ESP32_MQUICKJS_SPI_DEFAULT_TIMEOUT_MS;
    bool cs_high = false;
    bool lsb_first = false;
    bool direct_external_dma = false;
    int actual_freq_khz = 0;
    JSValue result;
    esp_err_t err;

    if (spi_get_this_bus_slot(ctx, *this_val, "SPIBus.openDevice()", &bus_ref, &bus_slot) != 0) {
        return JS_EXCEPTION;
    }

    if (argc > 1) {
        return JS_ThrowTypeError(
            ctx, "SPIBus.openDevice(options?) accepts at most one argument");
    }
    if (argc >= 1 && !JS_IsUndefined(argv[0])) {
        static const char *const known_options[] = {
            "cs", "freqHz", "mode", "queueSize", "csHigh",
            "lsbFirst", "directExternalDma", "timeoutMs",
        };
        JSGCRef property_ref;
        JSValue *property;

        if (JS_GetClassID(ctx, argv[0]) < 0) {
            return JS_ThrowTypeError(ctx,
                                     "SPIBus.openDevice(options?) expects an options object");
        }
        if (!spi_validate_option_keys(
                ctx, argv[0], "SPIBus.openDevice()", known_options,
                sizeof(known_options) / sizeof(known_options[0]))) {
            return JS_EXCEPTION;
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

        *property = JS_GetPropertyStr(ctx, argv[0], "directExternalDma");
        if (JS_IsException(*property)) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_EXCEPTION;
        }
        if (!JS_IsUndefined(*property) &&
            !js_value_to_bool(ctx, *property, &direct_external_dma)) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_ThrowTypeError(
                ctx,
                "SPIBus.openDevice({ directExternalDma }) expects a boolean");
        }

        *property = JS_GetPropertyStr(ctx, argv[0], "timeoutMs");
        if (JS_IsException(*property)) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_EXCEPTION;
        }
        if (!JS_IsUndefined(*property) &&
            (!js_value_to_u32(ctx, *property, &timeout_ms) ||
             timeout_ms < 1U || timeout_ms > ESP32_MQUICKJS_SPI_MAX_TIMEOUT_MS)) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_ThrowRangeError(
                ctx,
                "SPIBus.openDevice({ timeoutMs }) expects an integer in 1..60000");
        }

        JS_PopGCRef(ctx, &property_ref);
    }

    err = spi_cleanup_pending_devices(bus_slot);
    if (err != ESP_OK) {
        return spi_throw_error(
            ctx, err, "SPIBus.openDevice() cleanup failed");
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
    device_config.post_cb = spi_future_transaction_done;
    device_config.flags = 0;
    if (cs_high) {
        device_config.flags |= SPI_DEVICE_POSITIVE_CS;
    }
    if (lsb_first) {
        device_config.flags |= SPI_DEVICE_BIT_LSBFIRST;
    }

    err = spi_bus_add_device(bus_slot->host_id, &device_config, &device_slot->handle);
    if (err != ESP_OK) {
        device_slot->release_pending = true;
        (void)spi_cleanup_device_slot(device_slot);
        return spi_throw_error(ctx, err, "SPIBus.openDevice() failed");
    }
    err = spi_device_get_actual_freq(device_slot->handle, &actual_freq_khz);
    if (err != ESP_OK || actual_freq_khz <= 0) {
        device_slot->release_pending = true;
        (void)spi_cleanup_device_slot(device_slot);
        return spi_throw_error(
            ctx, err != ESP_OK ? err : ESP_ERR_INVALID_STATE,
            "SPIBus.openDevice() failed to resolve actual frequency");
    }

    device_slot->cs_pin = cs_pin;
    device_slot->mode = mode;
    device_slot->requested_freq_hz = freq_hz;
    device_slot->actual_freq_hz = (uint32_t)actual_freq_khz * 1000U;
    device_slot->queue_size = queue_size;
    device_slot->timeout_ms = timeout_ms;
    device_slot->cs_high = cs_high;
    device_slot->lsb_first = lsb_first;
    device_slot->direct_external_dma = direct_external_dma;

    result = spi_make_device_object(ctx, device_slot);
    if (JS_IsException(result)) {
        device_slot->release_pending = true;
        (void)spi_cleanup_device_slot(device_slot);
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
        (void)spi_cleanup_device_slot(slot);
    }
    heap_caps_free(device_ref);
}

JSValue js_spi_device_close(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    esp32_mquickjs_spi_device_ref_t device_ref;
    esp32_mquickjs_spi_device_ref_t *device_ref_ptr;
    esp32_mquickjs_spi_device_slot_t *slot;
    esp_err_t err;

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
        slot->release_pending = true;
        err = spi_cleanup_device_slot(slot);
        if (err != ESP_OK) {
            return spi_throw_error(ctx, err, "SPIDevice.close() failed");
        }
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

static const char *spi_future_operation_name(spi_future_kind_t kind)
{
    switch (kind) {
    case SPI_FUTURE_TRANSFER:
        return "transfer";
    case SPI_FUTURE_WRITE:
        return "write";
    case SPI_FUTURE_READ:
        return "read";
    case SPI_FUTURE_WRITE_CHUNKS:
        return "writeChunks";
    case SPI_FUTURE_WRITE_SOURCE:
        return "writeSource";
    default:
        return "unknown";
    }
}

typedef struct {
    esp32_mquickjs_dma_chunk_t chunk;
    int64_t queued_us;
    size_t output_offset;
} spi_transaction_meta_t;

struct esp32_mquickjs_future_driver_state {
    spi_future_kind_t kind;
    JSContext *ctx;
    JSGCRef owner_ref;
    esp32_mquickjs_spi_device_ref_t device_ref;
    esp32_mquickjs_runtime_t *runtime;
    esp32_mquickjs_future_token_t token;
    esp32_mquickjs_timer_resource_t progress_timer;
    uint64_t progress_timer_delay_us;
    spi_transaction_t transactions[ESP32_MQUICKJS_DMA_STAGING_SLOT_COUNT];
    spi_transaction_meta_t transaction_meta[ESP32_MQUICKJS_DMA_STAGING_SLOT_COUNT];
    esp32_mquickjs_byte_span_source_t span_source;
    esp32_mquickjs_byte_span_t pending_span;
    esp32_mquickjs_dma_cursor_t cursor;
    esp32_mquickjs_dma_stats_t dma_stats;
    esp32_mquickjs_byte_source_t single_source;
    esp32_mquickjs_byte_source_chunk_t *chunks;
    JSGCRef source_error_ref;
    JSGCRef single_owner_ref;
    uint8_t *single_owned;
    uint8_t *rx_data;
    size_t length;
    size_t expected_source_bytes;
    size_t produced_source_bytes;
    size_t queued_stream_bytes;
    uint32_t chunk_count;
    uint32_t next_chunk;
    uint32_t queue_depth;
    esp32_mquickjs_dma_completion_queue_t completion_queue;
    uint32_t timeout_ms;
    uint8_t fill_byte;
    uint64_t copy_us;
    uint64_t queue_us;
    uint64_t transfer_us;
    uint64_t total_us;
    uint64_t operation_deadline_us;
    uint64_t progress_deadline_us;
    int64_t total_start_us;
    int64_t first_queue_us;
    int64_t last_completion_us;
    esp_err_t err;
    const char *error_code;
    const char *operation;
    bool owner_retained;
    bool single_owner_retained;
    bool single_read_leased;
    bool device_reserved;
    bool bus_reserved;
    bool span_source_opened;
    bool pending_span_ready;
    bool source_done;
    bool source_error_retained;
    bool source_length_known;
    bool bus_acquired;
    bool timeout_triggered;
    bool started;
    _Atomic bool completed;
    bool cancelled;
};

static void IRAM_ATTR spi_future_transaction_done(
    spi_transaction_t *transaction)
{
    esp32_mquickjs_future_driver_state_t *state =
        transaction != NULL ? transaction->user : NULL;
    int task_woken = 0;

    if (state == NULL || state->runtime == NULL ||
        atomic_load_explicit(&state->completed, memory_order_acquire)) {
        return;
    }
    (void)esp32_mquickjs_future_wake_from_isr(
        state->runtime, state->token, &task_woken);
    if (task_woken != 0) {
        portYIELD_FROM_ISR();
    }
}

static JSValue spi_throw_operation_error(
    JSContext *ctx,
    const char *code,
    const char *operation,
    esp_err_t esp_code,
    size_t completed_bytes,
    esp32_mquickjs_dma_path_t path,
    const esp32_mquickjs_spi_device_slot_t *device)
{
    JSGCRef details_ref;
    JSValue *details = JS_PushGCRef(ctx, &details_ref);
    JSValue result;

    *details = JS_NewObject(ctx);
    if (JS_IsException(*details) ||
        !esp32_mquickjs_set_property_ref(
            ctx, details, "espCode", JS_NewInt32(ctx, esp_code)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, details, "espName",
            JS_NewString(ctx, esp_err_to_name(esp_code))) ||
        !esp32_mquickjs_set_property_ref(
            ctx, details, "completedBytes",
            JS_NewInt64(ctx, (int64_t)completed_bytes)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, details, "path",
            JS_NewString(ctx, esp32_mquickjs_dma_path_name(path))) ||
        !esp32_mquickjs_set_property_ref(
            ctx, details, "requestedFreqHz",
            JS_NewUint32(ctx, device != NULL
                                  ? device->requested_freq_hz
                                  : ESP32_MQUICKJS_SPI_DEFAULT_FREQ_HZ)) ||
        !esp32_mquickjs_set_property_ref(
            ctx, details, "actualFreqHz",
            JS_NewUint32(ctx, device != NULL
                                  ? device->actual_freq_hz
                                  : ESP32_MQUICKJS_SPI_DEFAULT_FREQ_HZ))) {
        JS_PopGCRef(ctx, &details_ref);
        return JS_EXCEPTION;
    }
    result = esp32_mquickjs_throw_native_error(
        ctx, code, operation, "SPI operation failed", *details);
    JS_PopGCRef(ctx, &details_ref);
    return result;
}

static int spi_future_progress_timer_create(void *opaque, void **out_timer)
{
    esp32_mquickjs_future_driver_state_t *state = opaque;
    esp_timer_create_args_t timer_args = {
        .callback = spi_future_progress_timer_callback,
        .arg = state,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "spi_progress",
        .skip_unhandled_events = true,
    };
    esp_timer_handle_t timer = NULL;
    esp_err_t err;

    if (state == NULL || out_timer == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    err = esp_timer_create(&timer_args, &timer);
    *out_timer = timer;
    return err;
}

static int spi_future_progress_timer_start(void *timer, void *opaque)
{
    esp32_mquickjs_future_driver_state_t *state = opaque;

    if (state == NULL || state->progress_timer_delay_us == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    return esp_timer_start_once((esp_timer_handle_t)timer,
                                state->progress_timer_delay_us);
}

static void spi_future_progress_timer_stop(void *timer, void *opaque)
{
    (void)opaque;
    (void)esp_timer_stop((esp_timer_handle_t)timer);
}

static void spi_future_progress_timer_delete(void *timer, void *opaque)
{
    (void)opaque;
    (void)esp_timer_delete((esp_timer_handle_t)timer);
}

static esp32_mquickjs_timer_resource_ops_t spi_future_progress_timer_ops(
    esp32_mquickjs_future_driver_state_t *state)
{
    return (esp32_mquickjs_timer_resource_ops_t){
        .create = spi_future_progress_timer_create,
        .start = spi_future_progress_timer_start,
        .stop = spi_future_progress_timer_stop,
        .delete_timer = spi_future_progress_timer_delete,
        .opaque = state,
    };
}

static void spi_future_release(esp32_mquickjs_future_driver_state_t *state)
{
    esp32_mquickjs_spi_device_slot_t *device;
    esp32_mquickjs_spi_bus_slot_t *bus;
    esp32_mquickjs_timer_resource_ops_t timer_ops;
    int32_t index;

    if (state == NULL) {
        return;
    }
    timer_ops = spi_future_progress_timer_ops(state);
    esp32_mquickjs_timer_resource_deinit(&state->progress_timer,
                                         &timer_ops);
    device = spi_get_device_slot(&state->device_ref);
    if (state->bus_acquired && device != NULL) {
        spi_device_release_bus(device->handle);
        state->bus_acquired = false;
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
    if (state->single_read_leased && state->single_owner_retained) {
        esp32_mquickjs_byte_view_release_read(
            state->ctx, state->single_owner_ref.val);
        state->single_read_leased = false;
    }
    if (state->single_owner_retained) {
        JS_DeleteGCRef(state->ctx, &state->single_owner_ref);
        state->single_owner_retained = false;
    }
    for (index = 0; index < (int32_t)state->chunk_count; ++index) {
        esp32_mquickjs_release_byte_source_chunk(
            state->ctx, &state->chunks[index]);
    }
    heap_caps_free(state->chunks);
    state->chunks = NULL;
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
    esp32_mquickjs_release_byte_source(state->single_owned);
    state->single_owned = NULL;
    esp32_mquickjs_memory_payload_free(state->rx_data);
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
                (void)spi_cleanup_device_slot(candidate);
            }
        }
        if (bus->release_pending && bus->future_reservations == 0) {
            (void)spi_cleanup_bus_slot(bus);
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
    if (device->faulted) {
        heap_caps_free(state);
        (void)spi_throw_operation_error(
            ctx, "DMA_DEVICE_FAULTED", spi_future_operation_name(kind),
            ESP_ERR_INVALID_STATE,
            0, ESP32_MQUICKJS_DMA_PATH_DIRECT_INTERNAL, device);
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
    esp32_mquickjs_dma_stats_init(&state->dma_stats);
    state->err = ESP_OK;
    device->future_reservations++;
    bus->future_reservations++;
    state->device_reserved = true;
    state->bus_reserved = true;
    owner = JS_AddGCRef(ctx, &state->owner_ref);
    *owner = this_value;
    state->owner_retained = true;
    return state;
}

static bool spi_future_allocate_rx_buffer(
    JSContext *ctx,
    esp32_mquickjs_future_driver_state_t *state,
    size_t length)
{
    state->length = length;
    if (length == 0) {
        return true;
    }
    state->rx_data = esp32_mquickjs_memory_payload_alloc(
        "spi.rx", length, ESP32_MQUICKJS_MEMORY_EXTERNAL);
    if (state->rx_data == NULL) {
        JS_ThrowOutOfMemory(ctx);
        return false;
    }
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
    esp32_mquickjs_spi_device_slot_t *device;
    uint32_t timeout_ms;
    JSValue *source_owner;

    if (out_state == NULL || argc < 1 || argc > 2) {
        JS_ThrowTypeError(
            ctx,
            "SPIDevice write/transfer expects (data, { timeoutMs? }?)");
        return false;
    }
    if (!esp32_mquickjs_get_byte_source(
            ctx, argv[0].val,
            kind == SPI_FUTURE_TRANSFER
                ? "SPIDevice.transfer(data)"
                : "SPIDevice.write(data)",
            &source, &owned, &error)) {
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
    device = spi_get_device_slot(&state->device_ref);
    if (device == NULL ||
        !spi_parse_timeout_options(
            ctx, argc == 2 ? argv[1].val : JS_UNDEFINED,
            kind == SPI_FUTURE_TRANSFER ? "SPIDevice.transfer()"
                                        : "SPIDevice.write()",
            device != NULL ? device->timeout_ms
                           : ESP32_MQUICKJS_SPI_DEFAULT_TIMEOUT_MS,
            &timeout_ms)) {
        esp32_mquickjs_release_byte_source(owned);
        spi_future_release(state);
        return false;
    }
    state->single_source = source;
    state->single_owned = owned;
    source_owner = JS_AddGCRef(ctx, &state->single_owner_ref);
    *source_owner = source.owner;
    state->single_owner_retained = true;
    state->operation = kind == SPI_FUTURE_TRANSFER ? "transfer" : "write";
    if (JS_GetClassID(ctx, argv[0].val) == JS_CLASS_BYTE_VIEW) {
        const uint8_t *leased_data = NULL;
        size_t leased_length = 0;

        if (!esp32_mquickjs_byte_view_acquire_read(
                ctx, argv[0].val, state->operation,
                &leased_data, &leased_length)) {
            spi_future_release(state);
            return false;
        }
        state->single_source.data = leased_data;
        state->single_source.length = leased_length;
        state->single_read_leased = true;
    }
    state->length = source.length;
    state->expected_source_bytes = source.length;
    state->source_length_known = true;
    state->timeout_ms = timeout_ms;
    state->queue_depth = spi_clamp_queue_depth(device, 2);
    (void)esp32_mquickjs_dma_completion_queue_init(
        &state->completion_queue, state->queue_depth);
    if (kind == SPI_FUTURE_TRANSFER &&
        !spi_future_allocate_rx_buffer(ctx, state, source.length)) {
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
    static const char *const known_options[] = {"fillByte", "timeoutMs"};
    esp32_mquickjs_future_driver_state_t *state;
    esp32_mquickjs_spi_device_slot_t *device;
    uint32_t length;
    uint32_t fill = 0;
    uint32_t timeout_ms;

    if (out_state == NULL || argc < 1 || argc > 2 ||
        !js_value_to_u32(ctx, argv[0].val, &length)) {
        JS_ThrowTypeError(ctx,
                          "SPIDevice.read(length, { fillByte?, timeoutMs? }?) expects a byte length and options");
        return false;
    }
    state = spi_future_allocate(ctx, this_ref->val, SPI_FUTURE_READ);
    if (state == NULL) {
        return false;
    }
    device = spi_get_device_slot(&state->device_ref);
    timeout_ms = device != NULL ? device->timeout_ms
                                : ESP32_MQUICKJS_SPI_DEFAULT_TIMEOUT_MS;
    if (argc == 2 && !JS_IsUndefined(argv[1].val) &&
        !JS_IsNull(argv[1].val)) {
        JSGCRef property_ref;
        JSValue *property;

        if (JS_GetClassID(ctx, argv[1].val) < 0 ||
            !spi_validate_option_keys(
                ctx, argv[1].val, "SPIDevice.read()", known_options,
                sizeof(known_options) / sizeof(known_options[0]))) {
            if (!JS_HasException(ctx)) {
                JS_ThrowTypeError(ctx, "SPIDevice.read() options must be an object");
            }
            spi_future_release(state);
            return false;
        }
        property = JS_PushGCRef(ctx, &property_ref);
        *property = JS_GetPropertyStr(ctx, argv[1].val, "fillByte");
        if (JS_IsException(*property) ||
            (!JS_IsUndefined(*property) && !JS_IsNull(*property) &&
             (!js_value_to_u32(ctx, *property, &fill) || fill > 0xffU))) {
            JS_PopGCRef(ctx, &property_ref);
            JS_ThrowRangeError(
                ctx, "SPIDevice.read({ fillByte }) expects an integer in 0..255");
            spi_future_release(state);
            return false;
        }
        JS_PopGCRef(ctx, &property_ref);
        property = JS_PushGCRef(ctx, &property_ref);
        *property = JS_GetPropertyStr(ctx, argv[1].val, "timeoutMs");
        if (JS_IsException(*property) ||
            (!JS_IsUndefined(*property) && !JS_IsNull(*property) &&
             (!js_value_to_u32(ctx, *property, &timeout_ms) ||
              timeout_ms < 1U ||
              timeout_ms > ESP32_MQUICKJS_SPI_MAX_TIMEOUT_MS))) {
            JS_PopGCRef(ctx, &property_ref);
            JS_ThrowRangeError(
                ctx,
                "SPIDevice.read({ timeoutMs }) expects an integer in 1..60000");
            spi_future_release(state);
            return false;
        }
        JS_PopGCRef(ctx, &property_ref);
    }
    state->fill_byte = (uint8_t)fill;
    state->expected_source_bytes = length;
    state->source_length_known = true;
    state->timeout_ms = timeout_ms;
    state->queue_depth = spi_clamp_queue_depth(device, 2);
    (void)esp32_mquickjs_dma_completion_queue_init(
        &state->completion_queue, state->queue_depth);
    state->operation = "read";
    if (!spi_future_allocate_rx_buffer(ctx, state, length)) {
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
    if (device == NULL ||
        !spi_parse_write_chunks_options(
            ctx, argc == 2 ? argv[1].val : JS_UNDEFINED,
            "SPIDevice.writeChunks()",
            device != NULL ? device->timeout_ms
                           : ESP32_MQUICKJS_SPI_DEFAULT_TIMEOUT_MS,
            &options) ||
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
    (void)esp32_mquickjs_dma_completion_queue_init(
        &state->completion_queue, state->queue_depth);
    state->timeout_ms = options.timeout_ms;
    state->operation = "writeChunks";
    if (state->chunk_count > 0) {
        state->chunks = heap_caps_calloc(
            state->chunk_count, sizeof(*state->chunks),
            MALLOC_CAP_8BIT);
        if (state->chunks == NULL) {
            spi_future_release(state);
            JS_ThrowOutOfMemory(ctx);
            return false;
        }
    }
    for (index = 0; index < state->chunk_count; ++index) {
        if (!esp32_mquickjs_get_byte_source_chunk(
                ctx, argv[0].val, index,
                "SPIDevice.writeChunks(chunks)", &state->chunks[index],
                &error)) {
            if (!JS_HasException(ctx)) {
                JS_ThrowTypeError(
                    ctx,
                    "SPIDevice.writeChunks(chunks) expects byte-source chunks");
            }
            spi_future_release(state);
            return false;
        }
        if (state->chunks[index].source.length > SIZE_MAX - total) {
            spi_future_release(state);
            JS_ThrowRangeError(
                ctx,
                "SPIDevice.writeChunks() total byte length exceeds the supported size");
            return false;
        }
        total += state->chunks[index].source.length;
    }
    state->length = total;
    state->expected_source_bytes = total;
    state->source_length_known = true;
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
    size_t known_length = 0;
    bool length_known;

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
    length_known = esp32_mquickjs_byte_span_source_known_length(
        ctx, argv[0].val, &known_length);
    if (device == NULL ||
        !spi_parse_write_chunks_options(
            ctx, argc == 2 ? argv[1].val : JS_UNDEFINED,
            "SPIDevice.writeSource()",
            device != NULL ? device->timeout_ms
                           : ESP32_MQUICKJS_SPI_DEFAULT_TIMEOUT_MS,
            &options) ||
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
    (void)esp32_mquickjs_dma_completion_queue_init(
        &state->completion_queue, state->queue_depth);
    state->timeout_ms = options.timeout_ms;
    state->operation = "writeSource";
    state->source_length_known = length_known;
    state->expected_source_bytes = length_known ? known_length : 0;
    state->length = length_known ? known_length : 0;
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

static bool spi_future_is_bulk(
    const esp32_mquickjs_future_driver_state_t *state)
{
    return state != NULL &&
           (state->kind == SPI_FUTURE_WRITE_CHUNKS ||
            state->kind == SPI_FUTURE_WRITE_SOURCE);
}

static bool spi_future_prepare_next_span(
    esp32_mquickjs_future_driver_state_t *state)
{
    unsigned empty_spans = 0;
    esp32_mquickjs_spi_device_slot_t *device;

    if (state == NULL) {
        return false;
    }
    if (state->pending_span_ready &&
        state->cursor.offset < state->cursor.length) {
        return true;
    }
    state->pending_span_ready = false;
    esp32_mquickjs_byte_span_clear(&state->pending_span);
    device = spi_get_device_slot(&state->device_ref);
    if (device == NULL) {
        state->err = ESP_ERR_INVALID_STATE;
        state->error_code = "DMA_DEVICE_FAULTED";
        state->source_done = true;
        return false;
    }

    while (!state->source_done && !state->pending_span_ready) {
        bool dma_hint = true;

        if (state->kind == SPI_FUTURE_READ) {
            if (state->next_chunk > 0 || state->length == 0) {
                state->source_done = true;
                break;
            }
            state->pending_span.data = NULL;
            state->pending_span.length = state->length;
            state->next_chunk = 1;
            dma_hint = false;
        } else if (state->kind == SPI_FUTURE_WRITE ||
                   state->kind == SPI_FUTURE_TRANSFER) {
            if (state->next_chunk > 0 || state->single_source.length == 0) {
                state->source_done = true;
                break;
            }
            state->pending_span.data = state->single_source.data;
            state->pending_span.length = state->single_source.length;
            state->pending_span.owner = state->single_source.owner;
            state->next_chunk = 1;
        } else if (state->kind == SPI_FUTURE_WRITE_CHUNKS) {
            if (state->next_chunk >= state->chunk_count) {
                state->source_done = true;
                break;
            }
            state->pending_span.data =
                state->chunks[state->next_chunk].source.data;
            state->pending_span.length =
                state->chunks[state->next_chunk].source.length;
            state->pending_span.owner =
                state->chunks[state->next_chunk].source.owner;
            state->next_chunk++;
        } else {
            if (!esp32_mquickjs_byte_span_source_next(
                    state->ctx, &state->span_source,
                    &state->pending_span)) {
                if (JS_HasException(state->ctx)) {
                    spi_future_retain_source_exception(
                        state,
                        "SPIDevice.writeSource() source iteration failed");
                }
                state->source_done = true;
                break;
            }
            dma_hint = state->pending_span.dma_capable;
        }
        if (state->pending_span.length == 0) {
            if (++empty_spans > 1024U) {
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
        if (state->pending_span.data == NULL &&
            state->kind != SPI_FUTURE_READ) {
            (void)JS_ThrowInternalError(
                state->ctx,
                "SPI source produced a non-empty span with null data");
            spi_future_retain_source_exception(
                state, "SPI source produced an invalid span");
            state->source_done = true;
            return false;
        }
        if (state->pending_span.length >
            SIZE_MAX - state->produced_source_bytes) {
            state->err = ESP_ERR_INVALID_SIZE;
            state->error_code = "DMA_TX_UNDERFLOW";
            state->source_done = true;
            return false;
        }
        state->produced_source_bytes += state->pending_span.length;
        esp32_mquickjs_dma_stats_note_span(
            &state->dma_stats,
            state->kind == SPI_FUTURE_READ
                ? ESP32_MQUICKJS_DMA_PATH_STAGED_INTERNAL
                : spi_classify_buffer(
                      state->pending_span.data, dma_hint,
                      device->direct_external_dma));
        esp32_mquickjs_dma_cursor_begin(
            &state->cursor, state->pending_span.data,
            state->pending_span.length,
            state->kind == SPI_FUTURE_READ
                ? ESP32_MQUICKJS_DMA_PATH_STAGED_INTERNAL
                : spi_classify_buffer(
                      state->pending_span.data, dma_hint,
                      device->direct_external_dma),
            state->dma_stats.source_spans);
        state->pending_span_ready = true;
    }
    if (state->source_done && state->source_length_known &&
        esp32_mquickjs_dma_validate_tx_progress(
            state->produced_source_bytes,
            state->expected_source_bytes) !=
            ESP32_MQUICKJS_DMA_PROGRESS_OK &&
        state->error_code == NULL && !state->source_error_retained) {
        state->err = ESP_ERR_INVALID_SIZE;
        state->error_code = "DMA_TX_UNDERFLOW";
    }
    if (state->source_done && !state->source_length_known) {
        state->length = state->produced_source_bytes;
    }
    return state != NULL && state->pending_span_ready;
}


static void spi_future_progress_timer_callback(void *opaque)
{
    esp32_mquickjs_future_driver_state_t *state = opaque;

    if (state != NULL && state->runtime != NULL) {
        (void)esp32_mquickjs_future_wake(state->runtime, state->token);
    }
}

static void spi_future_stop_progress_timer(
    esp32_mquickjs_future_driver_state_t *state)
{
    if (state != NULL) {
        esp32_mquickjs_timer_resource_ops_t timer_ops =
            spi_future_progress_timer_ops(state);

        esp32_mquickjs_timer_resource_stop(&state->progress_timer,
                                           &timer_ops);
    }
}

static bool spi_future_arm_progress_timer(
    esp32_mquickjs_future_driver_state_t *state,
    esp32_mquickjs_spi_device_slot_t *device,
    size_t bytes,
    uint64_t now_us)
{
    esp32_mquickjs_timer_resource_ops_t timer_ops;
    uint64_t interval_us;
    uint64_t deadline_us;
    esp_err_t err;

    if (state == NULL || device == NULL ||
        state->progress_timer.timer == NULL) {
        return false;
    }
    interval_us = esp32_mquickjs_dma_progress_timeout_us(
        bytes, device->actual_freq_hz);
    deadline_us = interval_us > UINT64_MAX - now_us
                      ? UINT64_MAX
                      : now_us + interval_us;
    if (state->operation_deadline_us > 0 &&
        deadline_us > state->operation_deadline_us) {
        deadline_us = state->operation_deadline_us;
    }
    state->progress_deadline_us = deadline_us;
    timer_ops = spi_future_progress_timer_ops(state);
    esp32_mquickjs_timer_resource_stop(&state->progress_timer, &timer_ops);
    if (deadline_us <= now_us) {
        return true;
    }
    state->progress_timer_delay_us = deadline_us - now_us;
    err = esp32_mquickjs_timer_resource_start(&state->progress_timer,
                                              &timer_ops);
    if (err != ESP_OK) {
        spi_future_mark_device_fault(state, device, "DMA_DEVICE_FAULTED", err);
        return false;
    }
    return true;
}

static void spi_future_mark_device_fault(
    esp32_mquickjs_future_driver_state_t *state,
    esp32_mquickjs_spi_device_slot_t *device,
    const char *code,
    esp_err_t err)
{
    if (state != NULL) {
        state->error_code = code;
        state->err = err;
    }
    if (device != NULL) {
        device->faulted = true;
        device->last_error_code = code;
    }
}

static bool spi_future_transaction_index(
    const esp32_mquickjs_future_driver_state_t *state,
    const spi_transaction_t *transaction,
    uint32_t *out_index)
{
    uint32_t index;

    if (state == NULL || transaction == NULL || out_index == NULL) {
        return false;
    }
    for (index = 0; index < state->queue_depth; ++index) {
        if (transaction == &state->transactions[index]) {
            *out_index = index;
            return true;
        }
    }
    return false;
}

static void spi_future_step(esp32_mquickjs_future_driver_state_t *state)
{
    esp32_mquickjs_spi_device_slot_t *device;
    esp32_mquickjs_spi_bus_slot_t *bus;
    spi_transaction_t *completed = NULL;
    uint64_t now_us;
    bool made_progress = false;

    if (state == NULL ||
        atomic_load_explicit(&state->completed, memory_order_acquire)) {
        return;
    }
    device = spi_get_device_slot(&state->device_ref);
    bus = device != NULL
        ? spi_get_bus_slot_by_ids(device->parent_bus_slot_id,
                                  device->parent_bus_generation)
        : NULL;
    if (device == NULL || bus == NULL) {
        if (esp32_mquickjs_dma_completion_queue_releasable(
                &state->completion_queue)) {
            state->error_code = "DMA_DEVICE_FAULTED";
            state->err = ESP_ERR_INVALID_STATE;
            atomic_store_explicit(&state->completed, true,
                                  memory_order_release);
        }
        return;
    }

    /*
     * ESP-IDF may have queued a completed transaction while the JavaScript
     * scheduler was busy. Drain every observable completion before comparing
     * the scheduler's current time with the no-progress deadline. The
     * deadline measures DMA progress, not how promptly the runtime task was
     * scheduled to observe that progress.
     */
    while (state->completion_queue.in_flight > 0) {
        esp32_mquickjs_dma_completion_t completion_order;
        uint32_t expected_index;
        uint32_t transaction_index;
        spi_transaction_meta_t *meta;
        esp_err_t err = spi_device_get_trans_result(
            device->handle, &completed, 0);

        if (err == ESP_ERR_TIMEOUT) {
            break;
        }
        if (!esp32_mquickjs_dma_completion_queue_peek_in_flight(
                &state->completion_queue, &expected_index)) {
            spi_future_mark_device_fault(
                state, device, "DMA_DEVICE_FAULTED",
                ESP_ERR_INVALID_STATE);
            break;
        }
        transaction_index = expected_index;
        if (completed != NULL &&
            !spi_future_transaction_index(
                state, completed, &transaction_index)) {
            spi_future_mark_device_fault(
                state, device, "DMA_DEVICE_FAULTED",
                ESP_ERR_INVALID_STATE);
            break;
        }
        completion_order =
            esp32_mquickjs_dma_completion_queue_note_returned(
                &state->completion_queue, transaction_index);
        if (completion_order == ESP32_MQUICKJS_DMA_COMPLETION_INVALID) {
            spi_future_mark_device_fault(
                state, device, "DMA_DEVICE_FAULTED",
                ESP_ERR_INVALID_STATE);
            break;
        }
        meta = &state->transaction_meta[transaction_index];
        now_us = (uint64_t)esp_timer_get_time();
        if (meta->chunk.rx_data != NULL && err == ESP_OK) {
            if (!esp32_mquickjs_dma_rx_window_fits(
                    meta->output_offset, meta->chunk.length,
                    state->length)) {
                spi_future_mark_device_fault(
                    state, device, "DMA_RX_OVERFLOW", ESP_ERR_INVALID_SIZE);
            } else {
                memcpy(state->rx_data + meta->output_offset,
                       meta->chunk.rx_data, meta->chunk.length);
            }
        }
        if (meta->chunk.uses_staging) {
            esp32_mquickjs_dma_workspace_release(
                &bus->dma_workspace, meta->chunk.slot_index);
        }
        if (err == ESP_OK) {
            esp32_mquickjs_dma_stats_note_transaction(
                &state->dma_stats, meta->chunk.path,
                meta->chunk.length,
                meta->chunk.path == ESP32_MQUICKJS_DMA_PATH_STAGED_INTERNAL
                    ? meta->chunk.length
                    : 0);
            state->last_completion_us = (int64_t)now_us;
            made_progress = true;
        } else {
            spi_future_mark_device_fault(
                state, device, "DMA_DEVICE_FAULTED", err);
        }
        if (completion_order ==
            ESP32_MQUICKJS_DMA_COMPLETION_OUT_OF_ORDER) {
            spi_future_mark_device_fault(
                state, device, "DMA_DEVICE_FAULTED",
                ESP_ERR_INVALID_STATE);
        }
        memset(meta, 0, sizeof(*meta));
        completed = NULL;
    }

    now_us = (uint64_t)esp_timer_get_time();
    if (!state->timeout_triggered && !state->cancelled &&
        state->err == ESP_OK && state->completion_queue.in_flight > 0 &&
        esp32_mquickjs_dma_progress_timed_out(
            now_us, state->progress_deadline_us, made_progress)) {
        state->timeout_triggered = true;
        state->cancelled = true;
        state->error_code = "DMA_TRANSFER_TIMEOUT";
        state->err = ESP_ERR_TIMEOUT;
        device->last_error_code = state->error_code;
        device->faulted = true;
    }

    if (state->completion_queue.in_flight == 0) {
        spi_future_stop_progress_timer(state);
        state->progress_deadline_us = 0;
    } else if (!state->timeout_triggered && made_progress) {
        uint32_t head_index;

        if (esp32_mquickjs_dma_completion_queue_peek_in_flight(
                &state->completion_queue, &head_index)) {
            spi_transaction_meta_t *head =
                &state->transaction_meta[head_index];

            spi_future_arm_progress_timer(
                state, device, head->chunk.length,
                (uint64_t)esp_timer_get_time());
        }
    }

    while (state->err == ESP_OK && !state->cancelled &&
           esp32_mquickjs_dma_completion_queue_can_submit(
               &state->completion_queue)) {
        uint32_t transaction_index;
        spi_transaction_t *transaction =
            NULL;
        spi_transaction_meta_t *meta = NULL;
        esp32_mquickjs_dma_chunk_t chunk;
        bool transmit = state->kind != SPI_FUTURE_READ;
        bool receive = state->kind == SPI_FUTURE_READ ||
                       state->kind == SPI_FUTURE_TRANSFER;
        int64_t copy_started_us;
        int64_t queue_started_us;
        esp_err_t err;

        if (!esp32_mquickjs_dma_completion_queue_next_submit(
                &state->completion_queue, &transaction_index)) {
            break;
        }
        transaction = &state->transactions[transaction_index];
        meta = &state->transaction_meta[transaction_index];

        if (!spi_future_prepare_next_span(state)) {
            break;
        }
        if (!state->bus_acquired) {
            /* ESP-IDF requires portMAX_DELAY here. The native Future resource
               lane guarantees this bus has no earlier transaction in flight,
               so acquisition completes immediately instead of waiting on the
               JavaScript runtime task. */
            err = spi_device_acquire_bus(device->handle, portMAX_DELAY);
            if (err != ESP_OK) {
                spi_future_mark_device_fault(
                    state, device, "DMA_DEVICE_FAULTED", err);
                break;
            }
            state->bus_acquired = true;
        }

        copy_started_us = esp_timer_get_time();
        if (!esp32_mquickjs_dma_cursor_next(
                &state->cursor, &bus->dma_workspace,
                bus->max_transfer_size, transmit, receive, &chunk)) {
            break;
        }
        if (state->kind == SPI_FUTURE_READ) {
            memset(bus->dma_workspace.slots[chunk.slot_index].tx,
                   state->fill_byte, chunk.length);
            chunk.tx_data =
                bus->dma_workspace.slots[chunk.slot_index].tx;
        }
        state->copy_us +=
            (uint64_t)(esp_timer_get_time() - copy_started_us);

        memset(transaction, 0, sizeof(*transaction));
        transaction->length = chunk.length * 8U;
        transaction->rxlength = receive ? chunk.length * 8U : 0;
        transaction->tx_buffer = chunk.tx_data;
        transaction->rx_buffer = chunk.rx_data;
        transaction->user = state;
        if (chunk.keep_cs_active) {
            transaction->flags |= SPI_TRANS_CS_KEEP_ACTIVE;
        }
        spi_mark_external_dma(transaction);
        queue_started_us = esp_timer_get_time();
        err = spi_device_queue_trans(device->handle, transaction, 0);
        state->queue_us +=
            (uint64_t)(esp_timer_get_time() - queue_started_us);
        if (err != ESP_OK) {
            state->cursor.offset -= chunk.length;
            if (chunk.uses_staging) {
                esp32_mquickjs_dma_workspace_release(
                    &bus->dma_workspace, chunk.slot_index);
            }
            if (err != ESP_ERR_TIMEOUT) {
                spi_future_mark_device_fault(
                    state, device, "DMA_DEVICE_FAULTED", err);
            }
            break;
        }
        meta->chunk = chunk;
        meta->queued_us = queue_started_us;
        meta->output_offset = state->queued_stream_bytes;
        state->queued_stream_bytes += chunk.length;
        if (state->first_queue_us == 0) {
            state->first_queue_us = queue_started_us;
        }
        if (!esp32_mquickjs_dma_completion_queue_note_submitted(
                &state->completion_queue, transaction_index)) {
            spi_future_mark_device_fault(
                state, device, "DMA_DEVICE_FAULTED",
                ESP_ERR_INVALID_STATE);
            break;
        }
        spi_future_arm_progress_timer(
            state, device, chunk.length,
            (uint64_t)esp_timer_get_time());
    }

    if (!state->cancelled && state->err == ESP_OK &&
        state->completion_queue.in_flight == 0 && !state->source_done) {
        (void)spi_future_prepare_next_span(state);
    }
    if ((state->source_done || state->cancelled || state->err != ESP_OK ||
         state->source_error_retained) &&
        esp32_mquickjs_dma_completion_queue_releasable(
            &state->completion_queue)) {
        if (state->bus_acquired) {
            spi_device_release_bus(device->handle);
            state->bus_acquired = false;
        }
        state->total_us =
            (uint64_t)(esp_timer_get_time() - state->total_start_us);
        state->transfer_us =
            state->first_queue_us > 0 &&
                    state->last_completion_us >= state->first_queue_us
                ? (uint64_t)(state->last_completion_us -
                             state->first_queue_us)
                : 0;
        atomic_store_explicit(&state->completed, true,
                              memory_order_release);
    }
}

static bool spi_future_start(JSContext *ctx,
                             esp32_mquickjs_runtime_t *runtime,
                             esp32_mquickjs_future_token_t token,
                             esp32_mquickjs_future_driver_state_t *state)
{
    esp32_mquickjs_spi_device_slot_t *device = state != NULL
        ? spi_get_device_slot(&state->device_ref) : NULL;
    esp32_mquickjs_timer_resource_ops_t timer_ops;
    esp_err_t timer_err;

    if (state == NULL || device == NULL) {
        JS_ThrowReferenceError(ctx, "SPI device closed before transaction start");
        return false;
    }
    if (device->busy) {
        JS_ThrowInternalError(ctx, "SPI device is busy");
        return false;
    }
    if (device->faulted) {
        (void)spi_throw_operation_error(
            ctx, "DMA_DEVICE_FAULTED", state->operation,
            ESP_ERR_INVALID_STATE, state->dma_stats.bytes,
            esp32_mquickjs_dma_stats_path(&state->dma_stats), device);
        return false;
    }
    device->busy = true;
    state->runtime = runtime;
    state->token = token;
    state->started = true;
    state->total_start_us = esp_timer_get_time();
    state->operation_deadline_us =
        (uint64_t)state->total_start_us +
        ((uint64_t)state->timeout_ms * 1000ULL);
    timer_ops = spi_future_progress_timer_ops(state);
    timer_err = esp32_mquickjs_timer_resource_acquire(
        &state->progress_timer, &timer_ops);
    if (timer_err != ESP_OK) {
        state->err = timer_err;
        state->error_code = "DMA_STAGING_NO_MEMORY";
        (void)spi_throw_operation_error(
            ctx, state->error_code, state->operation,
            timer_err, 0,
            ESP32_MQUICKJS_DMA_PATH_DIRECT_INTERNAL, device);
        return false;
    }
    spi_future_step(state);
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
    esp32_mquickjs_spi_device_slot_t *device = state != NULL
        ? spi_get_device_slot(&state->device_ref) : NULL;

    if (state == NULL) {
        return JS_ThrowInternalError(ctx, "SPI transaction state is missing");
    }
    if (state->error_code != NULL) {
        if (device != NULL) {
            device->last_error_code = state->error_code;
        }
        return spi_throw_operation_error(
            ctx, state->error_code, state->operation, state->err,
            state->dma_stats.bytes,
            esp32_mquickjs_dma_stats_path(&state->dma_stats), device);
    }
    if (state->cancelled) {
        return JS_ThrowInternalError(ctx, "SPI transaction cancelled");
    }
    if (state->source_error_retained) {
        return JS_Throw(ctx, state->source_error_ref.val);
    }
    if (state->err != ESP_OK) {
        return spi_throw_error(ctx, state->err, "SPI transaction failed");
    }
    if (spi_future_is_bulk(state)) {
        uint64_t accounted_us = state->copy_us + state->queue_us;
        uint64_t wait_us = state->total_us > accounted_us
            ? state->total_us - accounted_us : 0U;

        return spi_make_write_chunks_stats(
            ctx, &state->dma_stats, state->copy_us,
            state->queue_us, wait_us, state->transfer_us,
            state->total_us, state->queue_depth);
    }
    if (state->kind == SPI_FUTURE_WRITE) {
        return JS_NewInt64(ctx, (int64_t)state->dma_stats.bytes);
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
    esp32_mquickjs_dma_cancel_t disposition;

    if (state == NULL) {
        return ESP32_MQUICKJS_CANCEL_REJECTED;
    }
    disposition = esp32_mquickjs_dma_cancel_disposition(
        &state->completion_queue,
        atomic_load_explicit(&state->completed, memory_order_acquire),
        state->cancelled);
    if (disposition == ESP32_MQUICKJS_DMA_CANCEL_REJECTED) {
        return ESP32_MQUICKJS_CANCEL_REJECTED;
    }
    state->cancelled = true;
    if (disposition == ESP32_MQUICKJS_DMA_CANCEL_COMPLETE) {
        atomic_store_explicit(&state->completed, true,
                              memory_order_release);
    }
    if (state->runtime != NULL) {
        (void)esp32_mquickjs_future_wake(state->runtime, state->token);
    }
    return disposition == ESP32_MQUICKJS_DMA_CANCEL_PENDING
               ? ESP32_MQUICKJS_CANCEL_REQUESTED
               : ESP32_MQUICKJS_CANCELLED;
}

static uint32_t spi_future_timeout_ms(
    const esp32_mquickjs_future_driver_state_t *state)
{
    return state != NULL ? state->timeout_ms : 0;
}

static JSValue spi_future_on_timeout(
    JSContext *ctx,
    esp32_mquickjs_future_driver_state_t *state,
    uint32_t timeout_ms)
{
    esp32_mquickjs_spi_device_slot_t *device = state != NULL
        ? spi_get_device_slot(&state->device_ref) : NULL;

    (void)timeout_ms;
    if (state == NULL) {
        return spi_throw_operation_error(
            ctx, "DMA_TRANSFER_TIMEOUT", "unknown", ESP_ERR_TIMEOUT,
            0, ESP32_MQUICKJS_DMA_PATH_DIRECT_INTERNAL, device);
    }
    state->timeout_triggered = true;
    state->cancelled = true;
    state->error_code = "DMA_TRANSFER_TIMEOUT";
    state->err = ESP_ERR_TIMEOUT;
    if (device != NULL) {
        device->last_error_code = state->error_code;
        if (state->completion_queue.in_flight > 0) {
            device->faulted = true;
        }
    }
    if (esp32_mquickjs_dma_completion_queue_releasable(
            &state->completion_queue)) {
        atomic_store_explicit(&state->completed, true,
                              memory_order_release);
    }
    return spi_throw_operation_error(
        ctx, state->error_code, state->operation, state->err,
        state->dma_stats.bytes,
        esp32_mquickjs_dma_stats_path(&state->dma_stats), device);
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
        .timeout_ms = spi_future_timeout_ms, \
        .on_timeout = spi_future_on_timeout, \
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
