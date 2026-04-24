#include <math.h>
#include <stdio.h>
#include <string.h>

#include "mquickjs_build.h"

#define JS_CLASS_HEADERS (JS_CLASS_USER + 0)
#define JS_CLASS_REQUEST (JS_CLASS_USER + 1)
#define JS_CLASS_RESPONSE (JS_CLASS_USER + 2)
#define JS_CLASS_DEFERRED (JS_CLASS_USER + 3)
#define JS_CLASS_STREAM (JS_CLASS_USER + 4)
#define JS_CLASS_HTTP_SERVER (JS_CLASS_USER + 5)
#define JS_CLASS_STATIC_FILE_HANDLER (JS_CLASS_USER + 6)
#define JS_CLASS_I2C_BUS (JS_CLASS_USER + 7)
#define JS_CLASS_SPI_BUS (JS_CLASS_USER + 8)
#define JS_CLASS_SPI_DEVICE (JS_CLASS_USER + 9)
#define JS_CLASS_COUNT (JS_CLASS_USER + 10)

#define js_global_object js_global_object_base
#define js_c_function_decl js_c_function_decl_base
#define main mqjs_stdlib_base_main
#include "mqjs_stdlib.c"
#undef main
#undef js_c_function_decl
#undef js_global_object

static const JSPropDef js_headers_proto[] = {
    JS_CFUNC_DEF("get", 1, js_headers_get),
    JS_CFUNC_DEF("set", 2, js_headers_set),
    JS_CFUNC_DEF("has", 1, js_headers_has),
    JS_CFUNC_DEF("delete", 1, js_headers_delete),
    JS_CFUNC_DEF("entries", 0, js_headers_entries),
    JS_CFUNC_DEF("toObject", 0, js_headers_toObject),
    JS_PROP_END,
};

static const JSClassDef js_headers_class =
    JS_CLASS_DEF("Headers", 1, js_headers_constructor, JS_CLASS_HEADERS, NULL, js_headers_proto, NULL, NULL);

static const JSPropDef js_request_proto[] = {
    JS_CFUNC_DEF("text", 0, js_request_text),
    JS_CFUNC_DEF("json", 0, js_request_json),
    JS_PROP_END,
};

static const JSClassDef js_request_class =
    JS_CLASS_DEF("Request", 2, js_request_constructor, JS_CLASS_REQUEST, NULL, js_request_proto, NULL, NULL);

static const JSPropDef js_response[] = {
    JS_CFUNC_DEF("text", 2, js_response_make_text),
    JS_CFUNC_DEF("json", 2, js_response_make_json),
    JS_CFUNC_DEF("stream", 2, js_response_make_stream),
    JS_PROP_END,
};

static const JSPropDef js_response_proto[] = {
    JS_CFUNC_DEF("text", 0, js_response_text),
    JS_CFUNC_DEF("json", 0, js_response_json),
    JS_PROP_END,
};

static const JSClassDef js_response_class =
    JS_CLASS_DEF("Response", 2, js_response_constructor, JS_CLASS_RESPONSE, js_response, js_response_proto, NULL, NULL);

static const JSPropDef js_deferred_proto[] = {
    JS_CFUNC_DEF("resolve", 1, js_deferred_resolve),
    JS_CFUNC_DEF("reject", 1, js_deferred_reject),
    JS_CFUNC_DEF("callback", 2, js_deferred_callback),
    JS_CFUNC_DEF("wait", 1, js_deferred_wait),
    JS_PROP_END,
};

static const JSClassDef js_deferred_class =
    JS_CLASS_DEF("_Deferred", 0, js_deferred_constructor, JS_CLASS_DEFERRED, NULL, js_deferred_proto, NULL, NULL);

static const JSPropDef js_stream[] = {
    JS_PROP_DOUBLE_DEF("SEEK_SET", 0, 0),
    JS_PROP_DOUBLE_DEF("SEEK_CUR", 1, 0),
    JS_PROP_DOUBLE_DEF("SEEK_END", 2, 0),
    JS_PROP_END,
};

static const JSPropDef js_stream_proto[] = {
    JS_CFUNC_DEF("read", 1, js_stream_read),
    JS_CFUNC_DEF("write", 1, js_stream_write),
    JS_CFUNC_DEF("flush", 0, js_stream_flush),
    JS_CFUNC_DEF("close", 0, js_stream_close),
    JS_CFUNC_DEF("seek", 2, js_stream_seek),
    JS_CFUNC_DEF("tell", 0, js_stream_tell),
    JS_CFUNC_DEF("eof", 0, js_stream_eof),
    JS_PROP_END,
};

static const JSClassDef js_stream_class =
    JS_CLASS_DEF("Stream", 0, js_stream_constructor, JS_CLASS_STREAM, js_stream, js_stream_proto, NULL, NULL);

#if CONFIG_ESP32_MQUICKJS_FEATURE_FS
static const JSPropDef js_fs[] = {
    JS_PROP_STRING_DEF("ROOT", "/littlefs", 0),
    JS_CFUNC_DEF("open", 2, js_fs_open),
    JS_CFUNC_DEF("list", 1, js_fs_list),
    JS_CFUNC_DEF("stat", 1, js_fs_stat),
    JS_CFUNC_DEF("exists", 1, js_fs_exists),
    JS_CFUNC_DEF("readText", 1, js_fs_readText),
    JS_CFUNC_DEF("writeText", 2, js_fs_writeText),
    JS_CFUNC_DEF("appendText", 2, js_fs_appendText),
    JS_CFUNC_DEF("remove", 1, js_fs_remove),
    JS_CFUNC_DEF("rename", 2, js_fs_rename),
    JS_CFUNC_DEF("mkdir", 1, js_fs_mkdir),
    JS_PROP_END,
};

static const JSClassDef js_fs_obj =
    JS_OBJECT_DEF("fs", js_fs);
#endif

#if CONFIG_ESP32_MQUICKJS_FEATURE_GPIO
static const JSPropDef js_gpio[] = {
    JS_PROP_STRING_DEF("DISABLED", "disabled", 0),
    JS_PROP_STRING_DEF("INPUT", "input", 0),
    JS_PROP_STRING_DEF("OUTPUT", "output", 0),
    JS_PROP_STRING_DEF("INPUT_OUTPUT", "inputOutput", 0),
    JS_PROP_STRING_DEF("OUTPUT_OPEN_DRAIN", "outputOpenDrain", 0),
    JS_PROP_STRING_DEF("INPUT_OUTPUT_OPEN_DRAIN", "inputOutputOpenDrain", 0),
    JS_PROP_STRING_DEF("FLOATING", "floating", 0),
    JS_PROP_STRING_DEF("PULLUP", "pullup", 0),
    JS_PROP_STRING_DEF("PULLDOWN", "pulldown", 0),
    JS_PROP_STRING_DEF("PULLUP_PULLDOWN", "pullupPulldown", 0),
    JS_PROP_STRING_DEF("CHANGE", "change", 0),
    JS_PROP_STRING_DEF("RISING", "rising", 0),
    JS_PROP_STRING_DEF("FALLING", "falling", 0),
    JS_PROP_DOUBLE_DEF("LOW", 0, 0),
    JS_PROP_DOUBLE_DEF("HIGH", 1, 0),
    JS_PROP_DOUBLE_DEF("DRIVE_0", 0, 0),
    JS_PROP_DOUBLE_DEF("DRIVE_1", 1, 0),
    JS_PROP_DOUBLE_DEF("DRIVE_2", 2, 0),
    JS_PROP_DOUBLE_DEF("DRIVE_3", 3, 0),
    JS_CGETSET_DEF("LED_BUILTIN", js_gpio_get_led_builtin, NULL),
    JS_CGETSET_DEF("USER_LED_PIN", js_gpio_get_user_led_pin, NULL),
    JS_CGETSET_DEF("USER_LED_ACTIVE_LOW", js_gpio_get_user_led_active_low, NULL),
    JS_CFUNC_DEF("isValid", 1, js_gpio_isValid),
    JS_CFUNC_DEF("isOutputCapable", 1, js_gpio_isOutputCapable),
    JS_CFUNC_DEF("pinMode", 2, js_gpio_pinMode),
    JS_CFUNC_DEF("setPull", 2, js_gpio_setPull),
    JS_CFUNC_DEF("status", 1, js_gpio_status),
    JS_CFUNC_DEF("configure", 2, js_gpio_configure),
    JS_CFUNC_DEF("digitalWrite", 2, js_gpio_digitalWrite),
    JS_CFUNC_DEF("digitalRead", 1, js_gpio_digitalRead),
    JS_CFUNC_DEF("toggle", 1, js_gpio_toggle),
    JS_CFUNC_DEF("getDriveStrength", 1, js_gpio_getDriveStrength),
    JS_CFUNC_DEF("setDriveStrength", 2, js_gpio_setDriveStrength),
    JS_CFUNC_DEF("hold", 2, js_gpio_hold),
    JS_CFUNC_DEF("attachInterrupt", 3, js_gpio_attachInterrupt),
    JS_CFUNC_DEF("detachInterrupt", 1, js_gpio_detachInterrupt),
    JS_CFUNC_DEF("reset", 1, js_gpio_reset),
    JS_CFUNC_DEF("led", 1, js_gpio_led),
    JS_PROP_END,
};

static const JSClassDef js_gpio_obj =
    JS_OBJECT_DEF("gpio", js_gpio);
#endif

#if CONFIG_ESP32_MQUICKJS_FEATURE_LEDC
static const JSPropDef js_ledc[] = {
    JS_PROP_STRING_DEF("AUTO_CLOCK", "auto", 0),
    JS_PROP_STRING_DEF("APB_CLOCK", "apb", 0),
    JS_PROP_STRING_DEF("XTAL_CLOCK", "xtal", 0),
    JS_PROP_STRING_DEF("RC_FAST_CLOCK", "rcFast", 0),
    JS_PROP_STRING_DEF("SLEEP_NO_ALIVE_NO_PD", "noAliveNoPd", 0),
    JS_PROP_STRING_DEF("SLEEP_NO_ALIVE_ALLOW_PD", "noAliveAllowPd", 0),
    JS_PROP_STRING_DEF("SLEEP_KEEP_ALIVE", "keepAlive", 0),
    JS_CGETSET_DEF("CHANNEL_COUNT", js_ledc_get_channel_count, NULL),
    JS_CGETSET_DEF("TIMER_COUNT", js_ledc_get_timer_count, NULL),
    JS_CGETSET_DEF("MAX_DUTY_RESOLUTION_BITS", js_ledc_get_max_duty_resolution_bits, NULL),
    JS_CFUNC_DEF("timerConfig", 2, js_ledc_timerConfig),
    JS_CFUNC_DEF("channelConfig", 2, js_ledc_channelConfig),
    JS_CFUNC_DEF("setDuty", 2, js_ledc_setDuty),
    JS_CFUNC_DEF("setDutyWithHpoint", 3, js_ledc_setDutyWithHpoint),
    JS_CFUNC_DEF("setDutyAndUpdate", 2, js_ledc_setDutyAndUpdate),
    JS_CFUNC_DEF("getDuty", 1, js_ledc_getDuty),
    JS_CFUNC_DEF("getHpoint", 1, js_ledc_getHpoint),
    JS_CFUNC_DEF("updateDuty", 1, js_ledc_updateDuty),
    JS_CFUNC_DEF("setFreq", 2, js_ledc_setFreq),
    JS_CFUNC_DEF("getFreq", 1, js_ledc_getFreq),
    JS_CFUNC_DEF("bindChannelTimer", 2, js_ledc_bindChannelTimer),
    JS_CFUNC_DEF("stop", 2, js_ledc_stop),
    JS_CFUNC_DEF("timerPause", 1, js_ledc_timerPause),
    JS_CFUNC_DEF("timerResume", 1, js_ledc_timerResume),
    JS_CFUNC_DEF("timerStatus", 1, js_ledc_timerStatus),
    JS_CFUNC_DEF("channelStatus", 1, js_ledc_channelStatus),
    JS_PROP_END,
};

static const JSClassDef js_ledc_obj =
    JS_OBJECT_DEF("ledc", js_ledc);
#endif

#if CONFIG_ESP32_MQUICKJS_FEATURE_ADC
static const JSPropDef js_adc[] = {
    JS_PROP_DOUBLE_DEF("UNIT_1", 1, 0),
    JS_PROP_DOUBLE_DEF("UNIT_2", 2, 0),
    JS_PROP_DOUBLE_DEF("ATTEN_DB_0", 0, 0),
    JS_PROP_DOUBLE_DEF("ATTEN_DB_2_5", 1, 0),
    JS_PROP_DOUBLE_DEF("ATTEN_DB_6", 2, 0),
    JS_PROP_DOUBLE_DEF("ATTEN_DB_12", 3, 0),
    JS_PROP_DOUBLE_DEF("BITWIDTH_DEFAULT", 0, 0),
    JS_PROP_DOUBLE_DEF("BITWIDTH_9", 9, 0),
    JS_PROP_DOUBLE_DEF("BITWIDTH_10", 10, 0),
    JS_PROP_DOUBLE_DEF("BITWIDTH_11", 11, 0),
    JS_PROP_DOUBLE_DEF("BITWIDTH_12", 12, 0),
    JS_PROP_DOUBLE_DEF("BITWIDTH_13", 13, 0),
    JS_CGETSET_DEF("UNIT_COUNT", js_adc_get_unit_count, NULL),
    JS_CGETSET_DEF("MAX_CHANNEL_COUNT", js_adc_get_max_channel_count, NULL),
    JS_CFUNC_DEF("open", 1, js_adc_open),
    JS_CFUNC_DEF("close", 1, js_adc_close),
    JS_CFUNC_DEF("status", 1, js_adc_status),
    JS_CFUNC_DEF("configure", 3, js_adc_configure),
    JS_CFUNC_DEF("read", 2, js_adc_read),
    JS_CFUNC_DEF("readMilliVolts", 2, js_adc_readMilliVolts),
    JS_CFUNC_DEF("ioToChannel", 1, js_adc_ioToChannel),
    JS_CFUNC_DEF("channelToIo", 2, js_adc_channelToIo),
    JS_PROP_END,
};

static const JSClassDef js_adc_obj =
    JS_OBJECT_DEF("adc", js_adc);
#endif

#if CONFIG_ESP32_MQUICKJS_FEATURE_DAC
static const JSPropDef js_dac[] = {
    JS_PROP_DOUBLE_DEF("CHANNEL_0", 0, 0),
    JS_PROP_DOUBLE_DEF("CHANNEL_1", 1, 0),
    JS_CGETSET_DEF("CHANNEL_COUNT", js_dac_get_channel_count, NULL),
    JS_CGETSET_DEF("RESOLUTION_BITS", js_dac_get_resolution_bits, NULL),
    JS_CGETSET_DEF("MAX_VALUE", js_dac_get_max_value, NULL),
    JS_CFUNC_DEF("open", 1, js_dac_open),
    JS_CFUNC_DEF("close", 1, js_dac_close),
    JS_CFUNC_DEF("status", 1, js_dac_status),
    JS_CFUNC_DEF("write", 2, js_dac_write),
    JS_CFUNC_DEF("ioToChannel", 1, js_dac_ioToChannel),
    JS_CFUNC_DEF("channelToIo", 1, js_dac_channelToIo),
    JS_PROP_END,
};

static const JSClassDef js_dac_obj =
    JS_OBJECT_DEF("dac", js_dac);
#endif

static const JSPropDef js_esp32[] = {
    JS_CFUNC_DEF("info", 0, js_esp32_info),
    JS_CFUNC_DEF("millis", 0, js_esp32_millis),
    JS_CFUNC_DEF("micros", 0, js_esp32_micros),
    JS_CFUNC_DEF("freeHeap", 0, js_esp32_freeHeap),
    JS_PROP_END,
};

static const JSClassDef js_esp32_obj =
    JS_OBJECT_DEF("esp32", js_esp32);

#if CONFIG_ESP32_MQUICKJS_FEATURE_I2C
static const JSPropDef js_i2c_bus_proto[] = {
    JS_CFUNC_DEF("close", 0, js_i2c_bus_close),
    JS_CFUNC_DEF("status", 0, js_i2c_bus_status),
    JS_CFUNC_DEF("scan", 0, js_i2c_bus_scan),
    JS_CFUNC_DEF("write", 2, js_i2c_bus_write),
    JS_CFUNC_DEF("read", 2, js_i2c_bus_read),
    JS_CFUNC_DEF("writeRead", 3, js_i2c_bus_writeRead),
    JS_PROP_END,
};

static const JSClassDef js_i2c_bus_class =
    JS_CLASS_DEF("I2CBus", 0, js_i2c_bus_constructor, JS_CLASS_I2C_BUS, NULL, js_i2c_bus_proto, NULL, js_i2c_bus_finalizer);

static const JSPropDef js_i2c[] = {
    JS_CGETSET_DEF("DEFAULT_SDA", js_i2c_get_default_sda, NULL),
    JS_CGETSET_DEF("DEFAULT_SCL", js_i2c_get_default_scl, NULL),
    JS_CGETSET_DEF("DEFAULT_FREQ_HZ", js_i2c_get_default_freq_hz, NULL),
    JS_CGETSET_DEF("DEFAULT_TIMEOUT_MS", js_i2c_get_default_timeout_ms, NULL),
    JS_CFUNC_DEF("open", 1, js_i2c_open),
    JS_PROP_END,
};

static const JSClassDef js_i2c_obj =
    JS_OBJECT_DEF("i2c", js_i2c);
#endif

#if CONFIG_ESP32_MQUICKJS_FEATURE_SPI
static const JSPropDef js_spi_bus_proto[] = {
    JS_CFUNC_DEF("close", 0, js_spi_bus_close),
    JS_CFUNC_DEF("status", 0, js_spi_bus_status),
    JS_CFUNC_DEF("openDevice", 1, js_spi_bus_open_device),
    JS_PROP_END,
};

static const JSClassDef js_spi_bus_class =
    JS_CLASS_DEF("SPIBus", 0, js_spi_bus_constructor, JS_CLASS_SPI_BUS, NULL, js_spi_bus_proto, NULL, js_spi_bus_finalizer);

static const JSPropDef js_spi_device_proto[] = {
    JS_CFUNC_DEF("close", 0, js_spi_device_close),
    JS_CFUNC_DEF("status", 0, js_spi_device_status),
    JS_CFUNC_DEF("transfer", 1, js_spi_device_transfer),
    JS_CFUNC_DEF("write", 1, js_spi_device_write),
    JS_CFUNC_DEF("read", 2, js_spi_device_read),
    JS_PROP_END,
};

static const JSClassDef js_spi_device_class =
    JS_CLASS_DEF("SPIDevice", 0, js_spi_device_constructor, JS_CLASS_SPI_DEVICE, NULL, js_spi_device_proto, NULL, js_spi_device_finalizer);

static const JSPropDef js_spi[] = {
    JS_CGETSET_DEF("HOST_2", js_spi_get_host_2, NULL),
#if CONFIG_SOC_SPI_PERIPH_NUM > 2
    JS_CGETSET_DEF("HOST_3", js_spi_get_host_3, NULL),
#endif
    JS_CGETSET_DEF("DEFAULT_HOST", js_spi_get_default_host, NULL),
    JS_CGETSET_DEF("DEFAULT_SCLK", js_spi_get_default_sclk, NULL),
    JS_CGETSET_DEF("DEFAULT_MOSI", js_spi_get_default_mosi, NULL),
    JS_CGETSET_DEF("DEFAULT_MISO", js_spi_get_default_miso, NULL),
    JS_CGETSET_DEF("DEFAULT_CS", js_spi_get_default_cs, NULL),
    JS_CGETSET_DEF("DEFAULT_FREQ_HZ", js_spi_get_default_freq_hz, NULL),
    JS_CGETSET_DEF("DEFAULT_QUEUE_SIZE", js_spi_get_default_queue_size, NULL),
    JS_CGETSET_DEF("DEFAULT_MAX_TRANSFER_SIZE", js_spi_get_default_max_transfer_size, NULL),
    JS_CFUNC_DEF("openBus", 1, js_spi_open_bus),
    JS_PROP_END,
};

static const JSClassDef js_spi_obj =
    JS_OBJECT_DEF("spi", js_spi);
#endif

#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
static const JSPropDef js_wifi_async[] = {
    JS_CFUNC_DEF("connect", 4, js_wifi_async_connect),
    JS_CFUNC_DEF("scan", 1, js_wifi_async_scan),
    JS_PROP_END,
};

static const JSClassDef js_wifi_async_obj =
    JS_OBJECT_DEF("async", js_wifi_async);

static const JSPropDef js_wifi[] = {
    JS_CGETSET_DEF("DEFAULT_TIMEOUT_MS", js_wifi_get_default_timeout_ms, NULL),
    JS_CFUNC_DEF("connect", 3, js_wifi_connect),
    JS_CFUNC_DEF("disconnect", 0, js_wifi_disconnect),
    JS_CFUNC_DEF("status", 0, js_wifi_status),
    JS_CFUNC_DEF("scan", 0, js_wifi_scan),
    JS_PROP_CLASS_DEF("async", &js_wifi_async_obj),
    JS_PROP_END,
};

static const JSClassDef js_wifi_obj =
    JS_OBJECT_DEF("wifi", js_wifi);
#endif

#if CONFIG_ESP32_MQUICKJS_FEATURE_HTTP || CONFIG_ESP32_MQUICKJS_FEATURE_HTTP_SERVER
#if CONFIG_ESP32_MQUICKJS_FEATURE_HTTP
static const JSPropDef js_http_async[] = {
    JS_CFUNC_DEF("fetch", 3, js_http_async_fetch),
    JS_PROP_END,
};

static const JSClassDef js_http_async_obj =
    JS_OBJECT_DEF("async", js_http_async);
#endif

static const JSPropDef js_http[] = {
#if CONFIG_ESP32_MQUICKJS_FEATURE_HTTP
    JS_CGETSET_DEF("DEFAULT_TIMEOUT_MS", js_http_get_default_timeout_ms, NULL),
    JS_CFUNC_DEF("fetch", 2, js_http_fetch),
    JS_PROP_CLASS_DEF("async", &js_http_async_obj),
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_HTTP_SERVER
    JS_CFUNC_DEF("server", 1, js_http_server_create),
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_HTTP_SERVER && CONFIG_ESP32_MQUICKJS_FEATURE_FS
    JS_CFUNC_DEF("staticFileHandler", 1, js_http_static_file_handler),
#endif
    JS_PROP_END,
};

static const JSClassDef js_http_obj =
    JS_OBJECT_DEF("http", js_http);
#endif

#if CONFIG_ESP32_MQUICKJS_FEATURE_HTTP_SERVER
static const JSPropDef js_http_server_proto[] = {
    JS_CFUNC_DEF("start", 0, js_http_server_start),
    JS_CFUNC_DEF("stop", 0, js_http_server_stop),
    JS_CFUNC_DEF("get", 2, js_http_server_get),
    JS_CFUNC_DEF("post", 2, js_http_server_post),
    JS_CFUNC_DEF("put", 2, js_http_server_put),
    JS_CFUNC_DEF("patch", 2, js_http_server_patch),
    JS_CFUNC_DEF("delete", 2, js_http_server_delete),
    JS_CFUNC_DEF("options", 2, js_http_server_options),
    JS_CFUNC_DEF("head", 2, js_http_server_head),
    JS_CFUNC_DEF("all", 2, js_http_server_all),
    JS_PROP_END,
};

static const JSClassDef js_http_server_class =
    JS_CLASS_DEF("HttpServer", 0, js_http_server_constructor, JS_CLASS_HTTP_SERVER, NULL, js_http_server_proto, NULL, NULL);
#endif

#if CONFIG_ESP32_MQUICKJS_FEATURE_HTTP_SERVER && CONFIG_ESP32_MQUICKJS_FEATURE_FS
static const JSPropDef js_static_file_handler_proto[] = {
    JS_CFUNC_DEF("handle", 1, js_http_static_file_handler_handle),
    JS_PROP_END,
};

static const JSClassDef js_static_file_handler_class =
    JS_CLASS_DEF("StaticFileHandler", 0, js_http_static_file_handler_constructor, JS_CLASS_STATIC_FILE_HANDLER, NULL, js_static_file_handler_proto, NULL, NULL);
#endif

static const JSPropDef js_global_object_extra[] = {
    JS_PROP_CLASS_DEF("Headers", &js_headers_class),
    JS_PROP_CLASS_DEF("Request", &js_request_class),
    JS_PROP_CLASS_DEF("Response", &js_response_class),
    JS_PROP_CLASS_DEF("_Deferred", &js_deferred_class),
    JS_PROP_CLASS_DEF("Stream", &js_stream_class),
#if CONFIG_ESP32_MQUICKJS_FEATURE_FS
    JS_PROP_CLASS_DEF("fs", &js_fs_obj),
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_GPIO
    JS_PROP_CLASS_DEF("gpio", &js_gpio_obj),
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_LEDC
    JS_PROP_CLASS_DEF("ledc", &js_ledc_obj),
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_ADC
    JS_PROP_CLASS_DEF("adc", &js_adc_obj),
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_DAC
    JS_PROP_CLASS_DEF("dac", &js_dac_obj),
#endif
    JS_PROP_CLASS_DEF("esp32", &js_esp32_obj),
#if CONFIG_ESP32_MQUICKJS_FEATURE_I2C
    JS_PROP_CLASS_DEF("i2c", &js_i2c_obj),
    JS_PROP_CLASS_DEF("I2CBus", &js_i2c_bus_class),
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_SPI
    JS_PROP_CLASS_DEF("spi", &js_spi_obj),
    JS_PROP_CLASS_DEF("SPIBus", &js_spi_bus_class),
    JS_PROP_CLASS_DEF("SPIDevice", &js_spi_device_class),
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
    JS_PROP_CLASS_DEF("wifi", &js_wifi_obj),
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_HTTP || CONFIG_ESP32_MQUICKJS_FEATURE_HTTP_SERVER
    JS_PROP_CLASS_DEF("http", &js_http_obj),
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_HTTP_SERVER
    JS_PROP_CLASS_DEF("HttpServer", &js_http_server_class),
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_HTTP_SERVER && CONFIG_ESP32_MQUICKJS_FEATURE_FS
    JS_PROP_CLASS_DEF("StaticFileHandler", &js_static_file_handler_class),
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_FS
    JS_PROP_STRING_DEF("SCRIPTS_DIR", "/littlefs", 0),
#endif
    JS_CFUNC_DEF("help", 0, js_help),
    JS_CFUNC_DEF("defer", 0, js_defer),
    JS_CFUNC_DEF("waitFor", 2, js_waitFor),
    JS_CFUNC_DEF("sleep", 1, js_sleep),
    JS_CFUNC_DEF("delay", 1, js_sleep),
    JS_CFUNC_DEF("setInterval", 2, js_setInterval),
    JS_CFUNC_DEF("clearInterval", 1, js_clearTimeout),
#if CONFIG_ESP32_MQUICKJS_FEATURE_HTTP
    JS_CFUNC_DEF("fetch", 2, js_http_fetch),
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_HTTP_SERVER && CONFIG_ESP32_MQUICKJS_FEATURE_FS
    JS_CFUNC_DEF("staticFileHandler", 1, js_http_static_file_handler),
#endif
    JS_PROP_END,
};

static size_t count_prop_defs(const JSPropDef *defs)
{
    size_t count = 0;

    while (defs[count].def_type != JS_DEF_END) {
        count++;
    }
    return count;
}

static JSPropDef *merge_prop_defs(const JSPropDef *base_defs, const JSPropDef *extra_defs)
{
    size_t base_count = count_prop_defs(base_defs);
    size_t extra_count = count_prop_defs(extra_defs);
    JSPropDef *merged = malloc(sizeof(*merged) * (base_count + extra_count + 1));

    if (merged == NULL) {
        return NULL;
    }
    memcpy(merged, base_defs, sizeof(*merged) * base_count);
    memcpy(merged + base_count, extra_defs, sizeof(*merged) * extra_count);
    merged[base_count + extra_count] = (JSPropDef)JS_PROP_END;
    return merged;
}

int main(int argc, char **argv)
{
    JSPropDef *merged_global_object = merge_prop_defs(js_global_object_base, js_global_object_extra);
    int ret;

    if (merged_global_object == NULL) {
        fprintf(stderr, "out of memory while merging stdlib globals\n");
        return 1;
    }
    ret = build_atoms("js_stdlib", merged_global_object, js_c_function_decl_base, argc, argv);
    free(merged_global_object);
    return ret;
}
