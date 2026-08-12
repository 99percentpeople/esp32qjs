#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mquickjs.h"

#define CHECKER_STUB(name)                                                         \
    static JSValue name(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv) \
    {                                                                               \
        (void)ctx;                                                                  \
        (void)this_val;                                                             \
        (void)argc;                                                                 \
        (void)argv;                                                                 \
        return JS_UNDEFINED;                                                        \
    }

/* The generic generated stdlib expects these CLI-only functions. Parsing never runs them. */
CHECKER_STUB(js_print)
CHECKER_STUB(js_gc)
CHECKER_STUB(js_load)
CHECKER_STUB(js_setTimeout)
CHECKER_STUB(js_clearTimeout)
CHECKER_STUB(js_date_constructor)

static JSValue js_date_now(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_NewInt64(ctx, 0);
}

static JSValue js_performance_now(JSContext *ctx, JSValue *this_val, int argc, JSValue *argv)
{
    (void)this_val;
    (void)argc;
    (void)argv;
    return JS_NewInt64(ctx, 0);
}

#include "mqjs_stdlib.h"

#define CHECKER_HEAP_SIZE (16U * 1024U * 1024U)

static void checker_log(void *opaque, const void *buf, size_t buf_len)
{
    (void)opaque;
    fwrite(buf, 1, buf_len, stderr);
}

static char *checker_load_source(const char *path, size_t *out_len)
{
    FILE *file;
    long file_len;
    char *source;

    file = fopen(path, "rb");
    if (file == NULL) {
        fprintf(stderr, "%s: %s\n", path, strerror(errno));
        return NULL;
    }
    if (fseek(file, 0, SEEK_END) != 0 ||
        (file_len = ftell(file)) < 0 ||
        fseek(file, 0, SEEK_SET) != 0) {
        fprintf(stderr, "%s: failed to determine file size\n", path);
        fclose(file);
        return NULL;
    }

    source = malloc((size_t)file_len + 1);
    if (source == NULL) {
        fprintf(stderr, "%s: out of memory\n", path);
        fclose(file);
        return NULL;
    }
    if ((size_t)file_len > 0 &&
        fread(source, 1, (size_t)file_len, file) != (size_t)file_len) {
        fprintf(stderr, "%s: failed to read file\n", path);
        free(source);
        fclose(file);
        return NULL;
    }
    fclose(file);
    source[file_len] = '\0';
    *out_len = (size_t)file_len;
    return source;
}

static int checker_check_file(const char *path)
{
    uint8_t *heap;
    JSContext *ctx;
    JSValue parsed;
    char *source;
    size_t source_len = 0;
    int result = 0;

    source = checker_load_source(path, &source_len);
    if (source == NULL) {
        return 2;
    }
    heap = malloc(CHECKER_HEAP_SIZE);
    if (heap == NULL) {
        fprintf(stderr, "%s: failed to allocate checker heap\n", path);
        free(source);
        return 2;
    }
    ctx = JS_NewContext(heap, CHECKER_HEAP_SIZE, &js_stdlib);
    if (ctx == NULL) {
        fprintf(stderr, "%s: failed to create MQuickJS context\n", path);
        free(heap);
        free(source);
        return 2;
    }
    JS_SetLogFunc(ctx, checker_log);

    parsed = JS_Parse(ctx, source, source_len, path, 0);
    if (JS_IsException(parsed)) {
        JSValue exception = JS_GetException(ctx);

        fprintf(stderr, "%s: ", path);
        JS_PrintValueF(ctx, exception, JS_DUMP_LONG);
        fputc('\n', stderr);
        result = 1;
    }

    JS_FreeContext(ctx);
    free(heap);
    free(source);
    return result;
}

int main(int argc, char **argv)
{
    int result = 0;
    int i;

    if (argc < 2) {
        fprintf(stderr, "usage: %s FILE.js [FILE.js ...]\n", argv[0]);
        return 2;
    }
    for (i = 1; i < argc; ++i) {
        int file_result = checker_check_file(argv[i]);

        if (file_result > result) {
            result = file_result;
        }
    }
    return result;
}
