#include "esp32_mquickjs_uart.h"

#if CONFIG_ESP32_MQUICKJS_FEATURE_UART

#include "utils/esp32_mquickjs_byte_source.h"
#include "esp32_mquickjs_core.h"
#include "esp32_mquickjs_future.h"

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "soc/soc_caps.h"

typedef struct {
    int32_t port_id;
    uint32_t generation;
} esp32_mquickjs_uart_port_ref_t;

typedef struct {
    bool allocated;
    int32_t port_id;
    uint32_t generation;
    int32_t tx_pin;
    int32_t rx_pin;
    uint32_t baud;
    uart_word_length_t data_bits;
    uart_parity_t parity;
    uart_stop_bits_t stop_bits;
    uint32_t rx_buffer_size;
    uint32_t tx_buffer_size;
    uint32_t timeout_ms;
    bool busy;
} esp32_mquickjs_uart_slot_t;

static esp32_mquickjs_uart_slot_t s_uart_slots[SOC_UART_NUM];
static uint32_t s_uart_next_generation = 1;

static bool uart_register_future_drivers(JSContext *ctx,
                                         esp32_mquickjs_runtime_t *runtime);

static bool js_value_to_i32(JSContext *ctx, JSValue value, int32_t *out_value)
{
    int raw_value = 0;

    if (JS_ToInt32(ctx, &raw_value, value) != 0) {
        return false;
    }
    *out_value = (int32_t)raw_value;
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

static bool js_value_to_uart_port(JSContext *ctx, JSValue value, int32_t *out_port)
{
    int32_t port = -1;

    if (!js_value_to_i32(ctx, value, &port) || port < 0 || port >= (int32_t)SOC_UART_NUM) {
        return false;
    }
    *out_port = port;
    return true;
}

static bool js_value_to_gpio_num(JSContext *ctx, JSValue value, int32_t *out_pin)
{
    int32_t pin = -1;

    if (!js_value_to_i32(ctx, value, &pin)) {
        return false;
    }
    if (pin == -1) {
        *out_pin = -1;
        return true;
    }
    if (pin < 0 || pin >= GPIO_NUM_MAX || !GPIO_IS_VALID_GPIO(pin)) {
        return false;
    }

    *out_pin = pin;
    return true;
}

static bool js_value_to_uart_data_bits(JSContext *ctx, JSValue value, uart_word_length_t *out_data_bits)
{
    int32_t bits = 0;

    if (!js_value_to_i32(ctx, value, &bits)) {
        return false;
    }

    switch (bits) {
    case 5:
        *out_data_bits = UART_DATA_5_BITS;
        return true;
    case 6:
        *out_data_bits = UART_DATA_6_BITS;
        return true;
    case 7:
        *out_data_bits = UART_DATA_7_BITS;
        return true;
    case 8:
        *out_data_bits = UART_DATA_8_BITS;
        return true;
    default:
        return false;
    }
}

static int32_t uart_data_bits_to_number(uart_word_length_t data_bits)
{
    switch (data_bits) {
    case UART_DATA_5_BITS:
        return 5;
    case UART_DATA_6_BITS:
        return 6;
    case UART_DATA_7_BITS:
        return 7;
    case UART_DATA_8_BITS:
    default:
        return 8;
    }
}

static bool js_value_to_uart_parity(JSContext *ctx, JSValue value, uart_parity_t *out_parity)
{
    JSCStringBuf parity_buf;
    const char *parity;

    if (!JS_IsString(ctx, value)) {
        return false;
    }

    parity = JS_ToCString(ctx, value, &parity_buf);
    if (parity == NULL) {
        return false;
    }

    if (strcmp(parity, "none") == 0) {
        *out_parity = UART_PARITY_DISABLE;
        return true;
    }
    if (strcmp(parity, "even") == 0) {
        *out_parity = UART_PARITY_EVEN;
        return true;
    }
    if (strcmp(parity, "odd") == 0) {
        *out_parity = UART_PARITY_ODD;
        return true;
    }
    return false;
}

static const char *uart_parity_to_string(uart_parity_t parity)
{
    switch (parity) {
    case UART_PARITY_EVEN:
        return "even";
    case UART_PARITY_ODD:
        return "odd";
    case UART_PARITY_DISABLE:
    default:
        return "none";
    }
}

static bool js_value_to_uart_stop_bits(JSContext *ctx, JSValue value, uart_stop_bits_t *out_stop_bits)
{
    double stop_bits = 0.0;

    if (JS_ToNumber(ctx, &stop_bits, value) != 0) {
        return false;
    }

    if (stop_bits == 1.0) {
        *out_stop_bits = UART_STOP_BITS_1;
        return true;
    }
    if (stop_bits == 1.5) {
        *out_stop_bits = UART_STOP_BITS_1_5;
        return true;
    }
    if (stop_bits == 2.0) {
        *out_stop_bits = UART_STOP_BITS_2;
        return true;
    }
    return false;
}

static JSValue uart_stop_bits_to_value(JSContext *ctx, uart_stop_bits_t stop_bits)
{
    switch (stop_bits) {
    case UART_STOP_BITS_1_5:
        return JS_NewFloat64(ctx, 1.5);
    case UART_STOP_BITS_2:
        return JS_NewInt32(ctx, 2);
    case UART_STOP_BITS_1:
    default:
        return JS_NewInt32(ctx, 1);
    }
}

static JSValue uart_throw_error(JSContext *ctx,
                                esp_err_t err,
                                const char *message)
{
    return JS_ThrowInternalError(ctx, "%s: %s", message, esp_err_to_name(err));
}

static void uart_init_slot(esp32_mquickjs_uart_slot_t *slot, int32_t port_id)
{
    memset(slot, 0, sizeof(*slot));
    slot->port_id = port_id;
}

static void uart_reset_slots(void)
{
    int32_t i;

    for (i = 0; i < (int32_t)SOC_UART_NUM; ++i) {
        uart_init_slot(&s_uart_slots[i], i);
    }
    s_uart_next_generation = 1;
}

static uint32_t uart_take_generation(void)
{
    uint32_t generation = s_uart_next_generation++;

    if (generation == 0) {
        generation = s_uart_next_generation++;
    }
    return generation;
}

static esp32_mquickjs_uart_slot_t *uart_alloc_slot(int32_t port_id)
{
    esp32_mquickjs_uart_slot_t *slot;

    if (port_id < 0 || port_id >= (int32_t)SOC_UART_NUM) {
        return NULL;
    }

    slot = &s_uart_slots[port_id];
    if (slot->allocated || uart_is_driver_installed((uart_port_t)port_id)) {
        return NULL;
    }

    uart_init_slot(slot, port_id);
    slot->allocated = true;
    slot->generation = uart_take_generation();
    return slot;
}

static void uart_cleanup_slot(esp32_mquickjs_uart_slot_t *slot)
{
    int32_t port_id;

    if (slot == NULL || !slot->allocated) {
        return;
    }

    port_id = slot->port_id;
    if (uart_is_driver_installed((uart_port_t)port_id)) {
        uart_driver_delete((uart_port_t)port_id);
    }
    uart_init_slot(slot, port_id);
}

static esp32_mquickjs_uart_slot_t *uart_get_slot(const esp32_mquickjs_uart_port_ref_t *ref)
{
    esp32_mquickjs_uart_slot_t *slot;

    if (ref == NULL || ref->port_id < 0 || ref->port_id >= (int32_t)SOC_UART_NUM) {
        return NULL;
    }

    slot = &s_uart_slots[ref->port_id];
    if (!slot->allocated || slot->generation != ref->generation) {
        return NULL;
    }
    return slot;
}

static JSValue uart_bytes_to_array(JSContext *ctx, const uint8_t *bytes, size_t length)
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

static JSValue uart_make_write_stats(JSContext *ctx,
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

static JSValue uart_make_status_object(JSContext *ctx, const esp32_mquickjs_uart_slot_t *slot)
{
    JSGCRef status_ref;
    JSGCRef stop_bits_ref;
    JSValue *status_obj;
    JSValue *stop_bits_value;

    status_obj = JS_PushGCRef(ctx, &status_ref);
    stop_bits_value = JS_PushGCRef(ctx, &stop_bits_ref);
    *status_obj = JS_NewObject(ctx);
    *stop_bits_value = JS_UNDEFINED;
    if (JS_IsException(*status_obj)) {
        JS_PopGCRef(ctx, &stop_bits_ref);
        JS_PopGCRef(ctx, &status_ref);
        return JS_EXCEPTION;
    }

    *stop_bits_value = uart_stop_bits_to_value(ctx, slot != NULL ? slot->stop_bits : UART_STOP_BITS_1);
    if (JS_IsException(*stop_bits_value) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "opened",
                                     JS_NewBool(slot != NULL && slot->allocated)) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "port",
                                     JS_NewInt32(ctx, slot != NULL ? slot->port_id : ESP32_MQUICKJS_UART_DEFAULT_PORT)) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "tx",
                                     JS_NewInt32(ctx, slot != NULL ? slot->tx_pin : ESP32_MQUICKJS_UART_DEFAULT_TX_PIN)) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "rx",
                                     JS_NewInt32(ctx, slot != NULL ? slot->rx_pin : ESP32_MQUICKJS_UART_DEFAULT_RX_PIN)) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "baud",
                                     JS_NewUint32(ctx, slot != NULL ? slot->baud : ESP32_MQUICKJS_UART_DEFAULT_BAUD)) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "dataBits",
                                     JS_NewInt32(ctx, slot != NULL ? uart_data_bits_to_number(slot->data_bits) : 8)) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "parity",
                                     JS_NewString(ctx, slot != NULL ? uart_parity_to_string(slot->parity) : "none")) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "stopBits", *stop_bits_value) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "rxBufferSize",
                                     JS_NewUint32(ctx, slot != NULL ? slot->rx_buffer_size : ESP32_MQUICKJS_UART_DEFAULT_RX_BUFFER_SIZE)) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "txBufferSize",
                                     JS_NewUint32(ctx, slot != NULL ? slot->tx_buffer_size : ESP32_MQUICKJS_UART_DEFAULT_TX_BUFFER_SIZE)) ||
        !esp32_mquickjs_set_property_ref(ctx, status_obj, "timeoutMs",
                                     JS_NewUint32(ctx, slot != NULL ? slot->timeout_ms : ESP32_MQUICKJS_UART_DEFAULT_TIMEOUT_MS))) {
        JS_PopGCRef(ctx, &stop_bits_ref);
        JS_PopGCRef(ctx, &status_ref);
        return JS_EXCEPTION;
    }

    JS_PopGCRef(ctx, &stop_bits_ref);
    return JS_PopGCRef(ctx, &status_ref);
}

static JSValue uart_make_port_object(JSContext *ctx, const esp32_mquickjs_uart_slot_t *slot)
{
    JSGCRef port_ref;
    JSValue *port_obj;
    esp32_mquickjs_uart_port_ref_t *port_data;

    port_obj = JS_PushGCRef(ctx, &port_ref);
    *port_obj = JS_NewObjectClassUser(ctx, JS_CLASS_UART_PORT);
    if (JS_IsException(*port_obj) || slot == NULL) {
        JS_PopGCRef(ctx, &port_ref);
        return JS_EXCEPTION;
    }

    port_data = heap_caps_malloc(sizeof(*port_data), MALLOC_CAP_8BIT);
    if (port_data == NULL) {
        JS_PopGCRef(ctx, &port_ref);
        return JS_ThrowOutOfMemory(ctx);
    }
    port_data->port_id = slot->port_id;
    port_data->generation = slot->generation;
    JS_SetOpaque(ctx, *port_obj, port_data);

    return JS_PopGCRef(ctx, &port_ref);
}

static int uart_port_ref_from_object(JSContext *ctx,
                                     JSValue port_value,
                                     const char *api_name,
                                     esp32_mquickjs_uart_port_ref_t *out_ref)
{
    const esp32_mquickjs_uart_port_ref_t *port_ref;

    if (out_ref == NULL || JS_GetClassID(ctx, port_value) != JS_CLASS_UART_PORT) {
        JS_ThrowTypeError(ctx, "%s expects a valid UARTPort instance", api_name);
        return -1;
    }
    port_ref = JS_GetOpaque(ctx, port_value);
    if (port_ref == NULL) {
        JS_ThrowTypeError(ctx, "%s expects a valid UARTPort instance", api_name);
        return -1;
    }
    *out_ref = *port_ref;
    return 0;
}

static int uart_get_bound_slot(JSContext *ctx,
                               JSValue port_value,
                               const char *api_name,
                               esp32_mquickjs_uart_port_ref_t *out_ref,
                               esp32_mquickjs_uart_slot_t **out_slot)
{
    esp32_mquickjs_uart_slot_t *slot;

    if (uart_port_ref_from_object(ctx, port_value, api_name, out_ref) != 0) {
        return -1;
    }
    slot = uart_get_slot(out_ref);
    if (slot == NULL) {
        JS_ThrowReferenceError(ctx, "%s failed because the UART port is closed", api_name);
        return -1;
    }
    if (out_slot != NULL) {
        *out_slot = slot;
    }
    return 0;
}

static JSValue uart_write_bytes_from_source(JSContext *ctx,
                                            const esp32_mquickjs_uart_slot_t *slot,
                                            const uint8_t *data,
                                            size_t length,
                                            const char *api_name)
{
    int written;

    if (length == 0) {
        return JS_NewInt32(ctx, 0);
    }
    if (data == NULL) {
        return JS_ThrowInternalError(ctx, "%s received non-empty byte source with null data", api_name);
    }
    if (length > INT_MAX) {
        return JS_ThrowRangeError(ctx, "%s byte length exceeds supported UART write size", api_name);
    }

    written = uart_write_bytes((uart_port_t)slot->port_id, data, length);
    if (written < 0) {
        return JS_ThrowInternalError(ctx, "%s failed", api_name);
    }
    return JS_NewInt32(ctx, written);
}

static JSValue uart_write(JSContext *ctx,
                          const esp32_mquickjs_uart_slot_t *slot,
                          JSValue data_value)
{
    esp32_mquickjs_byte_source_t source;
    uint8_t *owned = NULL;
    JSValue error = JS_UNDEFINED;
    JSValue result;

    if (!esp32_mquickjs_get_byte_source(ctx, data_value, "UARTPort.write(data)", &source, &owned, &error)) {
        return error;
    }

    result = uart_write_bytes_from_source(ctx, slot, source.data, source.length, "UARTPort.write()");
    esp32_mquickjs_release_byte_source(owned);
    return result;
}

static JSValue uart_write_chunks(JSContext *ctx,
                                 const esp32_mquickjs_uart_slot_t *slot,
                                 JSValue chunks_value)
{
    uint32_t chunk_count = 0;
    uint32_t chunks = 0;
    uint32_t index;
    size_t bytes = 0;
    int64_t total_start;
    JSValue error = JS_UNDEFINED;

    if (!esp32_mquickjs_get_byte_source_array_length(ctx,
                                                     chunks_value,
                                                     "UARTPort.writeChunks(chunks)",
                                                     &chunk_count,
                                                     &error)) {
        return JS_IsUndefined(error)
                   ? JS_ThrowTypeError(ctx, "UARTPort.writeChunks(chunks) expects an array-like object")
                   : error;
    }

    total_start = esp_timer_get_time();
    for (index = 0; index < chunk_count; ++index) {
        esp32_mquickjs_byte_source_chunk_t chunk;
        int written;

        if (!esp32_mquickjs_get_byte_source_chunk(ctx,
                                                  chunks_value,
                                                  index,
                                                  "UARTPort.writeChunks(chunks)",
                                                  &chunk,
                                                  &error)) {
            return JS_IsUndefined(error)
                       ? JS_ThrowTypeError(ctx, "UARTPort.writeChunks(chunks) expects byte-source chunks")
                       : error;
        }
        if (chunk.source.length == 0) {
            esp32_mquickjs_release_byte_source_chunk(ctx, &chunk);
            continue;
        }
        if (chunk.source.length > INT_MAX) {
            esp32_mquickjs_release_byte_source_chunk(ctx, &chunk);
            return JS_ThrowRangeError(ctx, "UARTPort.writeChunks(chunks) byte length exceeds supported UART write size");
        }
        written = uart_write_bytes((uart_port_t)slot->port_id, chunk.source.data, chunk.source.length);
        if (written < 0) {
            esp32_mquickjs_release_byte_source_chunk(ctx, &chunk);
            return JS_ThrowInternalError(ctx, "UARTPort.writeChunks() failed");
        }
        bytes += (size_t)written;
        chunks++;
        esp32_mquickjs_release_byte_source_chunk(ctx, &chunk);
    }

    return uart_make_write_stats(ctx,
                                 chunks,
                                 bytes,
                                 (uint64_t)(esp_timer_get_time() - total_start));
}

static JSValue uart_write_span_source(JSContext *ctx,
                                      const esp32_mquickjs_uart_slot_t *slot,
                                      JSValue source_value)
{
    esp32_mquickjs_byte_span_source_t source;
    uint32_t chunks = 0;
    size_t bytes = 0;
    int64_t total_start;
    JSValue error = JS_UNDEFINED;

    if (!esp32_mquickjs_open_byte_span_source(ctx,
                                              source_value,
                                              "UARTPort.writeSource(source)",
                                              &source,
                                              &error)) {
        return JS_IsUndefined(error)
                   ? JS_ThrowTypeError(ctx, "UARTPort.writeSource(source) expects a ByteSpanSource")
                   : error;
    }

    total_start = esp_timer_get_time();
    while (true) {
        esp32_mquickjs_byte_span_t span;
        int written;

        if (!esp32_mquickjs_byte_span_source_next(ctx, &source, &span)) {
            error = JS_GetException(ctx);
            if (!JS_IsUndefined(error) && !JS_IsNull(error)) {
                esp32_mquickjs_byte_span_source_close(ctx, &source);
                return error;
            }
            break;
        }
        if (span.length == 0) {
            continue;
        }
        if (span.data == NULL) {
            esp32_mquickjs_byte_span_source_close(ctx, &source);
            return JS_ThrowInternalError(ctx, "UARTPort.writeSource() received a non-empty span with null data");
        }
        if (span.length > INT_MAX) {
            esp32_mquickjs_byte_span_source_close(ctx, &source);
            return JS_ThrowRangeError(ctx, "UARTPort.writeSource() span length exceeds supported UART write size");
        }
        written = uart_write_bytes((uart_port_t)slot->port_id, span.data, span.length);
        if (written < 0) {
            esp32_mquickjs_byte_span_source_close(ctx, &source);
            return JS_ThrowInternalError(ctx, "UARTPort.writeSource() failed");
        }
        bytes += (size_t)written;
        chunks++;
    }

    esp32_mquickjs_byte_span_source_close(ctx, &source);
    return uart_make_write_stats(ctx,
                                 chunks,
                                 bytes,
                                 (uint64_t)(esp_timer_get_time() - total_start));
}

static JSValue uart_open(JSContext *ctx, int argc, JSValue *argv)
{
    int32_t port_id = ESP32_MQUICKJS_UART_DEFAULT_PORT;
    int32_t tx_pin = ESP32_MQUICKJS_UART_DEFAULT_TX_PIN;
    int32_t rx_pin = ESP32_MQUICKJS_UART_DEFAULT_RX_PIN;
    uint32_t baud = ESP32_MQUICKJS_UART_DEFAULT_BAUD;
    uart_word_length_t data_bits = UART_DATA_8_BITS;
    uart_parity_t parity = UART_PARITY_DISABLE;
    uart_stop_bits_t stop_bits = UART_STOP_BITS_1;
    uint32_t rx_buffer_size = ESP32_MQUICKJS_UART_DEFAULT_RX_BUFFER_SIZE;
    uint32_t tx_buffer_size = ESP32_MQUICKJS_UART_DEFAULT_TX_BUFFER_SIZE;
    uint32_t timeout_ms = ESP32_MQUICKJS_UART_DEFAULT_TIMEOUT_MS;
    uart_config_t uart_config = {0};
    esp32_mquickjs_uart_slot_t *slot;
    JSValue result;
    esp_err_t err;

    if (argc >= 1 && !JS_IsUndefined(argv[0])) {
        JSGCRef property_ref;
        JSValue *property;

        if (JS_GetClassID(ctx, argv[0]) < 0) {
            return JS_ThrowTypeError(ctx,
                                     "uart.open(options?) expects an object with port/tx/rx/baud/dataBits/parity/stopBits");
        }

        property = JS_PushGCRef(ctx, &property_ref);

        *property = JS_GetPropertyStr(ctx, argv[0], "port");
        if (JS_IsException(*property)) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_EXCEPTION;
        }
        if (!JS_IsUndefined(*property) && !js_value_to_uart_port(ctx, *property, &port_id)) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_ThrowTypeError(ctx, "uart.open({ port }) expects a valid UART port number");
        }

        *property = JS_GetPropertyStr(ctx, argv[0], "tx");
        if (JS_IsException(*property)) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_EXCEPTION;
        }
        if (!JS_IsUndefined(*property) && !js_value_to_gpio_num(ctx, *property, &tx_pin)) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_ThrowTypeError(ctx, "uart.open({ tx }) expects a valid GPIO or -1");
        }

        *property = JS_GetPropertyStr(ctx, argv[0], "rx");
        if (JS_IsException(*property)) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_EXCEPTION;
        }
        if (!JS_IsUndefined(*property) && !js_value_to_gpio_num(ctx, *property, &rx_pin)) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_ThrowTypeError(ctx, "uart.open({ rx }) expects a valid GPIO or -1");
        }

        *property = JS_GetPropertyStr(ctx, argv[0], "baud");
        if (JS_IsException(*property)) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_EXCEPTION;
        }
        if (!JS_IsUndefined(*property) &&
            (!js_value_to_u32(ctx, *property, &baud) || baud == 0U)) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_ThrowTypeError(ctx, "uart.open({ baud }) expects a positive integer");
        }

        *property = JS_GetPropertyStr(ctx, argv[0], "dataBits");
        if (JS_IsException(*property)) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_EXCEPTION;
        }
        if (!JS_IsUndefined(*property) && !js_value_to_uart_data_bits(ctx, *property, &data_bits)) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_ThrowTypeError(ctx, "uart.open({ dataBits }) expects 5, 6, 7, or 8");
        }

        *property = JS_GetPropertyStr(ctx, argv[0], "parity");
        if (JS_IsException(*property)) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_EXCEPTION;
        }
        if (!JS_IsUndefined(*property) && !js_value_to_uart_parity(ctx, *property, &parity)) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_ThrowTypeError(ctx, "uart.open({ parity }) expects \"none\", \"even\", or \"odd\"");
        }

        *property = JS_GetPropertyStr(ctx, argv[0], "stopBits");
        if (JS_IsException(*property)) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_EXCEPTION;
        }
        if (!JS_IsUndefined(*property) && !js_value_to_uart_stop_bits(ctx, *property, &stop_bits)) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_ThrowTypeError(ctx, "uart.open({ stopBits }) expects 1, 1.5, or 2");
        }

        *property = JS_GetPropertyStr(ctx, argv[0], "rxBufferSize");
        if (JS_IsException(*property)) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_EXCEPTION;
        }
        if (!JS_IsUndefined(*property) && !js_value_to_u32(ctx, *property, &rx_buffer_size)) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_ThrowTypeError(ctx, "uart.open({ rxBufferSize }) expects a non-negative integer");
        }

        *property = JS_GetPropertyStr(ctx, argv[0], "txBufferSize");
        if (JS_IsException(*property)) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_EXCEPTION;
        }
        if (!JS_IsUndefined(*property) && !js_value_to_u32(ctx, *property, &tx_buffer_size)) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_ThrowTypeError(ctx, "uart.open({ txBufferSize }) expects a non-negative integer");
        }

        *property = JS_GetPropertyStr(ctx, argv[0], "timeoutMs");
        if (JS_IsException(*property)) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_EXCEPTION;
        }
        if (!JS_IsUndefined(*property) && !js_value_to_u32(ctx, *property, &timeout_ms)) {
            JS_PopGCRef(ctx, &property_ref);
            return JS_ThrowTypeError(ctx, "uart.open({ timeoutMs }) expects a non-negative integer");
        }

        JS_PopGCRef(ctx, &property_ref);
    }

    if (tx_pin < 0 && rx_pin < 0) {
        return JS_ThrowTypeError(ctx, "uart.open() requires a TX or RX GPIO");
    }
    if (tx_pin >= 0 && !GPIO_IS_VALID_OUTPUT_GPIO(tx_pin)) {
        return JS_ThrowTypeError(ctx, "uart.open() requires TX to be an output-capable GPIO or -1");
    }
    if (rx_pin >= 0 && !GPIO_IS_VALID_GPIO(rx_pin)) {
        return JS_ThrowTypeError(ctx, "uart.open() requires RX to be a valid GPIO or -1");
    }
    if (rx_buffer_size <= UART_HW_FIFO_LEN((uart_port_t)port_id)) {
        return JS_ThrowTypeError(ctx, "uart.open({ rxBufferSize }) must be greater than the UART hardware FIFO length");
    }
    if (tx_buffer_size != 0U && tx_buffer_size <= UART_HW_FIFO_LEN((uart_port_t)port_id)) {
        return JS_ThrowTypeError(ctx, "uart.open({ txBufferSize }) must be zero or greater than the UART hardware FIFO length");
    }
#if defined(CONFIG_ESP_CONSOLE_UART_NUM) && CONFIG_ESP_CONSOLE_UART_NUM >= 0
    if (port_id == CONFIG_ESP_CONSOLE_UART_NUM) {
        return JS_ThrowInternalError(ctx, "uart.open() refused to take the console UART port");
    }
#endif

    slot = uart_alloc_slot(port_id);
    if (slot == NULL) {
        return JS_ThrowInternalError(ctx, "uart.open() failed: selected UART port is unavailable or already in use");
    }

    uart_config.baud_rate = (int)baud;
    uart_config.data_bits = data_bits;
    uart_config.parity = parity;
    uart_config.stop_bits = stop_bits;
    uart_config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    uart_config.rx_flow_ctrl_thresh = 0;
    uart_config.source_clk = UART_SCLK_DEFAULT;

    err = uart_param_config((uart_port_t)port_id, &uart_config);
    if (err != ESP_OK) {
        uart_cleanup_slot(slot);
        return uart_throw_error(ctx, err, "uart.open() failed to configure parameters");
    }

    err = uart_set_pin((uart_port_t)port_id,
                       tx_pin,
                       rx_pin,
                       UART_PIN_NO_CHANGE,
                       UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        uart_cleanup_slot(slot);
        return uart_throw_error(ctx, err, "uart.open() failed to configure pins");
    }

    err = uart_driver_install((uart_port_t)port_id,
                              (int)rx_buffer_size,
                              (int)tx_buffer_size,
                              0,
                              NULL,
                              0);
    if (err != ESP_OK) {
        uart_cleanup_slot(slot);
        return uart_throw_error(ctx, err, "uart.open() failed to install driver");
    }

    slot->tx_pin = tx_pin;
    slot->rx_pin = rx_pin;
    slot->baud = baud;
    slot->data_bits = data_bits;
    slot->parity = parity;
    slot->stop_bits = stop_bits;
    slot->rx_buffer_size = rx_buffer_size;
    slot->tx_buffer_size = tx_buffer_size;
    slot->timeout_ms = timeout_ms;

    result = uart_make_port_object(ctx, slot);
    if (JS_IsException(result)) {
        uart_cleanup_slot(slot);
    }
    return result;
}

void esp32_mquickjs_deinit_uart_runtime(void)
{
    int32_t i;

    for (i = 0; i < (int32_t)SOC_UART_NUM; ++i) {
        if (s_uart_slots[i].allocated) {
            uart_cleanup_slot(&s_uart_slots[i]);
        }
    }
    uart_reset_slots();
}

bool esp32_mquickjs_init_uart_runtime(JSContext *ctx,
                                      esp32_mquickjs_runtime_t *runtime)
{
    esp32_mquickjs_deinit_uart_runtime();
    return uart_register_future_drivers(ctx, runtime);
}

JSValue js_uart_port_constructor(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_ThrowTypeError(ctx, "UARTPort cannot be constructed directly");
}

void js_uart_port_finalizer(JSContext *ctx, void *opaque)
{
    esp32_mquickjs_uart_port_ref_t *port_ref = opaque;
    esp32_mquickjs_uart_slot_t *slot;

    (void)ctx;

    if (port_ref == NULL) {
        return;
    }

    slot = uart_get_slot(port_ref);
    if (slot != NULL) {
        uart_cleanup_slot(slot);
    }
    heap_caps_free(port_ref);
}

JSValue js_uart_port_close(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    esp32_mquickjs_uart_port_ref_t port_ref;
    esp32_mquickjs_uart_port_ref_t *port_ref_ptr;
    esp32_mquickjs_uart_slot_t *slot;

    (void)argc;
    (void)argv;

    if (uart_port_ref_from_object(ctx, *this_val, "UARTPort.close()", &port_ref) != 0) {
        return JS_EXCEPTION;
    }
    slot = uart_get_slot(&port_ref);
    if (slot != NULL) {
        if (slot->busy) {
            return JS_ThrowInternalError(ctx,
                                         "UARTPort.close() refused while an operation is pending");
        }
        uart_cleanup_slot(slot);
    }
    port_ref_ptr = JS_GetOpaque(ctx, *this_val);
    if (port_ref_ptr != NULL) {
        port_ref_ptr->port_id = -1;
        port_ref_ptr->generation = 0;
    }
    return JS_TRUE;
}

JSValue js_uart_port_status(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    esp32_mquickjs_uart_port_ref_t port_ref;
    esp32_mquickjs_uart_slot_t *slot = NULL;

    (void)argc;
    (void)argv;

    if (uart_get_bound_slot(ctx, *this_val, "UARTPort.status()", &port_ref, &slot) != 0) {
        return JS_EXCEPTION;
    }
    return uart_make_status_object(ctx, slot);
}

JSValue js_uart_port_write(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    esp32_mquickjs_uart_port_ref_t port_ref;
    esp32_mquickjs_uart_slot_t *slot = NULL;

    if (uart_get_bound_slot(ctx, *this_val, "UARTPort.write()", &port_ref, &slot) != 0) {
        return JS_EXCEPTION;
    }
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "UARTPort.write(data) expects byte data");
    }
    return uart_write(ctx, slot, argv[0]);
}

JSValue js_uart_port_write_chunks(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    esp32_mquickjs_uart_port_ref_t port_ref;
    esp32_mquickjs_uart_slot_t *slot = NULL;

    if (uart_get_bound_slot(ctx, *this_val, "UARTPort.writeChunks()", &port_ref, &slot) != 0) {
        return JS_EXCEPTION;
    }
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "UARTPort.writeChunks(chunks) expects byte-source chunks");
    }
    return uart_write_chunks(ctx, slot, argv[0]);
}

JSValue js_uart_port_write_source(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    esp32_mquickjs_uart_port_ref_t port_ref;
    esp32_mquickjs_uart_slot_t *slot = NULL;

    if (uart_get_bound_slot(ctx, *this_val, "UARTPort.writeSource()", &port_ref, &slot) != 0) {
        return JS_EXCEPTION;
    }
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "UARTPort.writeSource(source) expects a ByteSpanSource");
    }
    return uart_write_span_source(ctx, slot, argv[0]);
}

typedef enum {
    UART_FUTURE_READ,
    UART_FUTURE_FLUSH,
} uart_future_kind_t;

struct esp32_mquickjs_future_driver_state {
    uart_future_kind_t kind;
    JSContext *ctx;
    JSGCRef owner_ref;
    esp32_mquickjs_uart_port_ref_t port_ref;
    esp32_mquickjs_runtime_t *runtime;
    esp32_mquickjs_future_token_t token;
    esp_timer_handle_t poll_timer;
    uint64_t deadline_us;
    uint32_t timeout_ms;
    uint32_t length;
    uint8_t *data;
    int read_length;
    esp_err_t err;
    bool owner_retained;
    bool started;
    bool completed;
    bool cancelled;
};

static esp32_mquickjs_future_driver_state_t *uart_future_allocate(
    JSContext *ctx,
    JSValue this_value,
    uart_future_kind_t kind)
{
    esp32_mquickjs_future_driver_state_t *state;
    esp32_mquickjs_uart_slot_t *slot = NULL;
    JSValue *owner;

    state = heap_caps_calloc(1, sizeof(*state), MALLOC_CAP_8BIT);
    if (state == NULL) {
        JS_ThrowOutOfMemory(ctx);
        return NULL;
    }
    if (uart_get_bound_slot(ctx, this_value, "UART Future operation",
                            &state->port_ref, &slot) != 0) {
        heap_caps_free(state);
        return NULL;
    }
    if (slot->busy) {
        heap_caps_free(state);
        JS_ThrowInternalError(ctx, "UART port is busy");
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

static void uart_future_release(esp32_mquickjs_future_driver_state_t *state)
{
    if (state == NULL) {
        return;
    }
    if (state->poll_timer != NULL) {
        (void)esp_timer_stop(state->poll_timer);
        (void)esp_timer_delete(state->poll_timer);
    }
    if (state->owner_retained) {
        JS_DeleteGCRef(state->ctx, &state->owner_ref);
    }
    heap_caps_free(state->data);
    heap_caps_free(state);
}

static bool uart_read_future_prepare(
    JSContext *ctx,
    JSGCRef *this_ref,
    int argc,
    JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **out_state)
{
    esp32_mquickjs_future_driver_state_t *state;

    if (out_state == NULL || argc < 1 || argc > 2) {
        JS_ThrowTypeError(ctx,
                          "UARTPort.read(length, timeoutMs?) expects a byte length");
        return false;
    }
    state = uart_future_allocate(ctx, this_ref->val, UART_FUTURE_READ);
    if (state == NULL) {
        return false;
    }
    if (!js_value_to_u32(ctx, argv[0].val, &state->length) ||
        (argc >= 2 && !JS_IsUndefined(argv[1].val) &&
         !js_value_to_u32(ctx, argv[1].val, &state->timeout_ms))) {
        uart_future_release(state);
        JS_ThrowTypeError(ctx,
                          "UARTPort.read(length, timeoutMs?) expects non-negative integers");
        return false;
    }
    if (state->length > 0) {
        state->data = heap_caps_malloc(state->length, MALLOC_CAP_8BIT);
        if (state->data == NULL) {
            uart_future_release(state);
            JS_ThrowOutOfMemory(ctx);
            return false;
        }
    }
    *out_state = state;
    return true;
}

static bool uart_flush_future_prepare(
    JSContext *ctx,
    JSGCRef *this_ref,
    int argc,
    JSGCRef *argv,
    esp32_mquickjs_future_driver_state_t **out_state)
{
    esp32_mquickjs_future_driver_state_t *state;

    if (out_state == NULL || argc > 1) {
        JS_ThrowTypeError(ctx, "UARTPort.flush(timeoutMs?) expected");
        return false;
    }
    state = uart_future_allocate(ctx, this_ref->val, UART_FUTURE_FLUSH);
    if (state == NULL) {
        return false;
    }
    if (argc == 1 && !JS_IsUndefined(argv[0].val) &&
        !js_value_to_u32(ctx, argv[0].val, &state->timeout_ms)) {
        uart_future_release(state);
        JS_ThrowTypeError(ctx,
                          "UARTPort.flush(timeoutMs?) expects timeoutMs to be non-negative");
        return false;
    }
    *out_state = state;
    return true;
}

static void uart_future_step(esp32_mquickjs_future_driver_state_t *state)
{
    esp32_mquickjs_uart_slot_t *slot;

    if (state == NULL || state->completed || state->cancelled) {
        return;
    }
    slot = uart_get_slot(&state->port_ref);
    if (slot == NULL) {
        state->err = ESP_ERR_INVALID_STATE;
        state->completed = true;
        return;
    }
    if (state->kind == UART_FUTURE_READ) {
        size_t available = 0;

        state->err = uart_get_buffered_data_len((uart_port_t)slot->port_id,
                                                &available);
        if (state->err != ESP_OK) {
            state->completed = true;
        } else if (state->length == 0 || available > 0) {
            size_t wanted = available < state->length ? available : state->length;

            state->read_length = wanted == 0 ? 0 : uart_read_bytes(
                (uart_port_t)slot->port_id,
                state->data,
                wanted,
                0);
            if (state->read_length < 0) {
                state->err = ESP_FAIL;
            }
            state->completed = true;
        }
    } else {
        state->err = uart_wait_tx_done((uart_port_t)slot->port_id, 0);
        if (state->err == ESP_OK) {
            state->completed = true;
        } else if (state->err == ESP_ERR_TIMEOUT) {
            state->err = ESP_OK;
        } else {
            state->completed = true;
        }
    }
    if (!state->completed && (state->timeout_ms == 0 ||
        (state->deadline_us > 0 &&
         (uint64_t)esp_timer_get_time() >= state->deadline_us))) {
        if (state->kind == UART_FUTURE_READ) {
            state->read_length = 0;
            state->err = ESP_OK;
        } else {
            state->err = ESP_ERR_TIMEOUT;
        }
        state->completed = true;
    }
}

static void uart_future_timer(void *opaque)
{
    esp32_mquickjs_future_driver_state_t *state = opaque;

    if (state != NULL && !state->completed) {
        (void)esp32_mquickjs_future_wake(state->runtime, state->token);
    }
}

static bool uart_future_start(JSContext *ctx,
                              esp32_mquickjs_runtime_t *runtime,
                              esp32_mquickjs_future_token_t token,
                              esp32_mquickjs_future_driver_state_t *state)
{
    esp32_mquickjs_uart_slot_t *slot = state != NULL
        ? uart_get_slot(&state->port_ref) : NULL;
    esp_timer_create_args_t timer_args = {
        .callback = uart_future_timer,
        .arg = state,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "mqjs_uart",
        .skip_unhandled_events = true,
    };

    if (state == NULL || slot == NULL) {
        JS_ThrowReferenceError(ctx, "UART port closed before operation start");
        return false;
    }
    if (slot->busy) {
        JS_ThrowInternalError(ctx, "UART port is busy");
        return false;
    }
    slot->busy = true;
    state->runtime = runtime;
    state->token = token;
    state->started = true;
    if (state->timeout_ms > 0) {
        state->deadline_us = (uint64_t)esp_timer_get_time() +
                             (uint64_t)state->timeout_ms * 1000ULL;
    }
    uart_future_step(state);
    if (!state->completed &&
        (esp_timer_create(&timer_args, &state->poll_timer) != ESP_OK ||
         esp_timer_start_periodic(state->poll_timer, 10000U) != ESP_OK)) {
        JS_ThrowInternalError(ctx, "failed to start UART readiness poller");
        return false;
    }
    (void)esp32_mquickjs_future_wake(runtime, token);
    return true;
}

static esp32_mquickjs_future_poll_t uart_future_poll(
    esp32_mquickjs_future_driver_state_t *state)
{
    uart_future_step(state);
    return state != NULL && state->completed
        ? ESP32_MQUICKJS_FUTURE_READY
        : ESP32_MQUICKJS_FUTURE_PENDING;
}

static JSValue uart_future_finish(JSContext *ctx,
                                  esp32_mquickjs_future_driver_state_t *state)
{
    if (state == NULL || state->cancelled) {
        return JS_ThrowInternalError(ctx, "UART operation cancelled");
    }
    if (state->err != ESP_OK) {
        return uart_throw_error(ctx, state->err,
                                state->kind == UART_FUTURE_READ
                                    ? "UARTPort.read() failed"
                                    : "UARTPort.flush() failed");
    }
    return state->kind == UART_FUTURE_READ
        ? uart_bytes_to_array(ctx, state->data, (size_t)state->read_length)
        : JS_TRUE;
}

static bool uart_future_cancel(esp32_mquickjs_future_driver_state_t *state)
{
    if (state == NULL || state->completed || state->cancelled) {
        return false;
    }
    state->cancelled = true;
    state->completed = true;
    if (state->runtime != NULL) {
        (void)esp32_mquickjs_future_wake(state->runtime, state->token);
    }
    return true;
}

static void uart_future_destroy(esp32_mquickjs_future_driver_state_t *state)
{
    esp32_mquickjs_uart_slot_t *slot = state != NULL
        ? uart_get_slot(&state->port_ref) : NULL;

    if (slot != NULL && state->started) {
        slot->busy = false;
    }
    uart_future_release(state);
}

static const esp32_mquickjs_future_driver_t s_uart_read_driver = {
    .prepare = uart_read_future_prepare,
    .start = uart_future_start,
    .poll = uart_future_poll,
    .finish = uart_future_finish,
    .cancel = uart_future_cancel,
    .destroy = uart_future_destroy,
};

static const esp32_mquickjs_future_driver_t s_uart_flush_driver = {
    .prepare = uart_flush_future_prepare,
    .start = uart_future_start,
    .poll = uart_future_poll,
    .finish = uart_future_finish,
    .cancel = uart_future_cancel,
    .destroy = uart_future_destroy,
};

static JSValue uart_future_call_and_wait(JSContext *ctx,
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

static bool uart_register_future_drivers(JSContext *ctx,
                                         esp32_mquickjs_runtime_t *runtime)
{
    JSGCRef object_ref;
    JSGCRef read_ref;
    JSGCRef flush_ref;
    JSValue *object = JS_PushGCRef(ctx, &object_ref);
    JSValue *read_fn = JS_PushGCRef(ctx, &read_ref);
    JSValue *flush_fn = JS_PushGCRef(ctx, &flush_ref);
    bool result;

    *object = JS_NewObjectClassUser(ctx, JS_CLASS_UART_PORT);
    *read_fn = JS_IsException(*object)
        ? JS_EXCEPTION : JS_GetPropertyStr(ctx, *object, "read");
    *flush_fn = JS_IsException(*object)
        ? JS_EXCEPTION : JS_GetPropertyStr(ctx, *object, "flush");
    result = !JS_IsException(*read_fn) && !JS_IsException(*flush_fn) &&
             esp32_mquickjs_future_register_driver(ctx, runtime,
                                                   *read_fn, &s_uart_read_driver) &&
             esp32_mquickjs_future_register_driver(ctx, runtime,
                                                   *flush_fn, &s_uart_flush_driver);
    if (!result && !JS_IsException(*object)) {
        JS_ThrowInternalError(ctx, "failed to register UART Future drivers");
    }
    JS_PopGCRef(ctx, &flush_ref);
    JS_PopGCRef(ctx, &read_ref);
    JS_PopGCRef(ctx, &object_ref);
    return result;
}

JSValue js_uart_port_read(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    return uart_future_call_and_wait(ctx, *this_val, "read", argc, argv);
}

JSValue js_uart_port_available(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    esp32_mquickjs_uart_port_ref_t port_ref;
    esp32_mquickjs_uart_slot_t *slot = NULL;
    size_t available = 0;
    esp_err_t err;

    (void)argc;
    (void)argv;

    if (uart_get_bound_slot(ctx, *this_val, "UARTPort.available()", &port_ref, &slot) != 0) {
        return JS_EXCEPTION;
    }
    err = uart_get_buffered_data_len((uart_port_t)slot->port_id, &available);
    if (err != ESP_OK) {
        return uart_throw_error(ctx, err, "UARTPort.available() failed");
    }
    return JS_NewInt64(ctx, (int64_t)available);
}

JSValue js_uart_port_flush(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    return uart_future_call_and_wait(ctx, *this_val, "flush", argc, argv);
}

JSValue js_uart_port_clear_rx(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    esp32_mquickjs_uart_port_ref_t port_ref;
    esp32_mquickjs_uart_slot_t *slot = NULL;
    esp_err_t err;

    (void)argc;
    (void)argv;

    if (uart_get_bound_slot(ctx, *this_val, "UARTPort.clearRx()", &port_ref, &slot) != 0) {
        return JS_EXCEPTION;
    }
    err = uart_flush_input((uart_port_t)slot->port_id);
    if (err != ESP_OK) {
        return uart_throw_error(ctx, err, "UARTPort.clearRx() failed");
    }
    return JS_TRUE;
}

JSValue js_uart_open(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    return uart_open(ctx, argc, argv);
}

JSValue js_uart_get_default_port(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_NewInt32(ctx, ESP32_MQUICKJS_UART_DEFAULT_PORT);
}

JSValue js_uart_get_default_tx(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_NewInt32(ctx, ESP32_MQUICKJS_UART_DEFAULT_TX_PIN);
}

JSValue js_uart_get_default_rx(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_NewInt32(ctx, ESP32_MQUICKJS_UART_DEFAULT_RX_PIN);
}

JSValue js_uart_get_default_baud(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_NewUint32(ctx, ESP32_MQUICKJS_UART_DEFAULT_BAUD);
}

JSValue js_uart_get_default_rx_buffer_size(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_NewUint32(ctx, ESP32_MQUICKJS_UART_DEFAULT_RX_BUFFER_SIZE);
}

JSValue js_uart_get_default_tx_buffer_size(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_NewUint32(ctx, ESP32_MQUICKJS_UART_DEFAULT_TX_BUFFER_SIZE);
}

JSValue js_uart_get_default_timeout_ms(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_NewUint32(ctx, ESP32_MQUICKJS_UART_DEFAULT_TIMEOUT_MS);
}

#endif
