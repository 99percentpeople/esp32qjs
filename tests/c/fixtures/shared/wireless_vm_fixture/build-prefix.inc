#include "utils/esp32_mquickjs_byte_source.h"
enum { JS_CLASS_BYTE_VIEW=JS_CLASS_USER+11, JS_CLASS_BYTE_SPAN_SOURCE, JS_CLASS_BITMAP_SPAN_SOURCE };
#define JS_CLASS_COUNT (JS_CLASS_USER+52)
#include "mqjs_stdlib.h"