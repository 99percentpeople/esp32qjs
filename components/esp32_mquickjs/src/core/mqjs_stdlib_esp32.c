#include <math.h>
#include <stdio.h>
#include <string.h>

#include "mquickjs_build.h"

#define JS_CLASS_HEADERS (JS_CLASS_USER + 0)
#define JS_CLASS_REQUEST (JS_CLASS_USER + 1)
#define JS_CLASS_RESPONSE (JS_CLASS_USER + 2)
#define JS_CLASS_STREAM (JS_CLASS_USER + 4)
#define JS_CLASS_HTTP_SERVER (JS_CLASS_USER + 5)
#define JS_CLASS_I2C_BUS (JS_CLASS_USER + 7)
#define JS_CLASS_I2C_DEVICE (JS_CLASS_USER + 25)
#define JS_CLASS_SPI_BUS (JS_CLASS_USER + 8)
#define JS_CLASS_SPI_DEVICE (JS_CLASS_USER + 9)
#define JS_CLASS_UART_PORT (JS_CLASS_USER + 10)
#define JS_CLASS_BYTE_VIEW (JS_CLASS_USER + 11)
#define JS_CLASS_BYTE_SPAN_SOURCE (JS_CLASS_USER + 12)
#define JS_CLASS_BITMAP_SPAN_SOURCE (JS_CLASS_USER + 13)
#define JS_CLASS_BITMAP (JS_CLASS_USER + 14)
#define JS_CLASS_DISPLAY_FONT (JS_CLASS_USER + 15)
#define JS_CLASS_DISPLAY_COMMAND_BUFFER (JS_CLASS_USER + 16)
#define JS_CLASS_FUTURE (JS_CLASS_USER + 17)
#define JS_CLASS_EVENT_QUEUE (JS_CLASS_USER + 18)
#define JS_CLASS_I2S_CHANNEL (JS_CLASS_USER + 19)
#define JS_CLASS_CAMERA (JS_CLASS_USER + 20)
#define JS_CLASS_CAMERA_FRAME (JS_CLASS_USER + 21)
#define JS_CLASS_RMT_SYMBOL_BUFFER (JS_CLASS_USER + 22)
#define JS_CLASS_RMT_CHANNEL (JS_CLASS_USER + 23)
#define JS_CLASS_FS_VOLUME (JS_CLASS_USER + 24)
#define JS_CLASS_TCP_SOCKET (JS_CLASS_USER + 26)
#define JS_CLASS_TCP_LISTENER (JS_CLASS_USER + 27)
#define JS_CLASS_UDP_SOCKET (JS_CLASS_USER + 28)
#define JS_CLASS_RPC_CODEC (JS_CLASS_USER + 29)
#define JS_CLASS_RPC_DECODER (JS_CLASS_USER + 30)
#define JS_CLASS_BLE_ADAPTER (JS_CLASS_USER + 31)
#define JS_CLASS_BLE_SCANNER (JS_CLASS_USER + 32)
#define JS_CLASS_BLE_ADVERTISER (JS_CLASS_USER + 33)
#define JS_CLASS_BLE_CONNECTION (JS_CLASS_USER + 34)
#define JS_CLASS_BLE_SERVICE (JS_CLASS_USER + 35)
#define JS_CLASS_BLE_CHARACTERISTIC (JS_CLASS_USER + 36)
#define JS_CLASS_BLE_DESCRIPTOR (JS_CLASS_USER + 37)
#define JS_CLASS_BLE_NOTIFICATION_STREAM (JS_CLASS_USER + 38)
#define JS_CLASS_BLE_GATT_SERVER (JS_CLASS_USER + 39)
#define JS_CLASS_BLE_LOCAL_CHARACTERISTIC (JS_CLASS_USER + 40)
#define JS_CLASS_ESPNOW_SESSION (JS_CLASS_USER + 41)
#define JS_CLASS_ESPNOW_PEER (JS_CLASS_USER + 42)
#define JS_CLASS_COUNT (JS_CLASS_USER + 43)

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
    JS_CFUNC_DEF("bytes", 1, js_request_bytes),
    JS_PROP_END,
};

static const JSClassDef js_request_class =
    JS_CLASS_DEF("Request", 2, js_request_constructor, JS_CLASS_REQUEST, NULL, js_request_proto, NULL, NULL);

static const JSPropDef js_response[] = {
    JS_CFUNC_DEF("text", 2, js_response_make_text),
    JS_CFUNC_DEF("json", 2, js_response_make_json),
    JS_CFUNC_DEF("stream", 2, js_response_make_stream),
    JS_CFUNC_DEF("bytes", 2, js_response_make_bytes),
    JS_PROP_END,
};

static const JSPropDef js_response_proto[] = {
    JS_CFUNC_DEF("text", 0, js_response_text),
    JS_CFUNC_DEF("json", 0, js_response_json),
    JS_CFUNC_DEF("bytes", 1, js_response_bytes),
    JS_PROP_END,
};

static const JSClassDef js_response_class =
    JS_CLASS_DEF("Response", 2, js_response_constructor, JS_CLASS_RESPONSE, js_response, js_response_proto, NULL, NULL);

static const JSPropDef js_future[] = {
    JS_CFUNC_DEF("call", 3, js_future_call),
    JS_CFUNC_DEF("all", 1, js_future_all),
    JS_CFUNC_DEF("race", 1, js_future_race),
    JS_CFUNC_DEF("sleep", 1, js_future_sleep),
    JS_CFUNC_DEF("timeout", 2, js_future_timeout),
    JS_PROP_END,
};

static const JSPropDef js_future_proto[] = {
    JS_CFUNC_DEF("status", 0, js_future_status),
    JS_CFUNC_DEF("wait", 1, js_future_wait),
    JS_CFUNC_DEF("cancel", 0, js_future_cancel),
    JS_CFUNC_DEF("map", 1, js_future_map),
    JS_CFUNC_DEF("flatMap", 1, js_future_flat_map),
    JS_PROP_END,
};

static const JSClassDef js_future_class =
    JS_CLASS_TRACE_DEF("Future", 0, js_future_constructor, JS_CLASS_FUTURE, js_future, js_future_proto, NULL, js_future_finalizer, js_future_gc_trace);

static const JSPropDef js_event_queue_proto[] = {
    JS_CFUNC_DEF("receive", 1, js_event_queue_receive),
    JS_CFUNC_DEF("stats", 0, js_event_queue_stats),
    JS_CFUNC_DEF("close", 0, js_event_queue_close),
    JS_PROP_END,
};

static const JSClassDef js_event_queue_class =
    JS_CLASS_DEF("EventQueue", 0, js_event_queue_constructor, JS_CLASS_EVENT_QUEUE, NULL, js_event_queue_proto, NULL, js_event_queue_finalizer);

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
    JS_CLASS_DEF("Stream", 0, js_stream_constructor, JS_CLASS_STREAM, js_stream,
                 js_stream_proto, NULL, js_stream_finalizer);

static const JSPropDef js_byte_view_proto[] = {
    JS_CGETSET_DEF("length", js_byte_view_get_length, NULL),
    JS_CGETSET_DEF("byteLength", js_byte_view_get_length, NULL),
    JS_CFUNC_DEF("toArray", 0, js_byte_view_to_array),
    JS_CFUNC_DEF("close", 0, js_byte_view_close),
    JS_PROP_END,
};

static const JSClassDef js_byte_view_class =
    JS_CLASS_DEF("ByteView", 0, js_byte_view_constructor, JS_CLASS_BYTE_VIEW, NULL, js_byte_view_proto, NULL, js_byte_view_finalizer);

static const JSPropDef js_byte_span_source_proto[] = {
    JS_CGETSET_DEF("byteLength", js_byte_span_source_get_length, NULL),
    JS_CFUNC_DEF("close", 0, js_byte_span_source_close),
    JS_PROP_END,
};

static const JSClassDef js_byte_span_source_class =
    JS_CLASS_DEF("ByteSpanSource", 0, js_byte_span_source_constructor, JS_CLASS_BYTE_SPAN_SOURCE, NULL, js_byte_span_source_proto, NULL, js_byte_span_source_finalizer);

#if CONFIG_ESP32_MQUICKJS_FEATURE_BITMAP
static const JSPropDef js_bitmap_span_source_proto[] = {
    JS_CFUNC_DEF("setRect", 4, js_bitmap_span_source_set_rect),
    JS_PROP_END,
};

static const JSClassDef js_bitmap_span_source_class =
    JS_CLASS_DEF("BitmapSpanSource",
                 0,
                 js_bitmap_span_source_constructor,
                 JS_CLASS_BITMAP_SPAN_SOURCE,
                 NULL,
                 js_bitmap_span_source_proto,
                 &js_byte_span_source_class,
                 js_byte_span_source_finalizer);

static const JSPropDef js_display_font_proto[] = {
    JS_CGETSET_DEF("name", js_display_font_get_name, NULL),
    JS_CGETSET_DEF("width", js_display_font_get_width, NULL),
    JS_CGETSET_DEF("height", js_display_font_get_height, NULL),
    JS_CGETSET_DEF("advance", js_display_font_get_advance, NULL),
    JS_CGETSET_DEF("lineHeight", js_display_font_get_line_height, NULL),
    JS_PROP_END,
};

static const JSClassDef js_display_font_class =
    JS_CLASS_DEF("DisplayFont", 0, js_display_font_constructor, JS_CLASS_DISPLAY_FONT, NULL, js_display_font_proto, NULL, js_display_font_finalizer);

static const JSPropDef js_display_command_buffer_proto[] = {
    JS_CFUNC_DEF("reset", 0, js_display_command_buffer_reset),
    JS_CFUNC_DEF("close", 0, js_display_command_buffer_close),
    JS_CFUNC_DEF("clear", 1, js_display_command_buffer_clear),
    JS_CFUNC_DEF("fill", 1, js_display_command_buffer_clear),
    JS_CFUNC_DEF("fillRect", 5, js_display_command_buffer_fill_rect),
    JS_CFUNC_DEF("drawRect", 5, js_display_command_buffer_draw_rect),
    JS_CFUNC_DEF("drawLine", 5, js_display_command_buffer_draw_line),
    JS_CFUNC_DEF("drawRoundRect", 6, js_display_command_buffer_draw_round_rect),
    JS_CFUNC_DEF("fillRoundRect", 6, js_display_command_buffer_fill_round_rect),
    JS_CFUNC_DEF("drawText", 4, js_display_command_buffer_draw_text),
    JS_CFUNC_DEF("appendPacked", 2, js_display_command_buffer_append_packed),
    JS_CFUNC_DEF("replay", 1, js_display_command_buffer_replay),
    JS_CFUNC_DEF("stats", 0, js_display_command_buffer_stats),
    JS_PROP_END,
};

static const JSClassDef js_display_command_buffer_class =
    JS_CLASS_DEF("DisplayCommandBuffer",
                 0,
                 js_display_command_buffer_constructor,
                 JS_CLASS_DISPLAY_COMMAND_BUFFER,
                 NULL,
                 js_display_command_buffer_proto,
                 NULL,
                 js_display_command_buffer_finalizer);

static const JSPropDef js_bitmap_proto[] = {
    JS_CGETSET_DEF("width", js_bitmap_get_width, NULL),
    JS_CGETSET_DEF("height", js_bitmap_get_height, NULL),
    JS_CGETSET_DEF("format", js_bitmap_get_format, NULL),
    JS_CGETSET_DEF("layout", js_bitmap_get_layout, NULL),
    JS_CGETSET_DEF("stride", js_bitmap_get_stride, NULL),
    JS_CGETSET_DEF("pageHeight", js_bitmap_get_page_height, NULL),
    JS_CGETSET_DEF("byteLength", js_bitmap_get_byte_length, NULL),
    JS_CFUNC_DEF("close", 0, js_bitmap_close),
    JS_CFUNC_DEF("clear", 1, js_bitmap_clear),
    JS_CFUNC_DEF("fill", 1, js_bitmap_clear),
    JS_CFUNC_DEF("setPixel", 3, js_bitmap_set_pixel),
    JS_CFUNC_DEF("getPixel", 2, js_bitmap_get_pixel),
    JS_CFUNC_DEF("fillRect", 5, js_bitmap_fill_rect),
    JS_CFUNC_DEF("drawCircle", 4, js_bitmap_draw_circle),
    JS_CFUNC_DEF("fillCircle", 4, js_bitmap_fill_circle),
    JS_CFUNC_DEF("drawEllipse", 5, js_bitmap_draw_ellipse),
    JS_CFUNC_DEF("fillEllipse", 5, js_bitmap_fill_ellipse),
    JS_CFUNC_DEF("drawRect", 5, js_bitmap_draw_rect),
    JS_CFUNC_DEF("drawRoundRect", 6, js_bitmap_draw_round_rect),
    JS_CFUNC_DEF("fillRoundRect", 6, js_bitmap_fill_round_rect),
    JS_CFUNC_DEF("drawLine", 5, js_bitmap_draw_line),
    JS_CFUNC_DEF("drawPolyline", 2, js_bitmap_draw_polyline),
    JS_CFUNC_DEF("drawPolygon", 2, js_bitmap_draw_polygon),
    JS_CFUNC_DEF("fillPolygon", 2, js_bitmap_fill_polygon),
    JS_CFUNC_DEF("drawTriangle", 7, js_bitmap_draw_triangle),
    JS_CFUNC_DEF("fillTriangle", 7, js_bitmap_fill_triangle),
    JS_CFUNC_DEF("drawQuadraticBezier", 8, js_bitmap_draw_quadratic_bezier),
    JS_CFUNC_DEF("drawCubicBezier", 10, js_bitmap_draw_cubic_bezier),
    JS_CFUNC_DEF("drawMask", 4, js_bitmap_draw_mask),
    JS_CFUNC_DEF("blit", 2, js_bitmap_blit),
    JS_CFUNC_DEF("drawText", 5, js_bitmap_draw_text),
    JS_CFUNC_DEF("measureText", 2, js_bitmap_measure_text),
    JS_CFUNC_DEF("getDirty", 0, js_bitmap_get_dirty),
    JS_CFUNC_DEF("clearDirty", 0, js_bitmap_clear_dirty),
    JS_CFUNC_DEF("markDirty", 4, js_bitmap_mark_dirty),
    JS_CFUNC_DEF("readRect", 5, js_bitmap_read_rect),
    JS_CFUNC_DEF("readRectChunks", 5, js_bitmap_read_rect_chunks),
    JS_CFUNC_DEF("createSpanSource", 1, js_bitmap_create_span_source),
    JS_CFUNC_DEF("createCommandBuffer", 1, js_bitmap_create_command_buffer),
    JS_PROP_END,
};

static const JSClassDef js_bitmap_class =
    JS_CLASS_DEF("Bitmap", 0, js_bitmap_constructor, JS_CLASS_BITMAP, NULL, js_bitmap_proto, NULL, js_bitmap_finalizer);

static const JSPropDef js_bitmap[] = {
    JS_PROP_STRING_DEF("MONO1", "mono1", 0),
    JS_PROP_STRING_DEF("GRAY8", "gray8", 0),
    JS_PROP_STRING_DEF("RGB565", "rgb565", 0),
    JS_PROP_STRING_DEF("RGB888", "rgb888", 0),
    JS_CFUNC_DEF("create", 1, js_bitmap_create),
    JS_CFUNC_DEF("convert", 2, js_bitmap_convert),
    JS_CFUNC_DEF("loadFont", 1, js_bitmap_load_font),
    JS_PROP_END,
};

static const JSClassDef js_bitmap_obj =
    JS_OBJECT_DEF("bitmap", js_bitmap);
#endif

#if CONFIG_ESP32_MQUICKJS_FEATURE_FS
static const JSPropDef js_fs_volume_proto[] = {
    JS_CGETSET_DEF("ROOT", js_fs_get_root, NULL),
    JS_CFUNC_DEF("volume", 1, js_fs_volume),
    JS_CFUNC_DEF("info", 0, js_fs_info),
    JS_CFUNC_DEF("watch", 1, js_fs_watch),
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

static const JSClassDef js_fs_volume_class =
    JS_CLASS_DEF("FsVolume", 0, js_fs_volume_constructor,
                 JS_CLASS_FS_VOLUME, NULL, js_fs_volume_proto, NULL,
                 js_fs_volume_finalizer);

static const JSPropDef js_framework[] = {
    JS_CFUNC_DEF("load", 1, js_framework_load),
    JS_PROP_END,
};

static const JSClassDef js_framework_obj =
    JS_OBJECT_DEF("framework", js_framework);

#endif

#if CONFIG_ESP32_MQUICKJS_FEATURE_NVS
static const JSPropDef js_nvs[] = {
    JS_CGETSET_DEF("MAX_VALUE_BYTES", js_nvs_get_max_value_bytes, NULL),
    JS_CFUNC_DEF("getString", 2, js_nvs_getString),
    JS_CFUNC_DEF("setString", 3, js_nvs_setString),
    JS_CFUNC_DEF("erase", 2, js_nvs_erase),
    JS_CFUNC_DEF("clear", 1, js_nvs_clear),
    JS_CFUNC_DEF("status", 0, js_nvs_status),
    JS_PROP_END,
};

static const JSClassDef js_nvs_obj =
    JS_OBJECT_DEF("nvs", js_nvs);
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
    JS_CFUNC_DEF("watch", 2, js_gpio_watch),
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

static const JSPropDef js_sys_info_version[] = {
    JS_CGETSET_MAGIC_DEF("framework", js_sys_version_get, NULL, 0),
    JS_CGETSET_MAGIC_DEF("hostApi", js_sys_version_get, NULL, 1),
    JS_CGETSET_MAGIC_DEF("mquickjs", js_sys_version_get, NULL, 2),
    JS_CGETSET_MAGIC_DEF("espIdf", js_sys_version_get, NULL, 3),
    JS_PROP_END,
};

static const JSClassDef js_sys_info_version_obj =
    JS_OBJECT_DEF("version", js_sys_info_version);

static const JSPropDef js_sys_info_features[] = {
    JS_CGETSET_MAGIC_DEF("fs", js_sys_feature_get, NULL, 0),
    JS_CGETSET_MAGIC_DEF("nvs", js_sys_feature_get, NULL, 1),
    JS_CGETSET_MAGIC_DEF("gpio", js_sys_feature_get, NULL, 2),
    JS_CGETSET_MAGIC_DEF("ledc", js_sys_feature_get, NULL, 3),
    JS_CGETSET_MAGIC_DEF("adc", js_sys_feature_get, NULL, 4),
    JS_CGETSET_MAGIC_DEF("dac", js_sys_feature_get, NULL, 5),
    JS_CGETSET_MAGIC_DEF("i2c", js_sys_feature_get, NULL, 6),
    JS_CGETSET_MAGIC_DEF("spi", js_sys_feature_get, NULL, 7),
    JS_CGETSET_MAGIC_DEF("uart", js_sys_feature_get, NULL, 8),
    JS_CGETSET_MAGIC_DEF("usbSerial", js_sys_feature_get, NULL, 9),
    JS_CGETSET_MAGIC_DEF("socket", js_sys_feature_get, NULL, 10),
    JS_CGETSET_MAGIC_DEF("websocket", js_sys_feature_get, NULL, 11),
    JS_CGETSET_MAGIC_DEF("bitmap", js_sys_feature_get, NULL, 12),
    JS_CGETSET_MAGIC_DEF("wifi", js_sys_feature_get, NULL, 13),
    JS_CGETSET_MAGIC_DEF("http", js_sys_feature_get, NULL, 14),
    JS_CGETSET_MAGIC_DEF("httpServer", js_sys_feature_get, NULL, 15),
    JS_CGETSET_MAGIC_DEF("runtimeLogs", js_sys_feature_get, NULL, 16),
    JS_CGETSET_MAGIC_DEF("i2s", js_sys_feature_get, NULL, 17),
    JS_CGETSET_MAGIC_DEF("camera", js_sys_feature_get, NULL, 18),
    JS_CGETSET_MAGIC_DEF("rpc", js_sys_feature_get, NULL, 19),
    JS_CGETSET_MAGIC_DEF("rmt", js_sys_feature_get, NULL, 20),
    JS_CGETSET_MAGIC_DEF("tls", js_sys_feature_get, NULL, 21),
    JS_CGETSET_MAGIC_DEF("net", js_sys_feature_get, NULL, 22),
    JS_CGETSET_MAGIC_DEF("espNow", js_sys_feature_get, NULL, 23),
    JS_CGETSET_MAGIC_DEF("ble", js_sys_feature_get, NULL, 24),
    JS_PROP_END,
};

static const JSClassDef js_sys_info_features_obj =
    JS_OBJECT_DEF("features", js_sys_info_features);

static const JSPropDef js_sys_info_hardware[] = {
    JS_CGETSET_MAGIC_DEF("hardwareId", js_sys_hardware_get, NULL, 0),
    JS_CGETSET_MAGIC_DEF("target", js_sys_hardware_get, NULL, 1),
    JS_CGETSET_DEF("chip", js_sys_hardware_chip, NULL),
    JS_CGETSET_DEF("cpu", js_sys_hardware_cpu, NULL),
    JS_CGETSET_DEF("flash", js_sys_hardware_flash, NULL),
    JS_CGETSET_DEF("psram", js_sys_hardware_psram, NULL),
    JS_PROP_END,
};

static const JSClassDef js_sys_info_hardware_obj =
    JS_OBJECT_DEF("hardware", js_sys_info_hardware);

static const JSPropDef js_sys_info_runtime[] = {
    JS_CGETSET_DEF("heap", js_sys_runtime_info_heap, NULL),
    JS_CGETSET_DEF("task", js_sys_runtime_info_task, NULL),
    JS_CGETSET_MAGIC_DEF("evalTimeoutMs", js_sys_runtime_info_get, NULL, 0),
    JS_CGETSET_DEF("startup", js_sys_runtime_info_startup, NULL),
    JS_CGETSET_DEF("filesystem", js_sys_runtime_info_filesystem, NULL),
    JS_CGETSET_DEF("control", js_sys_runtime_info_control, NULL),
    JS_PROP_END,
};

static const JSClassDef js_sys_info_runtime_obj =
    JS_OBJECT_DEF("runtime", js_sys_info_runtime);

static const JSPropDef js_sys_info[] = {
    JS_PROP_CLASS_DEF("version", &js_sys_info_version_obj),
    JS_PROP_CLASS_DEF("hardware", &js_sys_info_hardware_obj),
    JS_PROP_CLASS_DEF("features", &js_sys_info_features_obj),
    JS_PROP_CLASS_DEF("runtime", &js_sys_info_runtime_obj),
    JS_PROP_END,
};

static const JSClassDef js_sys_info_obj =
    JS_OBJECT_DEF("info", js_sys_info);

static const JSPropDef js_sys_status_boot[] = {
    JS_CGETSET_MAGIC_DEF("bootId", js_sys_boot_get, NULL, 0),
    JS_CGETSET_MAGIC_DEF("uptimeMs", js_sys_boot_get, NULL, 1),
    JS_CGETSET_DEF("reset", js_sys_boot_reset, NULL),
    JS_CGETSET_DEF("wakeup", js_sys_boot_wakeup, NULL),
    JS_CGETSET_MAGIC_DEF("softwareReason", js_sys_boot_get, NULL, 2),
    JS_PROP_END,
};

static const JSClassDef js_sys_status_boot_obj =
    JS_OBJECT_DEF("boot", js_sys_status_boot);

static const JSPropDef js_sys_status_cpu[] = {
    JS_CGETSET_DEF("frequencyHz", js_sys_cpu_frequency, NULL),
    JS_PROP_END,
};

static const JSClassDef js_sys_status_cpu_obj =
    JS_OBJECT_DEF("cpu", js_sys_status_cpu);

static const JSPropDef js_sys_status_memory[] = {
    JS_CGETSET_MAGIC_DEF("default", js_sys_memory_get, NULL, 0),
    JS_CGETSET_MAGIC_DEF("internal", js_sys_memory_get, NULL, 1),
    JS_CGETSET_MAGIC_DEF("dma", js_sys_memory_get, NULL, 2),
    JS_CGETSET_MAGIC_DEF("psram", js_sys_memory_get, NULL, 3),
    JS_CGETSET_DEF("manager", js_sys_memory_manager, NULL),
    JS_PROP_END,
};

static const JSClassDef js_sys_status_memory_obj =
    JS_OBJECT_DEF("memory", js_sys_status_memory);

static const JSPropDef js_sys_status_rtos[] = {
    JS_CGETSET_MAGIC_DEF("name", js_sys_rtos_get, NULL, 0),
    JS_CGETSET_MAGIC_DEF("schedulerState", js_sys_rtos_get, NULL, 1),
    JS_CGETSET_MAGIC_DEF("tickRateHz", js_sys_rtos_get, NULL, 2),
    JS_CGETSET_MAGIC_DEF("taskCount", js_sys_rtos_get, NULL, 3),
    JS_CGETSET_DEF("runtimeTask", js_sys_rtos_runtime_task, NULL),
    JS_CGETSET_MAGIC_DEF("taskSnapshotSupported", js_sys_rtos_get, NULL, 4),
    JS_CGETSET_MAGIC_DEF("taskSnapshotLimit", js_sys_rtos_get, NULL, 5),
    JS_PROP_END,
};

static const JSClassDef js_sys_status_rtos_obj =
    JS_OBJECT_DEF("rtos", js_sys_status_rtos);

static const JSPropDef js_sys_status_runtime[] = {
    JS_CGETSET_MAGIC_DEF("state", js_sys_runtime_status_get, NULL, 0),
    JS_CGETSET_MAGIC_DEF("generation", js_sys_runtime_status_get, NULL, 1),
    JS_CGETSET_MAGIC_DEF("uptimeMs", js_sys_runtime_status_get, NULL, 2),
    JS_CGETSET_MAGIC_DEF("restartCount", js_sys_runtime_status_get, NULL, 3),
    JS_CGETSET_MAGIC_DEF("lastRestartReason", js_sys_runtime_status_get, NULL, 4),
    JS_CGETSET_MAGIC_DEF("pendingControl", js_sys_runtime_status_get, NULL, 5),
    JS_CGETSET_DEF("filesystem", js_sys_runtime_status_filesystem, NULL),
    JS_CGETSET_DEF("resources", js_sys_runtime_status_resources, NULL),
    JS_CGETSET_DEF("watchdog", js_sys_runtime_status_watchdog, NULL),
    JS_CGETSET_DEF("startup", js_sys_runtime_status_startup, NULL),
    JS_PROP_END,
};

static const JSClassDef js_sys_status_runtime_obj =
    JS_OBJECT_DEF("runtime", js_sys_status_runtime);

static const JSPropDef js_sys_status[] = {
    JS_PROP_CLASS_DEF("boot", &js_sys_status_boot_obj),
    JS_PROP_CLASS_DEF("cpu", &js_sys_status_cpu_obj),
    JS_PROP_CLASS_DEF("memory", &js_sys_status_memory_obj),
    JS_PROP_CLASS_DEF("rtos", &js_sys_status_rtos_obj),
    JS_PROP_CLASS_DEF("runtime", &js_sys_status_runtime_obj),
    JS_PROP_END,
};

static const JSClassDef js_sys_status_obj =
    JS_OBJECT_DEF("status", js_sys_status);

static const JSPropDef js_sys_time[] = {
    JS_CFUNC_DEF("status", 0, js_sys_time_status),
#if CONFIG_ESP32_MQUICKJS_FEATURE_NET
    JS_CFUNC_DEF("sync", 1, js_sys_time_sync),
#endif
    JS_PROP_END,
};

static const JSClassDef js_sys_time_obj =
    JS_OBJECT_DEF("time", js_sys_time);

static const JSPropDef js_sys[] = {
    JS_PROP_CLASS_DEF("info", &js_sys_info_obj),
    JS_PROP_CLASS_DEF("status", &js_sys_status_obj),
    JS_PROP_CLASS_DEF("time", &js_sys_time_obj),
    JS_CGETSET_DEF("safeMode", js_sys_safe_mode_get, js_sys_safe_mode_set),
    JS_CFUNC_DEF("config", 1, js_sys_config),
    JS_CFUNC_DEF("tasks", 1, js_sys_tasks),
    JS_CFUNC_DEF("restartRuntime", 1, js_sys_restart_runtime),
    JS_CFUNC_DEF("reboot", 1, js_sys_reboot),
    JS_CFUNC_DEF("millis", 0, js_sys_millis),
    JS_CFUNC_DEF("micros", 0, js_sys_micros),
    JS_CFUNC_DEF("freeHeap", 0, js_sys_freeHeap),
    JS_CFUNC_DEF("randomHex", 1, js_sys_randomHex),
    JS_CFUNC_DEF("withTimeout", 2, js_sys_withTimeout),
    JS_CFUNC_DEF("_deferIdle", 1, js_runtime_defer_idle),
    JS_PROP_END,
};

static const JSClassDef js_sys_obj =
    JS_OBJECT_DEF("sys", js_sys);

#if CONFIG_ESP32_MQUICKJS_FEATURE_RUNTIME_LOGS
static const JSPropDef js_runtime_logs[] = {
    JS_CFUNC_DEF("read", 3, js_runtime_logs_read),
    JS_PROP_END,
};

static const JSClassDef js_runtime_logs_obj =
    JS_OBJECT_DEF("runtimeLogs", js_runtime_logs);
#endif

#if CONFIG_ESP32_MQUICKJS_FEATURE_I2C
static const JSPropDef js_i2c_bus_proto[] = {
    JS_CFUNC_DEF("close", 0, js_i2c_bus_close),
    JS_CFUNC_DEF("status", 0, js_i2c_bus_status),
    JS_CFUNC_DEF("scan", 0, js_i2c_bus_scan),
    JS_CFUNC_DEF("openDevice", 1, js_i2c_bus_open_device),
    JS_PROP_END,
};

static const JSClassDef js_i2c_bus_class =
    JS_CLASS_DEF("I2CBus", 0, js_i2c_bus_constructor, JS_CLASS_I2C_BUS, NULL, js_i2c_bus_proto, NULL, js_i2c_bus_finalizer);

static const JSPropDef js_i2c_device_proto[] = {
    JS_CFUNC_DEF("close", 0, js_i2c_device_close),
    JS_CFUNC_DEF("status", 0, js_i2c_device_status),
    JS_CFUNC_DEF("write", 1, js_i2c_device_write),
    JS_CFUNC_DEF("writeSegments", 1, js_i2c_device_write_segments),
    JS_CFUNC_DEF("writeBatch", 1, js_i2c_device_write_batch),
    JS_CFUNC_DEF("read", 1, js_i2c_device_read),
    JS_CFUNC_DEF("writeRead", 2, js_i2c_device_write_read),
    JS_PROP_END,
};

static const JSClassDef js_i2c_device_class =
    JS_CLASS_DEF("I2CDevice", 0, js_i2c_device_constructor, JS_CLASS_I2C_DEVICE, NULL, js_i2c_device_proto, NULL, js_i2c_device_finalizer);

static const JSPropDef js_i2c[] = {
    JS_CGETSET_DEF("DEFAULT_SDA", js_i2c_get_default_sda, NULL),
    JS_CGETSET_DEF("DEFAULT_SCL", js_i2c_get_default_scl, NULL),
    JS_CGETSET_DEF("DEFAULT_FREQ_HZ", js_i2c_get_default_freq_hz, NULL),
    JS_CGETSET_DEF("DEFAULT_TIMEOUT_MS", js_i2c_get_default_timeout_ms, NULL),
    JS_CFUNC_DEF("openBus", 1, js_i2c_open_bus),
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
    JS_CFUNC_DEF("writeChunks", 2, js_spi_device_write_chunks),
    JS_CFUNC_DEF("writeSource", 2, js_spi_device_write_source),
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

#if CONFIG_ESP32_MQUICKJS_FEATURE_UART
static const JSPropDef js_uart_port_proto[] = {
    JS_CFUNC_DEF("close", 0, js_uart_port_close),
    JS_CFUNC_DEF("status", 0, js_uart_port_status),
    JS_CFUNC_DEF("write", 1, js_uart_port_write),
    JS_CFUNC_DEF("writeChunks", 1, js_uart_port_write_chunks),
    JS_CFUNC_DEF("writeSource", 1, js_uart_port_write_source),
    JS_CFUNC_DEF("read", 2, js_uart_port_read),
    JS_CFUNC_DEF("available", 0, js_uart_port_available),
    JS_CFUNC_DEF("flush", 1, js_uart_port_flush),
    JS_CFUNC_DEF("clearRx", 0, js_uart_port_clear_rx),
    JS_CFUNC_DEF("watch", 1, js_uart_port_watch),
    JS_PROP_END,
};

static const JSClassDef js_uart_port_class =
    JS_CLASS_DEF("UARTPort", 0, js_uart_port_constructor, JS_CLASS_UART_PORT, NULL, js_uart_port_proto, NULL, js_uart_port_finalizer);

static const JSPropDef js_uart[] = {
    JS_CGETSET_DEF("DEFAULT_PORT", js_uart_get_default_port, NULL),
    JS_CGETSET_DEF("DEFAULT_TX", js_uart_get_default_tx, NULL),
    JS_CGETSET_DEF("DEFAULT_RX", js_uart_get_default_rx, NULL),
    JS_CGETSET_DEF("DEFAULT_BAUD", js_uart_get_default_baud, NULL),
    JS_CGETSET_DEF("DEFAULT_RX_BUFFER_SIZE", js_uart_get_default_rx_buffer_size, NULL),
    JS_CGETSET_DEF("DEFAULT_TX_BUFFER_SIZE", js_uart_get_default_tx_buffer_size, NULL),
    JS_CGETSET_DEF("DEFAULT_TIMEOUT_MS", js_uart_get_default_timeout_ms, NULL),
    JS_CFUNC_DEF("open", 1, js_uart_open),
    JS_PROP_END,
};

static const JSClassDef js_uart_obj =
    JS_OBJECT_DEF("uart", js_uart);
#endif

#if CONFIG_ESP32_MQUICKJS_FEATURE_RMT
static const JSPropDef js_rmt_symbol_buffer_proto[] = {
    JS_CGETSET_DEF("capacity", js_rmt_symbol_buffer_get_capacity, NULL),
    JS_CGETSET_DEF("length", js_rmt_symbol_buffer_get_length, NULL),
    JS_CFUNC_DEF("push", 4, js_rmt_symbol_buffer_push),
    JS_CFUNC_DEF("get", 1, js_rmt_symbol_buffer_get),
    JS_CFUNC_DEF("set", 5, js_rmt_symbol_buffer_set),
    JS_CFUNC_DEF("clear", 0, js_rmt_symbol_buffer_clear),
    JS_CFUNC_DEF("close", 0, js_rmt_symbol_buffer_close),
    JS_PROP_END,
};

static const JSClassDef js_rmt_symbol_buffer_class =
    JS_CLASS_DEF("RMTSymbolBuffer", 0, js_rmt_symbol_buffer_constructor,
                 JS_CLASS_RMT_SYMBOL_BUFFER, NULL,
                 js_rmt_symbol_buffer_proto, NULL,
                 js_rmt_symbol_buffer_finalizer);

static const JSPropDef js_rmt_channel_proto[] = {
    JS_CFUNC_DEF("start", 0, js_rmt_channel_start),
    JS_CFUNC_DEF("stop", 0, js_rmt_channel_stop),
    JS_CFUNC_DEF("transmit", 2, js_rmt_channel_transmit),
    JS_CFUNC_DEF("receive", 2, js_rmt_channel_receive),
    JS_CFUNC_DEF("status", 0, js_rmt_channel_status),
    JS_CFUNC_DEF("close", 0, js_rmt_channel_close),
    JS_PROP_END,
};

static const JSClassDef js_rmt_channel_class =
    JS_CLASS_DEF("RMTChannel", 0, js_rmt_channel_constructor,
                 JS_CLASS_RMT_CHANNEL, NULL, js_rmt_channel_proto, NULL,
                 js_rmt_channel_finalizer);

static const JSPropDef js_rmt[] = {
    JS_CFUNC_DEF("capabilities", 0, js_rmt_capabilities),
    JS_CFUNC_DEF("createSymbols", 1, js_rmt_create_symbols),
    JS_CFUNC_DEF("open", 1, js_rmt_open),
    JS_PROP_END,
};

static const JSClassDef js_rmt_obj = JS_OBJECT_DEF("rmt", js_rmt);
#endif

#if CONFIG_ESP32_MQUICKJS_FEATURE_ESPNOW
static const JSPropDef js_espnow_session_proto[] = {
    JS_CFUNC_DEF("receive", 1, js_espnow_session_receive),
    JS_CFUNC_DEF("stats", 0, js_espnow_session_stats),
    JS_CFUNC_DEF("status", 0, js_espnow_session_status),
    JS_CFUNC_DEF("addPeer", 1, js_espnow_session_add_peer),
    JS_CFUNC_DEF("peer", 1, js_espnow_session_peer),
    JS_CFUNC_DEF("peers", 0, js_espnow_session_peers),
    JS_CFUNC_DEF("broadcast", 2, js_espnow_session_broadcast),
    JS_CFUNC_DEF("setPowerSave", 1, js_espnow_session_set_power_save),
    JS_CFUNC_DEF("close", 0, js_espnow_session_close),
    JS_PROP_END,
};

static const JSClassDef js_espnow_session_class =
    JS_CLASS_DEF("EspNowSession", 0, js_espnow_session_constructor,
                 JS_CLASS_ESPNOW_SESSION, NULL, js_espnow_session_proto, NULL,
                 js_espnow_session_finalizer);

static const JSPropDef js_espnow_peer_proto[] = {
    JS_CFUNC_DEF("status", 0, js_espnow_peer_status),
    JS_CFUNC_DEF("send", 2, js_espnow_peer_send),
    JS_CFUNC_DEF("update", 1, js_espnow_peer_update),
    JS_CFUNC_DEF("close", 0, js_espnow_peer_close),
    JS_PROP_END,
};

static const JSClassDef js_espnow_peer_class =
    JS_CLASS_DEF("EspNowPeer", 0, js_espnow_peer_constructor,
                 JS_CLASS_ESPNOW_PEER, NULL, js_espnow_peer_proto, NULL,
                 js_espnow_peer_finalizer);

static const JSPropDef js_espnow[] = {
    JS_PROP_STRING_DEF("BROADCAST_ADDRESS", "ff:ff:ff:ff:ff:ff", 0),
    JS_PROP_DOUBLE_DEF("MAX_PAYLOAD_V1", 250, 0),
    JS_PROP_DOUBLE_DEF("MAX_PAYLOAD_V2", 1470, 0),
    JS_CFUNC_DEF("capabilities", 0, js_espnow_capabilities),
    JS_CFUNC_DEF("open", 1, js_espnow_open),
    JS_PROP_END,
};

static const JSClassDef js_espnow_obj =
    JS_OBJECT_DEF("espNow", js_espnow);
#endif

#if CONFIG_ESP32_MQUICKJS_FEATURE_BLE
static const JSPropDef js_ble_adapter_proto[] = {
    JS_CFUNC_DEF("status", 0, js_ble_adapter_status),
    JS_CFUNC_DEF("scan", 1, js_ble_adapter_scan),
    JS_CFUNC_DEF("connect", 2, js_ble_adapter_connect),
    JS_CFUNC_DEF("advertise", 1, js_ble_adapter_advertise),
    JS_CFUNC_DEF("server", 0, js_ble_adapter_server),
    JS_CFUNC_DEF("bonds", 0, js_ble_adapter_bonds),
    JS_CFUNC_DEF("removeBond", 1, js_ble_adapter_remove_bond),
    JS_CFUNC_DEF("clearBonds", 0, js_ble_adapter_clear_bonds),
    JS_CFUNC_DEF("close", 0, js_ble_adapter_close),
    JS_PROP_END,
};

static const JSClassDef js_ble_adapter_class =
    JS_CLASS_DEF("BLEAdapter", 0, js_ble_adapter_constructor,
                 JS_CLASS_BLE_ADAPTER, NULL, js_ble_adapter_proto, NULL,
                 js_ble_adapter_finalizer);

static const JSPropDef js_ble_scanner_proto[] = {
    JS_CFUNC_DEF("receive", 1, js_ble_scanner_receive),
    JS_CFUNC_DEF("stats", 0, js_ble_scanner_stats),
    JS_CFUNC_DEF("status", 0, js_ble_scanner_status),
    JS_CFUNC_DEF("close", 0, js_ble_scanner_close),
    JS_PROP_END,
};

static const JSClassDef js_ble_scanner_class =
    JS_CLASS_DEF("BLEScanner", 0, js_ble_scanner_constructor,
                 JS_CLASS_BLE_SCANNER, NULL, js_ble_scanner_proto, NULL,
                 js_ble_scanner_finalizer);

static const JSPropDef js_ble_advertiser_proto[] = {
    JS_CFUNC_DEF("receive", 1, js_ble_advertiser_receive),
    JS_CFUNC_DEF("stats", 0, js_ble_advertiser_stats),
    JS_CFUNC_DEF("status", 0, js_ble_advertiser_status),
    JS_CFUNC_DEF("close", 0, js_ble_advertiser_close),
    JS_PROP_END,
};

static const JSClassDef js_ble_advertiser_class =
    JS_CLASS_DEF("BLEAdvertiser", 0, js_ble_advertiser_constructor,
                 JS_CLASS_BLE_ADVERTISER, NULL, js_ble_advertiser_proto, NULL,
                 js_ble_advertiser_finalizer);

static const JSPropDef js_ble_connection_proto[] = {
    JS_CFUNC_DEF("receive", 1, js_ble_connection_receive),
    JS_CFUNC_DEF("stats", 0, js_ble_connection_stats),
    JS_CFUNC_DEF("status", 0, js_ble_connection_status),
    JS_CFUNC_DEF("pair", 1, js_ble_connection_pair),
    JS_CFUNC_DEF("respondPairing", 2, js_ble_connection_respond_pairing),
    JS_CFUNC_DEF("exchangeMtu", 2, js_ble_connection_exchange_mtu),
    JS_CFUNC_DEF("readRssi", 1, js_ble_connection_read_rssi),
    JS_CFUNC_DEF("discover", 1, js_ble_connection_discover),
    JS_CFUNC_DEF("close", 0, js_ble_connection_close),
    JS_PROP_END,
};

static const JSClassDef js_ble_connection_class =
    JS_CLASS_DEF("BLEConnection", 0, js_ble_connection_constructor,
                 JS_CLASS_BLE_CONNECTION, NULL, js_ble_connection_proto, NULL,
                 js_ble_connection_finalizer);

static const JSPropDef js_ble_service_proto[] = {
    JS_CFUNC_DEF("characteristics", 0, js_ble_service_characteristics),
    JS_PROP_END,
};

static const JSClassDef js_ble_service_class =
    JS_CLASS_DEF("BLEService", 0, js_ble_service_constructor,
                 JS_CLASS_BLE_SERVICE, NULL, js_ble_service_proto, NULL,
                 js_ble_service_finalizer);

static const JSPropDef js_ble_characteristic_proto[] = {
    JS_CFUNC_DEF("descriptors", 0, js_ble_characteristic_descriptors),
    JS_CFUNC_DEF("read", 1, js_ble_characteristic_read),
    JS_CFUNC_DEF("write", 2, js_ble_characteristic_write),
    JS_CFUNC_DEF("subscribe", 1, js_ble_characteristic_subscribe),
    JS_PROP_END,
};

static const JSClassDef js_ble_characteristic_class =
    JS_CLASS_DEF("BLECharacteristic", 0, js_ble_characteristic_constructor,
                 JS_CLASS_BLE_CHARACTERISTIC, NULL,
                 js_ble_characteristic_proto, NULL,
                 js_ble_characteristic_finalizer);

static const JSPropDef js_ble_descriptor_proto[] = {
    JS_CFUNC_DEF("read", 1, js_ble_descriptor_read),
    JS_CFUNC_DEF("write", 2, js_ble_descriptor_write),
    JS_PROP_END,
};

static const JSClassDef js_ble_descriptor_class =
    JS_CLASS_DEF("BLEDescriptor", 0, js_ble_descriptor_constructor,
                 JS_CLASS_BLE_DESCRIPTOR, NULL, js_ble_descriptor_proto, NULL,
                 js_ble_descriptor_finalizer);

static const JSPropDef js_ble_notification_proto[] = {
    JS_CFUNC_DEF("receive", 1, js_ble_notification_receive),
    JS_CFUNC_DEF("stats", 0, js_ble_notification_stats),
    JS_CFUNC_DEF("status", 0, js_ble_notification_status),
    JS_CFUNC_DEF("close", 0, js_ble_notification_close),
    JS_PROP_END,
};

static const JSClassDef js_ble_notification_class =
    JS_CLASS_DEF("BLENotificationStream", 0, js_ble_notification_constructor,
                 JS_CLASS_BLE_NOTIFICATION_STREAM, NULL,
                 js_ble_notification_proto, NULL,
                 js_ble_notification_finalizer);

static const JSPropDef js_ble_gatt_server_proto[] = {
    JS_CFUNC_DEF("status", 0, js_ble_gatt_server_status),
    JS_CFUNC_DEF("watch", 1, js_ble_gatt_server_watch),
    JS_CFUNC_DEF("characteristic", 1, js_ble_gatt_server_characteristic),
    JS_PROP_END,
};

static const JSClassDef js_ble_gatt_server_class =
    JS_CLASS_DEF("BLEGattServer", 0, js_ble_gatt_server_constructor,
                 JS_CLASS_BLE_GATT_SERVER, NULL, js_ble_gatt_server_proto,
                 NULL, js_ble_gatt_server_finalizer);

static const JSPropDef js_ble_local_characteristic_proto[] = {
    JS_CFUNC_DEF("value", 0, js_ble_local_characteristic_value),
    JS_CFUNC_DEF("setValue", 1, js_ble_local_characteristic_set_value),
    JS_CFUNC_DEF("notify", 2, js_ble_local_characteristic_notify),
    JS_PROP_END,
};

static const JSClassDef js_ble_local_characteristic_class =
    JS_CLASS_DEF("BLELocalCharacteristic", 0,
                 js_ble_local_characteristic_constructor,
                 JS_CLASS_BLE_LOCAL_CHARACTERISTIC, NULL,
                 js_ble_local_characteristic_proto, NULL,
                 js_ble_local_characteristic_finalizer);

static const JSPropDef js_ble[] = {
    JS_CFUNC_DEF("capabilities", 0, js_ble_capabilities),
    JS_CFUNC_DEF("open", 1, js_ble_open),
    JS_PROP_END,
};

static const JSClassDef js_ble_obj = JS_OBJECT_DEF("ble", js_ble);
#endif

#if CONFIG_ESP32_MQUICKJS_FEATURE_I2S
static const JSPropDef js_i2s_channel_proto[] = {
    JS_CFUNC_DEF("start", 0, js_i2s_channel_start),
    JS_CFUNC_DEF("stop", 0, js_i2s_channel_stop),
    JS_CFUNC_DEF("read", 2, js_i2s_channel_read),
    JS_CFUNC_DEF("write", 2, js_i2s_channel_write),
    JS_CFUNC_DEF("status", 0, js_i2s_channel_status),
    JS_CFUNC_DEF("close", 0, js_i2s_channel_close),
    JS_PROP_END,
};

static const JSClassDef js_i2s_channel_class =
    JS_CLASS_DEF("I2SChannel", 0, js_i2s_channel_constructor,
                 JS_CLASS_I2S_CHANNEL, NULL, js_i2s_channel_proto, NULL,
                 js_i2s_channel_finalizer);

static const JSPropDef js_i2s[] = {
    JS_CFUNC_DEF("capabilities", 0, js_i2s_capabilities),
    JS_CFUNC_DEF("open", 1, js_i2s_open),
    JS_PROP_END,
};

static const JSClassDef js_i2s_obj = JS_OBJECT_DEF("i2s", js_i2s);
#endif

#if CONFIG_ESP32_MQUICKJS_FEATURE_CAMERA
static const JSPropDef js_camera_proto[] = {
    JS_CFUNC_DEF("capture", 1, js_camera_capture),
    JS_CFUNC_DEF("status", 0, js_camera_status),
    JS_CFUNC_DEF("controls", 0, js_camera_controls),
    JS_CFUNC_DEF("setControl", 2, js_camera_set_control),
    JS_CFUNC_DEF("close", 0, js_camera_close),
    JS_PROP_END,
};

static const JSClassDef js_camera_class =
    JS_CLASS_DEF("Camera", 0, js_camera_constructor, JS_CLASS_CAMERA,
                 NULL, js_camera_proto, NULL, js_camera_finalizer);

static const JSPropDef js_camera_frame_proto[] = {
    JS_CFUNC_DEF("source", 1, js_camera_frame_source),
    JS_CFUNC_DEF("read", 2, js_camera_frame_read),
    JS_CFUNC_DEF("close", 0, js_camera_frame_close),
    JS_PROP_END,
};

static const JSClassDef js_camera_frame_class =
    JS_CLASS_DEF("CameraFrame", 0, js_camera_frame_constructor,
                 JS_CLASS_CAMERA_FRAME, NULL, js_camera_frame_proto, NULL,
                 js_camera_frame_finalizer);

static const JSPropDef js_camera_module[] = {
    JS_CFUNC_DEF("capabilities", 0, js_camera_capabilities),
    JS_CFUNC_DEF("open", 1, js_camera_open),
    JS_PROP_END,
};

static const JSClassDef js_camera_obj =
    JS_OBJECT_DEF("camera", js_camera_module);
#endif

#if CONFIG_ESP32_MQUICKJS_FEATURE_USB_SERIAL
static const JSPropDef js_usb_serial[] = {
    JS_CGETSET_DEF("MAX_FRAME_BYTES", js_usb_serial_get_max_frame_bytes, NULL),
    JS_CFUNC_DEF("open", 1, js_usb_serial_open),
    JS_CFUNC_DEF("close", 0, js_usb_serial_close),
    JS_CFUNC_DEF("send", 1, js_usb_serial_send),
    JS_CFUNC_DEF("status", 0, js_usb_serial_status),
    JS_PROP_END,
};

static const JSClassDef js_usb_serial_obj =
    JS_OBJECT_DEF("usbSerial", js_usb_serial);
#endif

#if CONFIG_ESP32_MQUICKJS_FEATURE_RPC
static const JSPropDef js_rpc_codec_proto[] = {
    JS_CFUNC_DEF("createDecoder", 0, js_rpc_create_decoder),
    JS_CFUNC_DEF("encode", 4, js_rpc_encode),
    JS_CFUNC_DEF("close", 0, js_rpc_codec_close),
    JS_PROP_END,
};

static const JSClassDef js_rpc_codec_class =
    JS_CLASS_DEF("RPCCodec", 0, js_rpc_codec_constructor,
                 JS_CLASS_RPC_CODEC, NULL, js_rpc_codec_proto, NULL,
                 js_rpc_codec_finalizer);

static const JSPropDef js_rpc_decoder_proto[] = {
    JS_CFUNC_DEF("feed", 1, js_rpc_feed),
    JS_CFUNC_DEF("reset", 0, js_rpc_reset_decoder),
    JS_CFUNC_DEF("status", 0, js_rpc_decoder_status),
    JS_CFUNC_DEF("close", 0, js_rpc_decoder_close),
    JS_PROP_END,
};

static const JSClassDef js_rpc_decoder_class =
    JS_CLASS_DEF("RPCDecoder", 0, js_rpc_decoder_constructor,
                 JS_CLASS_RPC_DECODER, NULL, js_rpc_decoder_proto, NULL,
                 js_rpc_decoder_finalizer);

static const JSPropDef js_rpc[] = {
    JS_PROP_STRING_DEF("PROTOCOL", "esp32qjs.rpc/1", 0),
    JS_PROP_DOUBLE_DEF("RESPONSE", 4, 0),
    JS_PROP_DOUBLE_DEF("ERROR", 8, 0),
    JS_PROP_DOUBLE_DEF("MAX_FRAME_BYTES", 7740, 0),
    JS_PROP_DOUBLE_DEF("MAX_MESSAGE_BYTES", 65536, 0),
    JS_PROP_DOUBLE_DEF("MAX_STREAM_BYTES", 33554432, 0),
    JS_PROP_DOUBLE_DEF("SEGMENT_PAYLOAD_BYTES", 7680, 0),
    JS_CFUNC_DEF("createCodec", 1, js_rpc_create_codec),
    JS_CFUNC_DEF("bytes", 1, js_rpc_bytes),
    JS_CFUNC_DEF("fileSource", 1, js_rpc_file_source),
    JS_CFUNC_DEF("sourceInfo", 1, js_rpc_source_info),
    JS_CFUNC_DEF("adoptFile", 2, js_rpc_adopt_file),
    JS_CFUNC_DEF("status", 0, js_rpc_status),
    JS_PROP_END,
};

static const JSClassDef js_rpc_obj =
    JS_OBJECT_DEF("rpc", js_rpc);
#endif

#if CONFIG_ESP32_MQUICKJS_FEATURE_SOCKET
static const JSPropDef js_tcp_socket_proto[] = {
    JS_CFUNC_DEF("connect", 3, js_socket_tcp_connect),
    JS_CFUNC_DEF("send", 2, js_socket_tcp_send),
    JS_CFUNC_DEF("recv", 2, js_socket_tcp_recv),
    JS_CFUNC_DEF("status", 0, js_socket_handle_status),
    JS_CFUNC_DEF("close", 0, js_socket_handle_close),
    JS_PROP_END,
};

static const JSClassDef js_tcp_socket_class =
    JS_CLASS_DEF("TCPSocket", 0, js_socket_handle_constructor,
                 JS_CLASS_TCP_SOCKET, NULL, js_tcp_socket_proto, NULL,
                 js_socket_handle_finalizer);

static const JSPropDef js_tcp_listener_proto[] = {
    JS_CFUNC_DEF("accept", 1, js_socket_tcp_accept),
    JS_CFUNC_DEF("status", 0, js_socket_handle_status),
    JS_CFUNC_DEF("close", 0, js_socket_handle_close),
    JS_PROP_END,
};

static const JSClassDef js_tcp_listener_class =
    JS_CLASS_DEF("TCPListener", 0, js_socket_handle_constructor,
                 JS_CLASS_TCP_LISTENER, NULL, js_tcp_listener_proto, NULL,
                 js_socket_handle_finalizer);

static const JSPropDef js_udp_socket_proto[] = {
    JS_CFUNC_DEF("sendTo", 3, js_socket_udp_send_to),
    JS_CFUNC_DEF("receiveFrom", 2, js_socket_udp_receive_from),
    JS_CFUNC_DEF("status", 0, js_socket_handle_status),
    JS_CFUNC_DEF("close", 0, js_socket_handle_close),
    JS_PROP_END,
};

static const JSClassDef js_udp_socket_class =
    JS_CLASS_DEF("UDPSocket", 0, js_socket_handle_constructor,
                 JS_CLASS_UDP_SOCKET, NULL, js_udp_socket_proto, NULL,
                 js_socket_handle_finalizer);

static const JSPropDef js_socket[] = {
    JS_CFUNC_DEF("openTCP", 1, js_socket_open_tcp),
    JS_CFUNC_DEF("listenTCP", 1, js_socket_listen_tcp),
    JS_CFUNC_DEF("openUDP", 1, js_socket_open_udp),
    JS_CGETSET_DEF("MAX_TRANSFER_BYTES", js_socket_get_max_transfer_bytes, NULL),
    JS_PROP_END,
};

static const JSClassDef js_socket_obj =
    JS_OBJECT_DEF("socket", js_socket);
#endif

#if CONFIG_ESP32_MQUICKJS_FEATURE_WEBSOCKET
static const JSPropDef js_websocket_client[] = {
    JS_CGETSET_DEF("MAX_MESSAGE_BYTES", js_websocket_get_max_message_bytes, NULL),
    JS_CFUNC_DEF("open", 1, js_websocket_open),
    JS_CFUNC_DEF("close", 0, js_websocket_close),
    JS_CFUNC_DEF("send", 1, js_websocket_send),
    JS_CFUNC_DEF("status", 0, js_websocket_status),
    JS_PROP_END,
};

static const JSClassDef js_websocket_client_obj =
    JS_OBJECT_DEF("websocketClient", js_websocket_client);
#endif

#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
static const JSPropDef js_wifi[] = {
    JS_CGETSET_DEF("DEFAULT_TIMEOUT_MS", js_wifi_get_default_timeout_ms, NULL),
    JS_CFUNC_DEF("connect", 3, js_wifi_connect),
    JS_CFUNC_DEF("disconnect", 1, js_wifi_disconnect),
    JS_CFUNC_DEF("setTxPower", 1, js_wifi_set_tx_power),
    JS_CFUNC_DEF("status", 0, js_wifi_status),
    JS_CFUNC_DEF("scan", 0, js_wifi_scan),
    JS_PROP_END,
};

static const JSClassDef js_wifi_obj =
    JS_OBJECT_DEF("wifi", js_wifi);
#endif

#if CONFIG_ESP32_MQUICKJS_FEATURE_NET
static const JSPropDef js_net[] = {
    JS_CFUNC_DEF("status", 0, js_net_status),
    JS_CFUNC_DEF("watch", 0, js_net_watch),
    JS_PROP_END,
};

static const JSClassDef js_net_obj =
    JS_OBJECT_DEF("net", js_net);
#endif

#if CONFIG_ESP32_MQUICKJS_FEATURE_HTTP || CONFIG_ESP32_MQUICKJS_FEATURE_HTTP_SERVER
static const JSPropDef js_http[] = {
#if CONFIG_ESP32_MQUICKJS_FEATURE_HTTP
    JS_CGETSET_DEF("DEFAULT_TIMEOUT_MS", js_http_get_default_timeout_ms, NULL),
    JS_CGETSET_DEF("MAX_BODY_BYTES", js_http_get_max_body_bytes, NULL),
    JS_CFUNC_DEF("fetch", 2, js_http_fetch),
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_HTTP_SERVER
    JS_CFUNC_DEF("server", 1, js_http_server_create),
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
    JS_CFUNC_DEF("close", 0, js_http_server_close),
    JS_CFUNC_DEF("receive", 1, js_http_server_receive),
    JS_CFUNC_DEF("stats", 0, js_http_server_stats),
    JS_CFUNC_DEF("route", 2, js_http_server_route),
    JS_CFUNC_DEF("respond", 2, js_http_server_respond),
    JS_CFUNC_DEF("removeRoute", 2, js_http_server_remove_route),
    JS_CFUNC_DEF("clearRoutes", 0, js_http_server_clear_routes),
    JS_PROP_END,
};

static const JSClassDef js_http_server_class =
    JS_CLASS_DEF("HttpServer", 0, js_http_server_constructor, JS_CLASS_HTTP_SERVER, NULL, js_http_server_proto, NULL, js_http_server_finalizer);
#endif

static const JSPropDef js_global_object_extra[] = {
    JS_PROP_CLASS_DEF("Headers", &js_headers_class),
    JS_PROP_CLASS_DEF("Request", &js_request_class),
    JS_PROP_CLASS_DEF("Response", &js_response_class),
    JS_PROP_CLASS_DEF("Future", &js_future_class),
    JS_PROP_CLASS_DEF("EventQueue", &js_event_queue_class),
    JS_PROP_CLASS_DEF("Stream", &js_stream_class),
    JS_PROP_CLASS_DEF("_ByteView", &js_byte_view_class),
    JS_PROP_CLASS_DEF("_ByteSpanSource", &js_byte_span_source_class),
#if CONFIG_ESP32_MQUICKJS_FEATURE_BITMAP
    JS_PROP_CLASS_DEF("_BitmapSpanSource", &js_bitmap_span_source_class),
    JS_PROP_CLASS_DEF("DisplayFont", &js_display_font_class),
    JS_PROP_CLASS_DEF("DisplayCommandBuffer", &js_display_command_buffer_class),
    JS_PROP_CLASS_DEF("Bitmap", &js_bitmap_class),
    JS_PROP_CLASS_DEF("bitmap", &js_bitmap_obj),
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_FS
    JS_PROP_CLASS_DEF("FsVolume", &js_fs_volume_class),
    JS_PROP_CLASS_DEF("framework", &js_framework_obj),
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_NVS
    JS_PROP_CLASS_DEF("nvs", &js_nvs_obj),
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
    JS_PROP_CLASS_DEF("sys", &js_sys_obj),
#if CONFIG_ESP32_MQUICKJS_FEATURE_RPC
    JS_PROP_CLASS_DEF("rpc", &js_rpc_obj),
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_RUNTIME_LOGS
    JS_PROP_CLASS_DEF("runtimeLogs", &js_runtime_logs_obj),
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_I2C
    JS_PROP_CLASS_DEF("i2c", &js_i2c_obj),
    JS_PROP_CLASS_DEF("I2CBus", &js_i2c_bus_class),
    JS_PROP_CLASS_DEF("I2CDevice", &js_i2c_device_class),
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_SPI
    JS_PROP_CLASS_DEF("spi", &js_spi_obj),
    JS_PROP_CLASS_DEF("SPIBus", &js_spi_bus_class),
    JS_PROP_CLASS_DEF("SPIDevice", &js_spi_device_class),
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_UART
    JS_PROP_CLASS_DEF("uart", &js_uart_obj),
    JS_PROP_CLASS_DEF("UARTPort", &js_uart_port_class),
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_RMT
    JS_PROP_CLASS_DEF("rmt", &js_rmt_obj),
    JS_PROP_CLASS_DEF("RMTSymbolBuffer", &js_rmt_symbol_buffer_class),
    JS_PROP_CLASS_DEF("RMTChannel", &js_rmt_channel_class),
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_ESPNOW
    JS_PROP_CLASS_DEF("espNow", &js_espnow_obj),
    JS_PROP_CLASS_DEF("EspNowSession", &js_espnow_session_class),
    JS_PROP_CLASS_DEF("EspNowPeer", &js_espnow_peer_class),
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_BLE
    JS_PROP_CLASS_DEF("ble", &js_ble_obj),
    JS_PROP_CLASS_DEF("BLEAdapter", &js_ble_adapter_class),
    JS_PROP_CLASS_DEF("BLEScanner", &js_ble_scanner_class),
    JS_PROP_CLASS_DEF("BLEAdvertiser", &js_ble_advertiser_class),
    JS_PROP_CLASS_DEF("BLEConnection", &js_ble_connection_class),
    JS_PROP_CLASS_DEF("BLEService", &js_ble_service_class),
    JS_PROP_CLASS_DEF("BLECharacteristic", &js_ble_characteristic_class),
    JS_PROP_CLASS_DEF("BLEDescriptor", &js_ble_descriptor_class),
    JS_PROP_CLASS_DEF("BLENotificationStream", &js_ble_notification_class),
    JS_PROP_CLASS_DEF("BLEGattServer", &js_ble_gatt_server_class),
    JS_PROP_CLASS_DEF("BLELocalCharacteristic", &js_ble_local_characteristic_class),
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_I2S
    JS_PROP_CLASS_DEF("i2s", &js_i2s_obj),
    JS_PROP_CLASS_DEF("I2SChannel", &js_i2s_channel_class),
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_CAMERA
    JS_PROP_CLASS_DEF("camera", &js_camera_obj),
    JS_PROP_CLASS_DEF("Camera", &js_camera_class),
    JS_PROP_CLASS_DEF("CameraFrame", &js_camera_frame_class),
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
    JS_PROP_CLASS_DEF("wifi", &js_wifi_obj),
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_NET
    JS_PROP_CLASS_DEF("net", &js_net_obj),
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_USB_SERIAL
    JS_PROP_CLASS_DEF("usbSerial", &js_usb_serial_obj),
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_SOCKET
    JS_PROP_CLASS_DEF("socket", &js_socket_obj),
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_WEBSOCKET
    JS_PROP_CLASS_DEF("websocketClient", &js_websocket_client_obj),
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_HTTP || CONFIG_ESP32_MQUICKJS_FEATURE_HTTP_SERVER
    JS_PROP_CLASS_DEF("http", &js_http_obj),
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_HTTP_SERVER
    JS_PROP_CLASS_DEF("HttpServer", &js_http_server_class),
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_SOCKET
    JS_PROP_CLASS_DEF("TCPSocket", &js_tcp_socket_class),
    JS_PROP_CLASS_DEF("TCPListener", &js_tcp_listener_class),
    JS_PROP_CLASS_DEF("UDPSocket", &js_udp_socket_class),
#endif
#if CONFIG_ESP32_MQUICKJS_FEATURE_RPC
    JS_PROP_CLASS_DEF("RPCCodec", &js_rpc_codec_class),
    JS_PROP_CLASS_DEF("RPCDecoder", &js_rpc_decoder_class),
#endif
    JS_CFUNC_DEF("help", 0, js_help),
    JS_CFUNC_DEF("sleep", 1, js_sleep),
    JS_CFUNC_DEF("delay", 1, js_sleep),
    JS_CFUNC_DEF("setInterval", 2, js_setInterval),
    JS_CFUNC_DEF("clearInterval", 1, js_clearTimeout),
#if CONFIG_ESP32_MQUICKJS_FEATURE_HTTP
    JS_CFUNC_DEF("fetch", 2, js_http_fetch),
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
