#include <math.h>
#include <stdio.h>
#include <string.h>

#include "vendor/mquickjs/mquickjs_build.h"

#define JS_CLASS_HEADERS (JS_CLASS_USER + 0)
#define JS_CLASS_REQUEST (JS_CLASS_USER + 1)
#define JS_CLASS_RESPONSE (JS_CLASS_USER + 2)
#define JS_CLASS_COUNT (JS_CLASS_USER + 3)

#define js_global_object js_global_object_base
#define js_c_function_decl js_c_function_decl_base
#define main mqjs_stdlib_base_main
#include "vendor/mquickjs/mqjs_stdlib.c"
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

static const JSPropDef js_global_object_extra[] = {
    JS_PROP_CLASS_DEF("Headers", &js_headers_class),
    JS_PROP_CLASS_DEF("Request", &js_request_class),
    JS_PROP_CLASS_DEF("Response", &js_response_class),
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
