#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp32_mquickjs_version.h"
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
#define CHECKER_SOURCE_MAX (1024U * 1024U)

static void checker_log(void *opaque, const void *buf, size_t buf_len)
{
    (void)opaque;
    fwrite(buf, 1, buf_len, stderr);
}

static char *checker_read_source(FILE *file, const char *name, size_t *out_len)
{
    size_t capacity = 4096U;
    size_t length = 0;
    char *source = malloc(capacity + 1U);

    if (source == NULL) {
        fprintf(stderr, "%s: out of memory\n", name);
        return NULL;
    }
    while (!feof(file)) {
        size_t read_len;

        if (length == capacity) {
            size_t next_capacity = capacity * 2U;
            char *next_source;

            if (next_capacity > CHECKER_SOURCE_MAX) {
                next_capacity = CHECKER_SOURCE_MAX;
            }
            if (next_capacity == capacity) {
                fprintf(stderr, "%s: source exceeds %u bytes\n",
                        name,
                        (unsigned)CHECKER_SOURCE_MAX);
                free(source);
                return NULL;
            }
            next_source = realloc(source, next_capacity + 1U);
            if (next_source == NULL) {
                fprintf(stderr, "%s: out of memory\n", name);
                free(source);
                return NULL;
            }
            source = next_source;
            capacity = next_capacity;
        }
        read_len = fread(source + length, 1, capacity - length, file);
        length += read_len;
        if (read_len == 0 && ferror(file)) {
            fprintf(stderr, "%s: failed to read source\n", name);
            free(source);
            return NULL;
        }
    }
    source[length] = '\0';
    *out_len = length;
    return source;
}

static char *checker_load_source(const char *path, size_t *out_len)
{
    FILE *file = fopen(path, "rb");
    char *source;

    if (file == NULL) {
        fprintf(stderr, "%s: %s\n", path, strerror(errno));
        return NULL;
    }
    source = checker_read_source(file, path, out_len);
    fclose(file);
    return source;
}

static int checker_process_source(const char *source,
                                  size_t source_len,
                                  const char *name,
                                  const char *output_path)
{
    uint8_t *heap;
    JSContext *ctx;
    JSValue parsed;
    int result = 0;

    heap = malloc(CHECKER_HEAP_SIZE);
    if (heap == NULL) {
        fprintf(stderr, "%s: failed to allocate checker heap\n", name);
        return 2;
    }
    ctx = JS_NewContext2(heap,
                         CHECKER_HEAP_SIZE,
                         &js_stdlib,
                         output_path != NULL);
    if (ctx == NULL) {
        fprintf(stderr, "%s: failed to create MQuickJS context\n", name);
        free(heap);
        return 2;
    }
    JS_SetLogFunc(ctx, checker_log);

    parsed = JS_Parse(ctx,
                      source,
                      source_len,
                      name,
                      output_path != NULL ? JS_EVAL_STRIP_COL : 0);
    if (JS_IsException(parsed)) {
        JSValue exception = JS_GetException(ctx);

        fprintf(stderr, "%s: ", name);
        JS_PrintValueF(ctx, exception, JS_DUMP_LONG);
        fputc('\n', stderr);
        result = 1;
    } else if (output_path != NULL) {
        const uint8_t *data;
        uint32_t data_len;
        FILE *output;
#if JSW == 8
        JSBytecodeHeader32 header;

        if (JS_PrepareBytecode64to32(ctx, &header, &data, &data_len, parsed) != 0) {
            fprintf(stderr, "%s: failed to generate 32-bit bytecode\n", name);
            result = 2;
            goto done;
        }
#else
        JSBytecodeHeader header;

        JS_PrepareBytecode(ctx, &header, &data, &data_len, parsed);
        if (JS_RelocateBytecode2(ctx,
                                &header,
                                (uint8_t *)data,
                                data_len,
                                0,
                                0) != 0) {
            fprintf(stderr, "%s: failed to relocate 32-bit bytecode\n", name);
            result = 2;
            goto done;
        }
#endif
        output = fopen(output_path, "wb");
        if (output == NULL) {
            fprintf(stderr, "%s: %s\n", output_path, strerror(errno));
            result = 2;
            goto done;
        }
        if (fwrite(&header, 1, sizeof(header), output) != sizeof(header) ||
            fwrite(data, 1, data_len, output) != data_len) {
            fprintf(stderr, "%s: failed to write bytecode\n", output_path);
            result = 2;
        }
        if (fclose(output) != 0) {
            fprintf(stderr, "%s: failed to close bytecode output\n", output_path);
            result = 2;
        }
    }

done:
    JS_FreeContext(ctx);
    free(heap);
    return result;
}

static int checker_check_file(const char *path, const char *output_path)
{
    char *source;
    size_t source_len = 0;
    int result;

    source = checker_load_source(path, &source_len);
    if (source == NULL) {
        return 2;
    }
    result = checker_process_source(source, source_len, path, output_path);
    free(source);
    return result;
}

int main(int argc, char **argv)
{
    int result = 0;
    int i;

    if (argc < 2) {
        fprintf(stderr,
                "usage: %s FILE.js [FILE.js ...] | --stdin NAME | "
                "--compile32 INPUT OUTPUT | --engine-version\n",
                argv[0]);
        return 2;
    }
    if (argc == 2 && strcmp(argv[1], "--engine-version") == 0) {
        puts(ESP32_MQUICKJS_ENGINE_VERSION);
        return 0;
    }
    if (argc == 3 && strcmp(argv[1], "--stdin") == 0) {
        char *source;
        size_t source_len = 0;

        source = checker_read_source(stdin, argv[2], &source_len);
        if (source == NULL) {
            return 2;
        }
        result = checker_process_source(source, source_len, argv[2], NULL);
        free(source);
        return result;
    }
    if (argc == 4 && strcmp(argv[1], "--compile32") == 0) {
        return checker_check_file(argv[2], argv[3]);
    }
    for (i = 1; i < argc; ++i) {
        int file_result = checker_check_file(argv[i], NULL);

        if (file_result > result) {
            result = file_result;
        }
    }
    return result;
}
