#include "esp32_mquickjs_fs_path.h"

#if CONFIG_ESP32_MQUICKJS_FEATURE_FS

#include <string.h>

bool esp32_mquickjs_fs_resolve_path(const char *base_path,
                                    const char *input_path,
                                    char *out_path,
                                    size_t out_path_size)
{
    const char *cursor;
    size_t base_len;
    size_t out_len;

    if (base_path == NULL || input_path == NULL || out_path == NULL) {
        return false;
    }

    base_len = strlen(base_path);
    if (base_len == 0 || out_path_size <= base_len + 1) {
        return false;
    }

    memcpy(out_path, base_path, base_len);
    out_path[base_len] = '\0';
    out_len = base_len;

    if (input_path[0] == '\0' || strcmp(input_path, ".") == 0) {
        return true;
    }

    if (input_path[0] == '/') {
        if (strncmp(input_path, base_path, base_len) != 0) {
            return false;
        }
        if (input_path[base_len] != '\0' && input_path[base_len] != '/') {
            return false;
        }
        cursor = input_path + base_len;
    } else {
        cursor = input_path;
    }

    while (*cursor != '\0') {
        const char *segment_start;
        size_t segment_len;

        while (*cursor == '/') {
            cursor++;
        }
        if (*cursor == '\0') {
            break;
        }

        segment_start = cursor;
        while (*cursor != '\0' && *cursor != '/') {
            cursor++;
        }
        segment_len = (size_t)(cursor - segment_start);

        if (segment_len == 1 && segment_start[0] == '.') {
            continue;
        }

        if (segment_len == 2 && segment_start[0] == '.' && segment_start[1] == '.') {
            char *slash;

            if (out_len == base_len) {
                return false;
            }

            slash = strrchr(out_path, '/');
            if (slash == NULL || (size_t)(slash - out_path) < base_len) {
                return false;
            }
            *slash = '\0';
            out_len = (size_t)(slash - out_path);
            continue;
        }

        if (out_len + 1 + segment_len >= out_path_size) {
            return false;
        }

        out_path[out_len++] = '/';
        memcpy(out_path + out_len, segment_start, segment_len);
        out_len += segment_len;
        out_path[out_len] = '\0';
    }

    return true;
}

const char *esp32_mquickjs_fs_path_basename(const char *path)
{
    const char *slash;

    if (path == NULL || path[0] == '\0') {
        return "";
    }

    slash = strrchr(path, '/');
    if (slash == NULL) {
        return path;
    }
    return slash[1] == '\0' ? slash : slash + 1;
}

#endif
