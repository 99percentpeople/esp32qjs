#include "esp32_mquickjs_request_response.h"
#include "esp32_mquickjs_core.h"
#include "esp32_mquickjs_stream.h"

#include <ctype.h>
#include <string.h>

#include "esp_heap_caps.h"

static bool rr_is_object(JSContext *ctx, JSValue value)
{
    return JS_GetClassID(ctx, value) >= 0;
}

static JSValue rr_call_function(JSContext *ctx,
                                JSValue func,
                                JSValue this_val,
                                int argc,
                                JSValue *argv)
{
    int i;

    if (JS_StackCheck(ctx, (uint32_t)(argc + 2))) {
        return JS_EXCEPTION;
    }

    for (i = argc - 1; i >= 0; --i) {
        JS_PushArg(ctx, argv[i]);
    }
    JS_PushArg(ctx, func);
    JS_PushArg(ctx, this_val);
    return JS_Call(ctx, argc);
}

static char *rr_strdup(const char *value)
{
    size_t len;
    char *copy;

    if (value == NULL) {
        return NULL;
    }
    len = strlen(value);
    copy = heap_caps_malloc(len + 1, MALLOC_CAP_8BIT);
    if (copy == NULL) {
        return NULL;
    }
    memcpy(copy, value, len + 1);
    return copy;
}

bool esp32_mquickjs_is_headers_object(JSContext *ctx, JSValue value)
{
    return rr_is_object(ctx, value) && JS_GetClassID(ctx, value) == JS_CLASS_HEADERS;
}

bool esp32_mquickjs_is_request_object(JSContext *ctx, JSValue value)
{
    return rr_is_object(ctx, value) && JS_GetClassID(ctx, value) == JS_CLASS_REQUEST;
}

bool esp32_mquickjs_is_response_object(JSContext *ctx, JSValue value)
{
    return rr_is_object(ctx, value) && JS_GetClassID(ctx, value) == JS_CLASS_RESPONSE;
}

static char *rr_normalize_header_key(const char *value)
{
    size_t len;
    size_t i;
    char *normalized;

    if (value == NULL) {
        return NULL;
    }
    len = strlen(value);
    normalized = heap_caps_malloc(len + 1, MALLOC_CAP_8BIT);
    if (normalized == NULL) {
        return NULL;
    }
    for (i = 0; i < len; ++i) {
        normalized[i] = (char)tolower((unsigned char)value[i]);
    }
    normalized[len] = '\0';
    return normalized;
}

static char *rr_url_decode(const char *value)
{
    size_t len;
    size_t i;
    size_t j = 0;
    char *out;

    if (value == NULL) {
        return rr_strdup("");
    }

    len = strlen(value);
    out = heap_caps_malloc(len + 1, MALLOC_CAP_8BIT);
    if (out == NULL) {
        return NULL;
    }

    for (i = 0; i < len; ++i) {
        if (value[i] == '%' && i + 2 < len) {
            int hi = isdigit((unsigned char)value[i + 1]) ? value[i + 1] - '0'
                     : (tolower((unsigned char)value[i + 1]) >= 'a' && tolower((unsigned char)value[i + 1]) <= 'f')
                           ? tolower((unsigned char)value[i + 1]) - 'a' + 10
                           : -1;
            int lo = isdigit((unsigned char)value[i + 2]) ? value[i + 2] - '0'
                     : (tolower((unsigned char)value[i + 2]) >= 'a' && tolower((unsigned char)value[i + 2]) <= 'f')
                           ? tolower((unsigned char)value[i + 2]) - 'a' + 10
                           : -1;

            if (hi >= 0 && lo >= 0) {
                out[j++] = (char)((hi << 4) | lo);
                i += 2;
                continue;
            }
        }
        out[j++] = value[i] == '+' ? ' ' : value[i];
    }
    out[j] = '\0';
    return out;
}

static int rr_copy_object_properties(JSContext *ctx,
                                     JSValue source_obj,
                                     JSValue target_obj,
                                     bool normalize_keys)
{
    JSGCRef global_ref;
    JSGCRef object_ref;
    JSGCRef keys_ref;
    JSGCRef array_ref;
    JSGCRef length_ref;
    JSValue *global_obj;
    JSValue *object_ctor;
    JSValue *keys_fn;
    JSValue *keys_array;
    JSValue *length_value;
    int length = 0;
    int i;
    int result = -1;

    global_obj = JS_PushGCRef(ctx, &global_ref);
    object_ctor = JS_PushGCRef(ctx, &object_ref);
    keys_fn = JS_PushGCRef(ctx, &keys_ref);
    keys_array = JS_PushGCRef(ctx, &array_ref);
    length_value = JS_PushGCRef(ctx, &length_ref);
    *global_obj = JS_GetGlobalObject(ctx);
    *object_ctor = JS_UNDEFINED;
    *keys_fn = JS_UNDEFINED;
    *keys_array = JS_UNDEFINED;
    *length_value = JS_UNDEFINED;

    if (JS_IsException(*global_obj)) {
        goto done;
    }
    *object_ctor = JS_GetPropertyStr(ctx, *global_obj, "Object");
    if (JS_IsException(*object_ctor) || !JS_IsFunction(ctx, *object_ctor)) {
        goto done;
    }
    *keys_fn = JS_GetPropertyStr(ctx, *object_ctor, "keys");
    if (JS_IsException(*keys_fn) || !JS_IsFunction(ctx, *keys_fn)) {
        goto done;
    }
    *keys_array = rr_call_function(ctx, *keys_fn, *object_ctor, 1, &source_obj);
    if (JS_IsException(*keys_array)) {
        goto done;
    }
    *length_value = JS_GetPropertyStr(ctx, *keys_array, "length");
    if (JS_IsException(*length_value) || JS_ToInt32(ctx, &length, *length_value) != 0 || length < 0) {
        goto done;
    }

    for (i = 0; i < length; ++i) {
        JSGCRef key_ref;
        JSGCRef value_ref;
        JSValue *key_value;
        JSValue *value_value;
        JSCStringBuf key_buf;
        JSCStringBuf value_buf;
        const char *key_str;
        const char *value_str;
        char *prop_name = NULL;

        key_value = JS_PushGCRef(ctx, &key_ref);
        value_value = JS_PushGCRef(ctx, &value_ref);
        *key_value = JS_GetPropertyUint32(ctx, *keys_array, (uint32_t)i);
        *value_value = JS_UNDEFINED;
        if (JS_IsException(*key_value)) {
            JS_PopGCRef(ctx, &value_ref);
            JS_PopGCRef(ctx, &key_ref);
            goto done;
        }

        key_str = JS_ToCString(ctx, *key_value, &key_buf);
        if (key_str == NULL) {
            JS_PopGCRef(ctx, &value_ref);
            JS_PopGCRef(ctx, &key_ref);
            goto done;
        }
        prop_name = normalize_keys ? rr_normalize_header_key(key_str) : rr_strdup(key_str);
        if (prop_name == NULL) {
            JS_PopGCRef(ctx, &value_ref);
            JS_PopGCRef(ctx, &key_ref);
            JS_ThrowOutOfMemory(ctx);
            goto done;
        }

        *value_value = JS_GetPropertyStr(ctx, source_obj, key_str);
        if (JS_IsException(*value_value)) {
            heap_caps_free(prop_name);
            JS_PopGCRef(ctx, &value_ref);
            JS_PopGCRef(ctx, &key_ref);
            goto done;
        }
        if (JS_IsUndefined(*value_value)) {
            heap_caps_free(prop_name);
            JS_PopGCRef(ctx, &value_ref);
            JS_PopGCRef(ctx, &key_ref);
            continue;
        }

        value_str = JS_ToCString(ctx, *value_value, &value_buf);
        if (value_str == NULL) {
            heap_caps_free(prop_name);
            JS_PopGCRef(ctx, &value_ref);
            JS_PopGCRef(ctx, &key_ref);
            goto done;
        }

        if (!esp32_mquickjs_set_property(ctx, target_obj, prop_name, JS_NewString(ctx, value_str))) {
            heap_caps_free(prop_name);
            JS_PopGCRef(ctx, &value_ref);
            JS_PopGCRef(ctx, &key_ref);
            goto done;
        }
        heap_caps_free(prop_name);
        JS_PopGCRef(ctx, &value_ref);
        JS_PopGCRef(ctx, &key_ref);
    }

    result = 0;

done:
    JS_PopGCRef(ctx, &length_ref);
    JS_PopGCRef(ctx, &array_ref);
    JS_PopGCRef(ctx, &keys_ref);
    JS_PopGCRef(ctx, &object_ref);
    JS_PopGCRef(ctx, &global_ref);
    return result;
}

static JSValue rr_get_object_keys(JSContext *ctx, JSValue object_value)
{
    JSGCRef global_ref;
    JSGCRef object_ref;
    JSGCRef keys_ref;
    JSGCRef array_ref;
    JSValue *global_obj;
    JSValue *object_ctor;
    JSValue *keys_fn;
    JSValue *keys_array;

    global_obj = JS_PushGCRef(ctx, &global_ref);
    object_ctor = JS_PushGCRef(ctx, &object_ref);
    keys_fn = JS_PushGCRef(ctx, &keys_ref);
    keys_array = JS_PushGCRef(ctx, &array_ref);
    *global_obj = JS_GetGlobalObject(ctx);
    *object_ctor = JS_UNDEFINED;
    *keys_fn = JS_UNDEFINED;
    *keys_array = JS_UNDEFINED;

    if (JS_IsException(*global_obj)) {
        goto fail;
    }
    *object_ctor = JS_GetPropertyStr(ctx, *global_obj, "Object");
    if (JS_IsException(*object_ctor) || !JS_IsFunction(ctx, *object_ctor)) {
        goto fail;
    }
    *keys_fn = JS_GetPropertyStr(ctx, *object_ctor, "keys");
    if (JS_IsException(*keys_fn) || !JS_IsFunction(ctx, *keys_fn)) {
        goto fail;
    }
    *keys_array = rr_call_function(ctx, *keys_fn, *object_ctor, 1, &object_value);
    if (JS_IsException(*keys_array)) {
        goto fail;
    }

    JS_PopGCRef(ctx, &keys_ref);
    JS_PopGCRef(ctx, &object_ref);
    JS_PopGCRef(ctx, &global_ref);
    return JS_PopGCRef(ctx, &array_ref);

fail:
    JS_PopGCRef(ctx, &array_ref);
    JS_PopGCRef(ctx, &keys_ref);
    JS_PopGCRef(ctx, &object_ref);
    JS_PopGCRef(ctx, &global_ref);
    return JS_EXCEPTION;
}

static JSValue rr_get_headers_store(JSContext *ctx, JSValue headers_value)
{
    if (!esp32_mquickjs_is_headers_object(ctx, headers_value)) {
        return JS_EXCEPTION;
    }
    return JS_GetPropertyStr(ctx, headers_value, ESP32_MQUICKJS_HEADERS_STORE_KEY);
}

static JSValue rr_get_global_object(JSContext *ctx)
{
    return JS_GetGlobalObject(ctx);
}

JSValue esp32_mquickjs_headers_to_plain_object(JSContext *ctx, JSValue headers_value)
{
    JSGCRef plain_ref;
    JSValue *plain_obj;
    JSValue source_obj = JS_UNDEFINED;

    plain_obj = JS_PushGCRef(ctx, &plain_ref);
    *plain_obj = JS_NewObject(ctx);
    if (JS_IsException(*plain_obj)) {
        goto fail;
    }

    if (esp32_mquickjs_is_headers_object(ctx, headers_value)) {
        source_obj = rr_get_headers_store(ctx, headers_value);
        if (JS_IsException(source_obj)) {
            goto fail;
        }
    } else if (rr_is_object(ctx, headers_value)) {
        source_obj = headers_value;
    } else if (JS_IsUndefined(headers_value) || JS_IsNull(headers_value)) {
        return JS_PopGCRef(ctx, &plain_ref);
    } else {
        JS_ThrowTypeError(ctx, "Headers expects a plain object or Headers");
        goto fail;
    }

    if (rr_copy_object_properties(ctx, source_obj, *plain_obj, false) != 0) {
        goto fail;
    }

    return JS_PopGCRef(ctx, &plain_ref);

fail:
    JS_PopGCRef(ctx, &plain_ref);
    return JS_EXCEPTION;
}

JSValue esp32_mquickjs_make_headers_object(JSContext *ctx, JSValue global_obj, JSValue init_value)
{
    JSGCRef headers_ref;
    JSGCRef store_ref;
    JSValue *headers_obj;
    JSValue *store_obj;

    (void)global_obj;
    headers_obj = JS_PushGCRef(ctx, &headers_ref);
    store_obj = JS_PushGCRef(ctx, &store_ref);
    *headers_obj = JS_NewObjectClassUser(ctx, JS_CLASS_HEADERS);
    *store_obj = JS_NewObject(ctx);
    if (JS_IsException(*headers_obj) || JS_IsException(*store_obj)) {
        goto fail;
    }

    if (!esp32_mquickjs_set_property(ctx, *headers_obj, ESP32_MQUICKJS_HEADERS_STORE_KEY, *store_obj)) {
        goto fail;
    }
    *store_obj = JS_UNDEFINED;

    if (!JS_IsUndefined(init_value) && !JS_IsNull(init_value)) {
        JSValue plain_obj = esp32_mquickjs_headers_to_plain_object(ctx, init_value);

        if (JS_IsException(plain_obj)) {
            goto fail;
        }
        if (rr_copy_object_properties(ctx,
                                      plain_obj,
                                      JS_GetPropertyStr(ctx, *headers_obj, ESP32_MQUICKJS_HEADERS_STORE_KEY),
                                      true) != 0) {
            goto fail;
        }
    }

    JS_PopGCRef(ctx, &store_ref);
    return JS_PopGCRef(ctx, &headers_ref);

fail:
    JS_PopGCRef(ctx, &store_ref);
    JS_PopGCRef(ctx, &headers_ref);
    return JS_EXCEPTION;
}

static JSValue rr_make_query_object(JSContext *ctx, const char *query_string)
{
    JSGCRef query_ref;
    JSValue *query_obj;
    const char *cursor;

    query_obj = JS_PushGCRef(ctx, &query_ref);
    *query_obj = JS_NewObject(ctx);
    if (JS_IsException(*query_obj)) {
        goto fail;
    }

    if (query_string == NULL || query_string[0] == '\0') {
        return JS_PopGCRef(ctx, &query_ref);
    }

    cursor = query_string;
    while (*cursor != '\0') {
        const char *segment_start = cursor;
        const char *separator;
        const char *equals;
        size_t key_len;
        size_t value_len;
        char *key_raw;
        char *value_raw;
        char *key;
        char *value;

        while (*cursor != '\0' && *cursor != '&') {
            cursor++;
        }
        separator = cursor;
        if (*cursor == '&') {
            cursor++;
        }

        equals = memchr(segment_start, '=', (size_t)(separator - segment_start));
        key_len = equals != NULL ? (size_t)(equals - segment_start) : (size_t)(separator - segment_start);
        value_len = equals != NULL ? (size_t)(separator - equals - 1) : 0;

        key_raw = heap_caps_malloc(key_len + 1, MALLOC_CAP_8BIT);
        value_raw = heap_caps_malloc(value_len + 1, MALLOC_CAP_8BIT);
        if (key_raw == NULL || value_raw == NULL) {
            heap_caps_free(key_raw);
            heap_caps_free(value_raw);
            JS_ThrowOutOfMemory(ctx);
            goto fail;
        }

        memcpy(key_raw, segment_start, key_len);
        key_raw[key_len] = '\0';
        memcpy(value_raw, equals != NULL ? equals + 1 : "", value_len);
        value_raw[value_len] = '\0';
        key = rr_url_decode(key_raw);
        value = rr_url_decode(value_raw);
        heap_caps_free(key_raw);
        heap_caps_free(value_raw);
        if (key == NULL || value == NULL) {
            heap_caps_free(key);
            heap_caps_free(value);
            JS_ThrowOutOfMemory(ctx);
            goto fail;
        }
        if (key[0] != '\0' &&
            !esp32_mquickjs_set_property(ctx, *query_obj, key, JS_NewString(ctx, value))) {
            heap_caps_free(key);
            heap_caps_free(value);
            goto fail;
        }
        heap_caps_free(key);
        heap_caps_free(value);
    }

    return JS_PopGCRef(ctx, &query_ref);

fail:
    JS_PopGCRef(ctx, &query_ref);
    return JS_EXCEPTION;
}

static int rr_parse_url_parts(JSContext *ctx,
                              const char *url,
                              char **out_path,
                              char **out_query_string,
                              JSValue *out_query)
{
    const char *path_start = NULL;
    const char *query_start = NULL;
    size_t path_len;
    char *path = NULL;
    char *query_string = NULL;

    *out_path = NULL;
    *out_query_string = NULL;
    *out_query = JS_UNDEFINED;

    if (url == NULL) {
        return -1;
    }

    if (strstr(url, "://") != NULL) {
        const char *after_scheme = strstr(url, "://") + 3;
        path_start = strchr(after_scheme, '/');
    } else {
        path_start = url[0] == '/' ? url : NULL;
    }

    if (path_start == NULL) {
        path = rr_strdup("/");
        if (path == NULL) {
            return -1;
        }
        *out_path = path;
        *out_query_string = rr_strdup("");
        if (*out_query_string == NULL) {
            heap_caps_free(path);
            *out_path = NULL;
            return -1;
        }
        return 0;
    }

    query_start = strchr(path_start, '?');
    path_len = query_start != NULL ? (size_t)(query_start - path_start) : strlen(path_start);
    path = heap_caps_malloc(path_len + 1, MALLOC_CAP_8BIT);
    if (path == NULL) {
        return -1;
    }
    memcpy(path, path_start, path_len);
    path[path_len] = '\0';
    query_string = rr_strdup(query_start != NULL ? query_start + 1 : "");
    if (query_string == NULL) {
        heap_caps_free(path);
        return -1;
    }

    *out_path = path;
    *out_query_string = query_string;
    *out_query = rr_make_query_object(ctx, query_string);
    if (JS_IsException(*out_query)) {
        heap_caps_free(path);
        heap_caps_free(query_string);
        *out_path = NULL;
        *out_query_string = NULL;
        return -1;
    }
    return 0;
}

JSValue esp32_mquickjs_make_text_body_stream(JSContext *ctx,
                                             JSValue global_obj,
                                             const char *text)
{
    size_t text_len = text != NULL ? strlen(text) : 0;
    char *copy = heap_caps_malloc(text_len + 1, MALLOC_CAP_8BIT);

    if (copy == NULL) {
        return JS_ThrowOutOfMemory(ctx);
    }
    if (text_len > 0) {
        memcpy(copy, text, text_len);
    }
    copy[text_len] = '\0';
    return esp32_mquickjs_stream_open_memory_owned(ctx, global_obj, copy, text_len);
}

static JSValue rr_body_to_stream(JSContext *ctx,
                                 JSValue global_obj,
                                 JSValue body_value,
                                 const char *api_name)
{
    JSCStringBuf body_buf;
    const char *body_str;

    if (JS_IsUndefined(body_value) || JS_IsNull(body_value)) {
        return esp32_mquickjs_make_text_body_stream(ctx, global_obj, "");
    }
    if (esp32_mquickjs_stream_is_stream(ctx, body_value)) {
        return esp32_mquickjs_stream_clone(ctx, global_obj, body_value);
    }
    if (!JS_IsString(ctx, body_value)) {
        return JS_ThrowTypeError(ctx, "%s expects body to be a string or Stream", api_name);
    }
    body_str = JS_ToCString(ctx, body_value, &body_buf);
    if (body_str == NULL) {
        return JS_EXCEPTION;
    }
    return esp32_mquickjs_make_text_body_stream(ctx, global_obj, body_str);
}

JSValue esp32_mquickjs_make_request_object(JSContext *ctx,
                                           JSValue global_obj,
                                           const char *method,
                                           const char *url,
                                           const char *path,
                                           const char *route,
                                           const char *mount_path,
                                           const char *relative_path,
                                           const char *query_string,
                                           JSValue query_value,
                                           JSValue headers_init,
                                           JSValue body_stream)
{
    JSGCRef request_ref;
    JSGCRef query_ref;
    JSGCRef headers_ref;
    JSGCRef body_ref;
    JSValue *request_obj;
    JSValue *query_obj;
    JSValue *headers_obj;
    JSValue *body_obj;

    request_obj = JS_PushGCRef(ctx, &request_ref);
    query_obj = JS_PushGCRef(ctx, &query_ref);
    headers_obj = JS_PushGCRef(ctx, &headers_ref);
    body_obj = JS_PushGCRef(ctx, &body_ref);
    *request_obj = JS_NewObjectClassUser(ctx, JS_CLASS_REQUEST);
    *query_obj = JS_UNDEFINED;
    *headers_obj = JS_UNDEFINED;
    *body_obj = JS_UNDEFINED;
    if (JS_IsException(*request_obj)) {
        goto fail;
    }

    if (JS_IsUndefined(query_value) || JS_IsNull(query_value)) {
        *query_obj = rr_make_query_object(ctx, query_string != NULL ? query_string : "");
    } else {
        *query_obj = query_value;
    }
    if (JS_IsException(*query_obj)) {
        goto fail;
    }

    *headers_obj = esp32_mquickjs_make_headers_object(ctx, global_obj, headers_init);
    if (JS_IsException(*headers_obj)) {
        goto fail;
    }

    *body_obj = rr_body_to_stream(ctx, global_obj, body_stream, "Request");
    if (JS_IsException(*body_obj)) {
        goto fail;
    }

    if (!esp32_mquickjs_set_property(ctx, *request_obj, "method", JS_NewString(ctx, method != NULL ? method : "GET")) ||
        !esp32_mquickjs_set_property(ctx, *request_obj, "url", JS_NewString(ctx, url != NULL ? url : "")) ||
        !esp32_mquickjs_set_property(ctx, *request_obj, "path", JS_NewString(ctx, path != NULL ? path : "/")) ||
        !esp32_mquickjs_set_property(ctx, *request_obj, "route",
                                     JS_NewString(ctx, route != NULL ? route : (path != NULL ? path : "/"))) ||
        !esp32_mquickjs_set_property(ctx, *request_obj, "mountPath",
                                     JS_NewString(ctx, mount_path != NULL ? mount_path : "")) ||
        !esp32_mquickjs_set_property(ctx, *request_obj, "relativePath",
                                     JS_NewString(ctx, relative_path != NULL ? relative_path : "")) ||
        !esp32_mquickjs_set_property(ctx, *request_obj, "queryString",
                                     JS_NewString(ctx, query_string != NULL ? query_string : "")) ||
        !esp32_mquickjs_set_property(ctx, *request_obj, "query", *query_obj) ||
        !esp32_mquickjs_set_property(ctx, *request_obj, "headers", *headers_obj) ||
        !esp32_mquickjs_set_property(ctx, *request_obj, "body", *body_obj)) {
        goto fail;
    }
    *query_obj = JS_UNDEFINED;
    *headers_obj = JS_UNDEFINED;
    *body_obj = JS_UNDEFINED;

    JS_PopGCRef(ctx, &body_ref);
    JS_PopGCRef(ctx, &headers_ref);
    JS_PopGCRef(ctx, &query_ref);
    return JS_PopGCRef(ctx, &request_ref);

fail:
    JS_PopGCRef(ctx, &body_ref);
    JS_PopGCRef(ctx, &headers_ref);
    JS_PopGCRef(ctx, &query_ref);
    JS_PopGCRef(ctx, &request_ref);
    return JS_EXCEPTION;
}

static const char *rr_status_text(int32_t status)
{
    switch (status) {
    case 200:
        return "OK";
    case 201:
        return "Created";
    case 202:
        return "Accepted";
    case 204:
        return "No Content";
    case 301:
        return "Moved Permanently";
    case 302:
        return "Found";
    case 304:
        return "Not Modified";
    case 307:
        return "Temporary Redirect";
    case 308:
        return "Permanent Redirect";
    case 400:
        return "Bad Request";
    case 401:
        return "Unauthorized";
    case 403:
        return "Forbidden";
    case 404:
        return "Not Found";
    case 405:
        return "Method Not Allowed";
    case 408:
        return "Request Timeout";
    case 409:
        return "Conflict";
    case 413:
        return "Payload Too Large";
    case 429:
        return "Too Many Requests";
    case 500:
        return "Internal Server Error";
    case 502:
        return "Bad Gateway";
    case 503:
        return "Service Unavailable";
    case 504:
        return "Gateway Timeout";
    default:
        return "";
    }
}

JSValue esp32_mquickjs_make_response_object(JSContext *ctx,
                                            JSValue global_obj,
                                            int32_t status,
                                            const char *status_text,
                                            const char *url,
                                            JSValue headers_init,
                                            JSValue body_stream)
{
    JSGCRef response_ref;
    JSGCRef headers_ref;
    JSGCRef body_ref;
    JSValue *response_obj;
    JSValue *headers_obj;
    JSValue *body_obj;
    bool ok = status >= 200 && status < 300;

    response_obj = JS_PushGCRef(ctx, &response_ref);
    headers_obj = JS_PushGCRef(ctx, &headers_ref);
    body_obj = JS_PushGCRef(ctx, &body_ref);
    *response_obj = JS_NewObjectClassUser(ctx, JS_CLASS_RESPONSE);
    *headers_obj = JS_UNDEFINED;
    *body_obj = JS_UNDEFINED;
    if (JS_IsException(*response_obj)) {
        goto fail;
    }

    *headers_obj = esp32_mquickjs_make_headers_object(ctx, global_obj, headers_init);
    if (JS_IsException(*headers_obj)) {
        goto fail;
    }
    *body_obj = rr_body_to_stream(ctx, global_obj, body_stream, "Response");
    if (JS_IsException(*body_obj)) {
        goto fail;
    }

    if (!esp32_mquickjs_set_property(ctx, *response_obj, "ok", JS_NewBool(ok)) ||
        !esp32_mquickjs_set_property(ctx, *response_obj, "status", JS_NewInt32(ctx, status)) ||
        !esp32_mquickjs_set_property(ctx, *response_obj, "statusText",
                                     JS_NewString(ctx, status_text != NULL ? status_text : rr_status_text(status))) ||
        !esp32_mquickjs_set_property(ctx, *response_obj, "url", JS_NewString(ctx, url != NULL ? url : "")) ||
        !esp32_mquickjs_set_property(ctx, *response_obj, "headers", *headers_obj) ||
        !esp32_mquickjs_set_property(ctx, *response_obj, "body", *body_obj)) {
        goto fail;
    }
    *headers_obj = JS_UNDEFINED;
    *body_obj = JS_UNDEFINED;

    JS_PopGCRef(ctx, &body_ref);
    JS_PopGCRef(ctx, &headers_ref);
    return JS_PopGCRef(ctx, &response_ref);

fail:
    JS_PopGCRef(ctx, &body_ref);
    JS_PopGCRef(ctx, &headers_ref);
    JS_PopGCRef(ctx, &response_ref);
    return JS_EXCEPTION;
}

static JSValue rr_json_parse(JSContext *ctx, JSValue text_value)
{
    JSGCRef global_ref;
    JSGCRef json_ref;
    JSGCRef parse_ref;
    JSValue *global_obj;
    JSValue *json_obj;
    JSValue *parse_fn;
    JSValue argv[1];
    JSValue result = JS_EXCEPTION;

    global_obj = JS_PushGCRef(ctx, &global_ref);
    json_obj = JS_PushGCRef(ctx, &json_ref);
    parse_fn = JS_PushGCRef(ctx, &parse_ref);
    *global_obj = JS_GetGlobalObject(ctx);
    *json_obj = JS_UNDEFINED;
    *parse_fn = JS_UNDEFINED;
    if (JS_IsException(*global_obj)) {
        goto done;
    }
    *json_obj = JS_GetPropertyStr(ctx, *global_obj, "JSON");
    if (JS_IsException(*json_obj) || !rr_is_object(ctx, *json_obj)) {
        goto done;
    }
    *parse_fn = JS_GetPropertyStr(ctx, *json_obj, "parse");
    if (JS_IsException(*parse_fn) || !JS_IsFunction(ctx, *parse_fn)) {
        goto done;
    }
    argv[0] = text_value;
    result = rr_call_function(ctx, *parse_fn, *json_obj, 1, argv);

done:
    JS_PopGCRef(ctx, &parse_ref);
    JS_PopGCRef(ctx, &json_ref);
    JS_PopGCRef(ctx, &global_ref);
    return result;
}

static JSValue rr_json_stringify(JSContext *ctx, JSValue value)
{
    JSGCRef global_ref;
    JSGCRef json_ref;
    JSGCRef stringify_ref;
    JSValue *global_obj;
    JSValue *json_obj;
    JSValue *stringify_fn;
    JSValue argv[1];
    JSValue result = JS_EXCEPTION;

    global_obj = JS_PushGCRef(ctx, &global_ref);
    json_obj = JS_PushGCRef(ctx, &json_ref);
    stringify_fn = JS_PushGCRef(ctx, &stringify_ref);
    *global_obj = JS_GetGlobalObject(ctx);
    *json_obj = JS_UNDEFINED;
    *stringify_fn = JS_UNDEFINED;
    if (JS_IsException(*global_obj)) {
        goto done;
    }
    *json_obj = JS_GetPropertyStr(ctx, *global_obj, "JSON");
    if (JS_IsException(*json_obj) || !rr_is_object(ctx, *json_obj)) {
        goto done;
    }
    *stringify_fn = JS_GetPropertyStr(ctx, *json_obj, "stringify");
    if (JS_IsException(*stringify_fn) || !JS_IsFunction(ctx, *stringify_fn)) {
        goto done;
    }
    argv[0] = value;
    result = rr_call_function(ctx, *stringify_fn, *json_obj, 1, argv);

done:
    JS_PopGCRef(ctx, &stringify_ref);
    JS_PopGCRef(ctx, &json_ref);
    JS_PopGCRef(ctx, &global_ref);
    return result;
}

static JSValue rr_make_text_result(JSContext *ctx, JSValue target_value, const char *api_name)
{
    JSGCRef body_ref;
    JSValue *body_value;
    char *text = NULL;
    size_t text_len = 0;
    JSValue result = JS_EXCEPTION;

    body_value = JS_PushGCRef(ctx, &body_ref);
    *body_value = JS_GetPropertyStr(ctx, target_value, "body");
    if (JS_IsException(*body_value)) {
        goto done;
    }
    if (JS_IsUndefined(*body_value) || JS_IsNull(*body_value)) {
        result = JS_NewString(ctx, "");
        goto done;
    }
    if (esp32_mquickjs_stream_read_all_text(ctx, *body_value, api_name, &text, &text_len) != 0) {
        goto done;
    }
    result = JS_NewStringLen(ctx, text, text_len);
    if (!JS_IsException(result)) {
        esp32_mquickjs_stream_close_value(ctx, *body_value);
    }

done:
    heap_caps_free(text);
    JS_PopGCRef(ctx, &body_ref);
    return result;
}

static JSValue rr_make_request_from_args(JSContext *ctx, JSValue global_obj, int argc, JSValue *argv)
{
    JSGCRef init_ref;
    JSGCRef method_ref;
    JSGCRef headers_ref;
    JSGCRef body_ref;
    JSValue *init_value;
    JSValue *method_value;
    JSValue *headers_value;
    JSValue *body_value;
    char *method = NULL;
    char *url = NULL;
    char *path = NULL;
    char *route = NULL;
    char *mount_path = NULL;
    char *relative_path = NULL;
    char *query_string = NULL;
    JSValue query_value = JS_UNDEFINED;
    JSValue result = JS_EXCEPTION;

    init_value = JS_PushGCRef(ctx, &init_ref);
    method_value = JS_PushGCRef(ctx, &method_ref);
    headers_value = JS_PushGCRef(ctx, &headers_ref);
    body_value = JS_PushGCRef(ctx, &body_ref);
    *init_value = argc >= 2 ? argv[1] : JS_UNDEFINED;
    *method_value = JS_UNDEFINED;
    *headers_value = JS_UNDEFINED;
    *body_value = JS_UNDEFINED;

    if (argc < 1) {
        result = JS_ThrowTypeError(ctx, "Request(input, init?) expects a URL or Request");
        goto done;
    }

    if (esp32_mquickjs_is_request_object(ctx, argv[0])) {
        JSGCRef url_ref;
        JSGCRef path_ref;
        JSGCRef route_ref;
        JSGCRef mount_path_ref;
        JSGCRef relative_path_ref;
        JSGCRef query_string_ref;
        JSValue *url_value;
        JSValue *path_value;
        JSValue *route_value;
        JSValue *mount_path_value;
        JSValue *relative_path_value;
        JSValue *query_string_value;
        JSCStringBuf method_buf;
        JSCStringBuf url_buf;
        JSCStringBuf path_buf;
        JSCStringBuf route_buf;
        JSCStringBuf mount_path_buf;
        JSCStringBuf relative_path_buf;
        JSCStringBuf query_buf;
        const char *method_str;
        const char *url_str;
        const char *path_str;
        const char *route_str;
        const char *mount_path_str;
        const char *relative_path_str;
        const char *query_str;

        url_value = JS_PushGCRef(ctx, &url_ref);
        path_value = JS_PushGCRef(ctx, &path_ref);
        route_value = JS_PushGCRef(ctx, &route_ref);
        mount_path_value = JS_PushGCRef(ctx, &mount_path_ref);
        relative_path_value = JS_PushGCRef(ctx, &relative_path_ref);
        query_string_value = JS_PushGCRef(ctx, &query_string_ref);
        *method_value = JS_GetPropertyStr(ctx, argv[0], "method");
        *headers_value = JS_GetPropertyStr(ctx, argv[0], "headers");
        *body_value = JS_GetPropertyStr(ctx, argv[0], "body");
        *url_value = JS_GetPropertyStr(ctx, argv[0], "url");
        *path_value = JS_GetPropertyStr(ctx, argv[0], "path");
        *route_value = JS_GetPropertyStr(ctx, argv[0], "route");
        *mount_path_value = JS_GetPropertyStr(ctx, argv[0], "mountPath");
        *relative_path_value = JS_GetPropertyStr(ctx, argv[0], "relativePath");
        *query_string_value = JS_GetPropertyStr(ctx, argv[0], "queryString");
        query_value = JS_GetPropertyStr(ctx, argv[0], "query");
        if (JS_IsException(*method_value) || JS_IsException(*headers_value) || JS_IsException(*body_value) ||
            JS_IsException(*url_value) || JS_IsException(*path_value) || JS_IsException(*route_value) ||
            JS_IsException(*mount_path_value) || JS_IsException(*relative_path_value) ||
            JS_IsException(*query_string_value) ||
            JS_IsException(query_value)) {
            JS_PopGCRef(ctx, &query_string_ref);
            JS_PopGCRef(ctx, &relative_path_ref);
            JS_PopGCRef(ctx, &mount_path_ref);
            JS_PopGCRef(ctx, &route_ref);
            JS_PopGCRef(ctx, &path_ref);
            JS_PopGCRef(ctx, &url_ref);
            goto done;
        }
        method_str = JS_ToCString(ctx, *method_value, &method_buf);
        url_str = JS_ToCString(ctx, *url_value, &url_buf);
        path_str = JS_ToCString(ctx, *path_value, &path_buf);
        route_str = JS_ToCString(ctx, *route_value, &route_buf);
        mount_path_str = JS_ToCString(ctx, *mount_path_value, &mount_path_buf);
        relative_path_str = JS_ToCString(ctx, *relative_path_value, &relative_path_buf);
        query_str = JS_ToCString(ctx, *query_string_value, &query_buf);
        method = rr_strdup(method_str != NULL ? method_str : "GET");
        url = rr_strdup(url_str != NULL ? url_str : "");
        path = rr_strdup(path_str != NULL ? path_str : "/");
        route = rr_strdup(route_str != NULL ? route_str : (path_str != NULL ? path_str : "/"));
        mount_path = rr_strdup(mount_path_str != NULL ? mount_path_str : "");
        relative_path = rr_strdup(relative_path_str != NULL ? relative_path_str : "");
        query_string = rr_strdup(query_str != NULL ? query_str : "");
        JS_PopGCRef(ctx, &query_string_ref);
        JS_PopGCRef(ctx, &relative_path_ref);
        JS_PopGCRef(ctx, &mount_path_ref);
        JS_PopGCRef(ctx, &route_ref);
        JS_PopGCRef(ctx, &path_ref);
        JS_PopGCRef(ctx, &url_ref);
    } else if (JS_IsString(ctx, argv[0])) {
        JSCStringBuf url_buf;
        const char *url_str = JS_ToCString(ctx, argv[0], &url_buf);

        if (url_str == NULL) {
            goto done;
        }
        url = rr_strdup(url_str);
        method = rr_strdup("GET");
        route = NULL;
        mount_path = rr_strdup("");
        relative_path = rr_strdup("");
        if (url == NULL || method == NULL || rr_parse_url_parts(ctx, url_str, &path, &query_string, &query_value) != 0) {
            JS_ThrowOutOfMemory(ctx);
            goto done;
        }
    } else {
        result = JS_ThrowTypeError(ctx, "Request(input, init?) expects a URL string or Request");
        goto done;
    }

    if (argc >= 2 && !JS_IsUndefined(*init_value) && !JS_IsNull(*init_value)) {
        JSGCRef method_init_ref;
        JSGCRef headers_init_ref;
        JSGCRef body_init_ref;
        JSValue *method_init;
        JSValue *headers_init;
        JSValue *body_init;

        if (!rr_is_object(ctx, *init_value)) {
            result = JS_ThrowTypeError(ctx, "Request(input, init) expects init to be an object");
            goto done;
        }
        method_init = JS_PushGCRef(ctx, &method_init_ref);
        headers_init = JS_PushGCRef(ctx, &headers_init_ref);
        body_init = JS_PushGCRef(ctx, &body_init_ref);
        *method_init = JS_GetPropertyStr(ctx, *init_value, "method");
        *headers_init = JS_GetPropertyStr(ctx, *init_value, "headers");
        *body_init = JS_GetPropertyStr(ctx, *init_value, "body");
        if (JS_IsException(*method_init) || JS_IsException(*headers_init) || JS_IsException(*body_init)) {
            JS_PopGCRef(ctx, &body_init_ref);
            JS_PopGCRef(ctx, &headers_init_ref);
            JS_PopGCRef(ctx, &method_init_ref);
            goto done;
        }
        if (!JS_IsUndefined(*method_init) && !JS_IsNull(*method_init)) {
            JSCStringBuf method_buf;
            const char *method_str;

            if (!JS_IsString(ctx, *method_init)) {
                result = JS_ThrowTypeError(ctx, "Request(input, init.method) expects a string");
                JS_PopGCRef(ctx, &body_init_ref);
                JS_PopGCRef(ctx, &headers_init_ref);
                JS_PopGCRef(ctx, &method_init_ref);
                goto done;
            }
            method_str = JS_ToCString(ctx, *method_init, &method_buf);
            if (method_str == NULL) {
                JS_PopGCRef(ctx, &body_init_ref);
                JS_PopGCRef(ctx, &headers_init_ref);
                JS_PopGCRef(ctx, &method_init_ref);
                goto done;
            }
            heap_caps_free(method);
            method = rr_strdup(method_str);
        }
        if (!JS_IsUndefined(*headers_init) && !JS_IsNull(*headers_init)) {
            *headers_value = *headers_init;
        }
        if (!JS_IsUndefined(*body_init) && !JS_IsNull(*body_init)) {
            *body_value = *body_init;
        }
        JS_PopGCRef(ctx, &body_init_ref);
        JS_PopGCRef(ctx, &headers_init_ref);
        JS_PopGCRef(ctx, &method_init_ref);
    }

    result = esp32_mquickjs_make_request_object(ctx,
                                                global_obj,
                                                method != NULL ? method : "GET",
                                                url != NULL ? url : "",
                                                path != NULL ? path : "/",
                                                route != NULL ? route : (path != NULL ? path : "/"),
                                                mount_path != NULL ? mount_path : "",
                                                relative_path != NULL ? relative_path : "",
                                                query_string != NULL ? query_string : "",
                                                query_value,
                                                *headers_value,
                                                *body_value);

done:
    heap_caps_free(method);
    heap_caps_free(url);
    heap_caps_free(path);
    heap_caps_free(route);
    heap_caps_free(mount_path);
    heap_caps_free(relative_path);
    heap_caps_free(query_string);
    JS_PopGCRef(ctx, &body_ref);
    JS_PopGCRef(ctx, &headers_ref);
    JS_PopGCRef(ctx, &method_ref);
    JS_PopGCRef(ctx, &init_ref);
    return result;
}

static JSValue rr_make_response_from_args(JSContext *ctx, JSValue global_obj, int argc, JSValue *argv)
{
    JSGCRef init_ref;
    JSGCRef status_ref;
    JSGCRef status_text_ref;
    JSGCRef headers_ref;
    JSGCRef url_ref;
    JSValue *init_value;
    JSValue *status_value;
    JSValue *status_text_value;
    JSValue *headers_value;
    JSValue *url_value;
    int status = 200;
    char *status_text = NULL;
    char *url = NULL;
    JSValue result = JS_EXCEPTION;

    init_value = JS_PushGCRef(ctx, &init_ref);
    status_value = JS_PushGCRef(ctx, &status_ref);
    status_text_value = JS_PushGCRef(ctx, &status_text_ref);
    headers_value = JS_PushGCRef(ctx, &headers_ref);
    url_value = JS_PushGCRef(ctx, &url_ref);
    *init_value = argc >= 2 ? argv[1] : JS_UNDEFINED;
    *status_value = JS_UNDEFINED;
    *status_text_value = JS_UNDEFINED;
    *headers_value = JS_UNDEFINED;
    *url_value = JS_UNDEFINED;

    if (argc >= 2 && !JS_IsUndefined(*init_value) && !JS_IsNull(*init_value)) {
        JSCStringBuf text_buf;
        const char *text_str;

        if (!rr_is_object(ctx, *init_value)) {
            result = JS_ThrowTypeError(ctx, "Response(body, init) expects init to be an object");
            goto done;
        }
        *status_value = JS_GetPropertyStr(ctx, *init_value, "status");
        *status_text_value = JS_GetPropertyStr(ctx, *init_value, "statusText");
        *headers_value = JS_GetPropertyStr(ctx, *init_value, "headers");
        *url_value = JS_GetPropertyStr(ctx, *init_value, "url");
        if (JS_IsException(*status_value) || JS_IsException(*status_text_value) ||
            JS_IsException(*headers_value) || JS_IsException(*url_value)) {
            goto done;
        }
        if (!JS_IsUndefined(*status_value) && !JS_IsNull(*status_value) &&
            JS_ToInt32(ctx, &status, *status_value) != 0) {
            result = JS_ThrowTypeError(ctx, "Response(body, init.status) expects an integer");
            goto done;
        }
        if (!JS_IsUndefined(*status_text_value) && !JS_IsNull(*status_text_value)) {
            text_str = JS_ToCString(ctx, *status_text_value, &text_buf);
            if (text_str == NULL) {
                goto done;
            }
            status_text = rr_strdup(text_str);
        }
        if (!JS_IsUndefined(*url_value) && !JS_IsNull(*url_value)) {
            text_str = JS_ToCString(ctx, *url_value, &text_buf);
            if (text_str == NULL) {
                goto done;
            }
            url = rr_strdup(text_str);
        }
    }

    result = esp32_mquickjs_make_response_object(ctx,
                                                 global_obj,
                                                 status,
                                                 status_text,
                                                 url,
                                                 *headers_value,
                                                 argc >= 1 ? argv[0] : JS_UNDEFINED);

done:
    heap_caps_free(status_text);
    heap_caps_free(url);
    JS_PopGCRef(ctx, &url_ref);
    JS_PopGCRef(ctx, &headers_ref);
    JS_PopGCRef(ctx, &status_text_ref);
    JS_PopGCRef(ctx, &status_ref);
    JS_PopGCRef(ctx, &init_ref);
    return result;
}

static JSValue rr_make_response_text_factory(JSContext *ctx, JSValue global_obj, int argc, JSValue *argv)
{
    JSCStringBuf text_buf;
    const char *text = argc >= 1 ? JS_ToCString(ctx, argv[0], &text_buf) : "";
    JSValue body_stream;
    JSValue result;

    if (argc >= 1 && text == NULL) {
        return JS_EXCEPTION;
    }
    body_stream = esp32_mquickjs_make_text_body_stream(ctx, global_obj, text != NULL ? text : "");
    if (JS_IsException(body_stream)) {
        return JS_EXCEPTION;
    }
    result = rr_make_response_from_args(ctx, global_obj, argc >= 2 ? 2 : 1,
                                        (JSValue[]){ body_stream, argc >= 2 ? argv[1] : JS_UNDEFINED });
    return result;
}

static JSValue rr_make_response_json_factory(JSContext *ctx, JSValue global_obj, int argc, JSValue *argv)
{
    JSValue text_value;
    JSCStringBuf text_buf;
    const char *text;
    JSValue body_stream;
    JSValue result;

    text_value = rr_json_stringify(ctx, argc >= 1 ? argv[0] : JS_NULL);
    if (JS_IsException(text_value)) {
        return JS_EXCEPTION;
    }
    text = JS_ToCString(ctx, text_value, &text_buf);
    if (text == NULL) {
        return JS_EXCEPTION;
    }
    body_stream = esp32_mquickjs_make_text_body_stream(ctx, global_obj, text);
    if (JS_IsException(body_stream)) {
        return JS_EXCEPTION;
    }

    if (argc >= 2 && rr_is_object(ctx, argv[1])) {
        JSGCRef init_ref;
        JSValue *init_obj;

        init_obj = JS_PushGCRef(ctx, &init_ref);
        *init_obj = JS_NewObject(ctx);
        if (JS_IsException(*init_obj)) {
            JS_PopGCRef(ctx, &init_ref);
            return JS_EXCEPTION;
        }
        if (rr_copy_object_properties(ctx, argv[1], *init_obj, false) != 0 ||
            !esp32_mquickjs_set_property(ctx, *init_obj, "headers",
                                         esp32_mquickjs_make_headers_object(ctx, global_obj,
                                                                            JS_GetPropertyStr(ctx, argv[1], "headers")))) {
            JS_PopGCRef(ctx, &init_ref);
            return JS_EXCEPTION;
        }
        {
            JSValue plain_headers = JS_GetPropertyStr(ctx, *init_obj, "headers");
            JSValue headers_with_json = esp32_mquickjs_make_headers_object(ctx, global_obj, plain_headers);
            JSValue plain_obj = esp32_mquickjs_headers_to_plain_object(ctx, headers_with_json);

            if (JS_IsException(headers_with_json) || JS_IsException(plain_obj) ||
                !esp32_mquickjs_set_property(ctx, plain_obj, "content-type",
                                             JS_NewString(ctx, "application/json; charset=utf-8")) ||
                !esp32_mquickjs_set_property(ctx, *init_obj, "headers", plain_obj)) {
                JS_PopGCRef(ctx, &init_ref);
                return JS_EXCEPTION;
            }
        }
        result = rr_make_response_from_args(ctx, global_obj, 2, (JSValue[]){ body_stream, *init_obj });
        JS_PopGCRef(ctx, &init_ref);
        return result;
    }

    {
        JSValue headers = JS_NewObject(ctx);
        JSValue init_obj = JS_NewObject(ctx);

        if (JS_IsException(headers) || JS_IsException(init_obj) ||
            !esp32_mquickjs_set_property(ctx, headers, "content-type",
                                         JS_NewString(ctx, "application/json; charset=utf-8")) ||
            !esp32_mquickjs_set_property(ctx, init_obj, "headers", headers)) {
            return JS_EXCEPTION;
        }
        return rr_make_response_from_args(ctx, global_obj, 2, (JSValue[]){ body_stream, init_obj });
    }
}

static JSValue rr_make_response_stream_factory(JSContext *ctx, JSValue global_obj, int argc, JSValue *argv)
{
    if (argc < 1 || !esp32_mquickjs_stream_is_stream(ctx, argv[0])) {
        return JS_ThrowTypeError(ctx, "Response.stream(stream, options?) expects a Stream");
    }
    return rr_make_response_from_args(ctx, global_obj, argc >= 2 ? 2 : 1,
                                      (JSValue[]){ argv[0], argc >= 2 ? argv[1] : JS_UNDEFINED });
}

static JSValue rr_require_global_object(JSContext *ctx)
{
    return rr_get_global_object(ctx);
}

static JSValue rr_require_headers_this(JSContext *ctx, JSValue this_value, const char *api_name)
{
    if (!esp32_mquickjs_is_headers_object(ctx, this_value)) {
        return JS_ThrowTypeError(ctx, "%s expects a Headers object", api_name);
    }
    return this_value;
}

static JSValue rr_require_request_this(JSContext *ctx, JSValue this_value, const char *api_name)
{
    if (!esp32_mquickjs_is_request_object(ctx, this_value)) {
        return JS_ThrowTypeError(ctx, "%s expects a Request object", api_name);
    }
    return this_value;
}

static JSValue rr_require_response_this(JSContext *ctx, JSValue this_value, const char *api_name)
{
    if (!esp32_mquickjs_is_response_object(ctx, this_value)) {
        return JS_ThrowTypeError(ctx, "%s expects a Response object", api_name);
    }
    return this_value;
}

static int rr_require_constructor(int argc, const char *class_name)
{
    if (!(argc & FRAME_CF_CTOR)) {
        return -1;
    }
    (void)class_name;
    return argc & ~FRAME_CF_CTOR;
}

JSValue js_headers_constructor(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    JSGCRef global_ref;
    JSValue *global_obj;
    JSValue result;
    int actual_argc = rr_require_constructor(argc, "Headers");

    (void)this_val;
    if (actual_argc < 0) {
        return JS_ThrowTypeError(ctx, "Headers must be called with new");
    }
    global_obj = JS_PushGCRef(ctx, &global_ref);
    *global_obj = rr_require_global_object(ctx);
    if (JS_IsException(*global_obj)) {
        JS_PopGCRef(ctx, &global_ref);
        return JS_EXCEPTION;
    }
    result = esp32_mquickjs_make_headers_object(ctx, *global_obj, actual_argc >= 1 ? argv[0] : JS_UNDEFINED);
    JS_PopGCRef(ctx, &global_ref);
    return result;
}

JSValue js_headers_get(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    JSGCRef store_ref;
    JSValue *store_obj;
    JSCStringBuf key_buf;
    const char *key;
    char *normalized;
    JSValue value;

    if (JS_IsException(rr_require_headers_this(ctx, *this_val, "Headers.get(name)"))) {
        return JS_EXCEPTION;
    }
    if (argc < 1 || !JS_IsString(ctx, argv[0])) {
        return JS_ThrowTypeError(ctx, "Headers.get(name) expects a string");
    }
    key = JS_ToCString(ctx, argv[0], &key_buf);
    if (key == NULL) {
        return JS_EXCEPTION;
    }
    normalized = rr_normalize_header_key(key);
    if (normalized == NULL) {
        return JS_ThrowOutOfMemory(ctx);
    }
    store_obj = JS_PushGCRef(ctx, &store_ref);
    *store_obj = rr_get_headers_store(ctx, *this_val);
    value = JS_GetPropertyStr(ctx, *store_obj, normalized);
    heap_caps_free(normalized);
    JS_PopGCRef(ctx, &store_ref);
    return JS_IsUndefined(value) ? JS_NULL : value;
}

JSValue js_headers_set(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    JSGCRef store_ref;
    JSValue *store_obj;
    JSCStringBuf key_buf;
    JSCStringBuf value_buf;
    const char *key;
    const char *value;
    char *normalized;

    if (JS_IsException(rr_require_headers_this(ctx, *this_val, "Headers.set(name, value)"))) {
        return JS_EXCEPTION;
    }
    if (argc < 2 || !JS_IsString(ctx, argv[0])) {
        return JS_ThrowTypeError(ctx, "Headers.set(name, value) expects a name and value");
    }
    key = JS_ToCString(ctx, argv[0], &key_buf);
    value = JS_ToCString(ctx, argv[1], &value_buf);
    if (key == NULL || value == NULL) {
        return JS_EXCEPTION;
    }
    normalized = rr_normalize_header_key(key);
    if (normalized == NULL) {
        return JS_ThrowOutOfMemory(ctx);
    }
    store_obj = JS_PushGCRef(ctx, &store_ref);
    *store_obj = rr_get_headers_store(ctx, *this_val);
    if (!esp32_mquickjs_set_property(ctx, *store_obj, normalized, JS_NewString(ctx, value))) {
        heap_caps_free(normalized);
        JS_PopGCRef(ctx, &store_ref);
        return JS_EXCEPTION;
    }
    heap_caps_free(normalized);
    JS_PopGCRef(ctx, &store_ref);
    return *this_val;
}

JSValue js_headers_has(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    JSGCRef store_ref;
    JSValue *store_obj;
    JSCStringBuf key_buf;
    const char *key;
    char *normalized;
    JSValue value;

    if (JS_IsException(rr_require_headers_this(ctx, *this_val, "Headers.has(name)"))) {
        return JS_EXCEPTION;
    }
    if (argc < 1 || !JS_IsString(ctx, argv[0])) {
        return JS_ThrowTypeError(ctx, "Headers.has(name) expects a string");
    }
    key = JS_ToCString(ctx, argv[0], &key_buf);
    if (key == NULL) {
        return JS_EXCEPTION;
    }
    normalized = rr_normalize_header_key(key);
    if (normalized == NULL) {
        return JS_ThrowOutOfMemory(ctx);
    }
    store_obj = JS_PushGCRef(ctx, &store_ref);
    *store_obj = rr_get_headers_store(ctx, *this_val);
    value = JS_GetPropertyStr(ctx, *store_obj, normalized);
    heap_caps_free(normalized);
    JS_PopGCRef(ctx, &store_ref);
    return JS_NewBool(!JS_IsUndefined(value) && !JS_IsNull(value));
}

JSValue js_headers_delete(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    JSGCRef store_ref;
    JSValue *store_obj;
    JSCStringBuf key_buf;
    const char *key;
    char *normalized;
    JSValue previous;
    bool existed;

    if (JS_IsException(rr_require_headers_this(ctx, *this_val, "Headers.delete(name)"))) {
        return JS_EXCEPTION;
    }
    if (argc < 1 || !JS_IsString(ctx, argv[0])) {
        return JS_ThrowTypeError(ctx, "Headers.delete(name) expects a string");
    }
    key = JS_ToCString(ctx, argv[0], &key_buf);
    if (key == NULL) {
        return JS_EXCEPTION;
    }
    normalized = rr_normalize_header_key(key);
    if (normalized == NULL) {
        return JS_ThrowOutOfMemory(ctx);
    }
    store_obj = JS_PushGCRef(ctx, &store_ref);
    *store_obj = rr_get_headers_store(ctx, *this_val);
    previous = JS_GetPropertyStr(ctx, *store_obj, normalized);
    existed = !JS_IsException(previous) && !JS_IsUndefined(previous) && !JS_IsNull(previous);
    if (JS_IsException(previous) ||
        !esp32_mquickjs_set_property(ctx, *store_obj, normalized, JS_UNDEFINED)) {
        heap_caps_free(normalized);
        JS_PopGCRef(ctx, &store_ref);
        return JS_EXCEPTION;
    }
    heap_caps_free(normalized);
    JS_PopGCRef(ctx, &store_ref);
    return JS_NewBool(existed);
}

JSValue js_headers_entries(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    JSValue plain_obj;
    JSGCRef array_ref;
    JSValue *entries_array;
    JSGCRef keys_ref;
    JSGCRef length_ref;
    JSValue *keys_array;
    JSValue *length_value;
    int length = 0;
    int i;

    (void)argc;
    (void)argv;
    if (JS_IsException(rr_require_headers_this(ctx, *this_val, "Headers.entries()"))) {
        return JS_EXCEPTION;
    }
    plain_obj = esp32_mquickjs_headers_to_plain_object(ctx, *this_val);
    if (JS_IsException(plain_obj)) {
        return JS_EXCEPTION;
    }
    entries_array = JS_PushGCRef(ctx, &array_ref);
    keys_array = JS_PushGCRef(ctx, &keys_ref);
    length_value = JS_PushGCRef(ctx, &length_ref);
    *entries_array = JS_NewArray(ctx, 0);
    *keys_array = rr_get_object_keys(ctx, plain_obj);
    *length_value = JS_UNDEFINED;
    *length_value = JS_GetPropertyStr(ctx, *keys_array, "length");
    if (JS_IsException(*entries_array) || JS_IsException(*keys_array) || JS_IsException(*length_value) ||
        JS_ToInt32(ctx, &length, *length_value) != 0) {
        JS_PopGCRef(ctx, &length_ref);
        JS_PopGCRef(ctx, &keys_ref);
        JS_PopGCRef(ctx, &array_ref);
        return JS_EXCEPTION;
    }
    for (i = 0; i < length; ++i) {
        JSValue pair = JS_NewArray(ctx, 0);
        JSValue key = JS_GetPropertyUint32(ctx, *keys_array, (uint32_t)i);
        JSCStringBuf key_buf;
        const char *key_str = JS_ToCString(ctx, key, &key_buf);
        JSValue value = key_str != NULL ? JS_GetPropertyStr(ctx, plain_obj, key_str) : JS_EXCEPTION;

        if (JS_IsException(pair) || JS_IsException(key) || key_str == NULL || JS_IsException(value) ||
            JS_SetPropertyUint32(ctx, pair, 0, key) == JS_EXCEPTION ||
            JS_SetPropertyUint32(ctx, pair, 1, value) == JS_EXCEPTION ||
            JS_SetPropertyUint32(ctx, *entries_array, (uint32_t)i, pair) == JS_EXCEPTION) {
            JS_PopGCRef(ctx, &length_ref);
            JS_PopGCRef(ctx, &keys_ref);
            JS_PopGCRef(ctx, &array_ref);
            return JS_EXCEPTION;
        }
    }
    JS_PopGCRef(ctx, &length_ref);
    JS_PopGCRef(ctx, &keys_ref);
    return JS_PopGCRef(ctx, &array_ref);
}

JSValue js_headers_toObject(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)argc;
    (void)argv;
    if (JS_IsException(rr_require_headers_this(ctx, *this_val, "Headers.toObject()"))) {
        return JS_EXCEPTION;
    }
    return esp32_mquickjs_headers_to_plain_object(ctx, *this_val);
}

JSValue js_request_constructor(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    JSGCRef global_ref;
    JSValue *global_obj;
    JSValue result;
    int actual_argc = rr_require_constructor(argc, "Request");

    (void)this_val;
    if (actual_argc < 0) {
        return JS_ThrowTypeError(ctx, "Request must be called with new");
    }
    global_obj = JS_PushGCRef(ctx, &global_ref);
    *global_obj = rr_require_global_object(ctx);
    if (JS_IsException(*global_obj)) {
        JS_PopGCRef(ctx, &global_ref);
        return JS_EXCEPTION;
    }
    result = rr_make_request_from_args(ctx, *global_obj, actual_argc, argv);
    JS_PopGCRef(ctx, &global_ref);
    return result;
}

JSValue js_request_text(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)argc;
    (void)argv;
    if (JS_IsException(rr_require_request_this(ctx, *this_val, "Request.text()"))) {
        return JS_EXCEPTION;
    }
    return rr_make_text_result(ctx, *this_val, "Request.text()");
}

JSValue js_request_json(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    JSValue text_value;

    (void)argc;
    (void)argv;
    if (JS_IsException(rr_require_request_this(ctx, *this_val, "Request.json()"))) {
        return JS_EXCEPTION;
    }
    text_value = rr_make_text_result(ctx, *this_val, "Request.json()");
    if (JS_IsException(text_value)) {
        return JS_EXCEPTION;
    }
    return rr_json_parse(ctx, text_value);
}

JSValue js_response_constructor(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    JSGCRef global_ref;
    JSValue *global_obj;
    JSValue result;
    int actual_argc = rr_require_constructor(argc, "Response");

    (void)this_val;
    if (actual_argc < 0) {
        return JS_ThrowTypeError(ctx, "Response must be called with new");
    }
    global_obj = JS_PushGCRef(ctx, &global_ref);
    *global_obj = rr_require_global_object(ctx);
    if (JS_IsException(*global_obj)) {
        JS_PopGCRef(ctx, &global_ref);
        return JS_EXCEPTION;
    }
    result = rr_make_response_from_args(ctx, *global_obj, actual_argc, argv);
    JS_PopGCRef(ctx, &global_ref);
    return result;
}

JSValue js_response_text(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)argc;
    (void)argv;
    if (JS_IsException(rr_require_response_this(ctx, *this_val, "Response.text()"))) {
        return JS_EXCEPTION;
    }
    return rr_make_text_result(ctx, *this_val, "Response.text()");
}

JSValue js_response_json(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    JSValue text_value;

    (void)argc;
    (void)argv;
    if (JS_IsException(rr_require_response_this(ctx, *this_val, "Response.json()"))) {
        return JS_EXCEPTION;
    }
    text_value = rr_make_text_result(ctx, *this_val, "Response.json()");
    if (JS_IsException(text_value)) {
        return JS_EXCEPTION;
    }
    return rr_json_parse(ctx, text_value);
}

JSValue js_response_make_text(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    JSGCRef global_ref;
    JSValue *global_obj;
    JSValue result;

    (void)this_val;
    global_obj = JS_PushGCRef(ctx, &global_ref);
    *global_obj = rr_require_global_object(ctx);
    if (JS_IsException(*global_obj)) {
        JS_PopGCRef(ctx, &global_ref);
        return JS_EXCEPTION;
    }
    result = rr_make_response_text_factory(ctx, *global_obj, argc, argv);
    JS_PopGCRef(ctx, &global_ref);
    return result;
}

JSValue js_response_make_json(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    JSGCRef global_ref;
    JSValue *global_obj;
    JSValue result;

    (void)this_val;
    global_obj = JS_PushGCRef(ctx, &global_ref);
    *global_obj = rr_require_global_object(ctx);
    if (JS_IsException(*global_obj)) {
        JS_PopGCRef(ctx, &global_ref);
        return JS_EXCEPTION;
    }
    result = rr_make_response_json_factory(ctx, *global_obj, argc, argv);
    JS_PopGCRef(ctx, &global_ref);
    return result;
}

JSValue js_response_make_stream(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    JSGCRef global_ref;
    JSValue *global_obj;
    JSValue result;

    (void)this_val;
    global_obj = JS_PushGCRef(ctx, &global_ref);
    *global_obj = rr_require_global_object(ctx);
    if (JS_IsException(*global_obj)) {
        JS_PopGCRef(ctx, &global_ref);
        return JS_EXCEPTION;
    }
    result = rr_make_response_stream_factory(ctx, *global_obj, argc, argv);
    JS_PopGCRef(ctx, &global_ref);
    return result;
}
