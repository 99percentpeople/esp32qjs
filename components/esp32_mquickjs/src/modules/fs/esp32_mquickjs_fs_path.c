#include "utils/esp32_mquickjs_fs_path.h"

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

    if (base_path[0] != '/') {
        return false;
    }
    if (input_path[0] == '/') {
        base_path = "/";
    }
    base_len = strlen(base_path);
    if (out_path_size <= base_len) {
        return false;
    }

    memcpy(out_path, base_path, base_len);
    out_path[base_len] = '\0';
    out_len = base_len;

    if (input_path[0] == '\0' || strcmp(input_path, ".") == 0) {
        return true;
    }

    /* Absolute paths use the shared namespace; relative paths use the receiver. */
    cursor = input_path;

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

            if (out_len == 1U) {
                return false;
            }

            slash = strrchr(out_path, '/');
            if (slash == NULL) {
                return false;
            }
            out_len = slash == out_path ? 1U : (size_t)(slash - out_path);
            out_path[out_len] = '\0';
            continue;
        }

        if (out_len + (out_len > 1U ? 1U : 0U) + segment_len >= out_path_size) {
            return false;
        }

        if (out_len > 1U) {
            out_path[out_len++] = '/';
        }
        memcpy(out_path + out_len, segment_start, segment_len);
        out_len += segment_len;
        out_path[out_len] = '\0';
    }

    return true;
}

bool esp32_mquickjs_fs_path_contains(const char *root, const char *path)
{
    size_t length;

    if (root == NULL || path == NULL || root[0] != '/' || path[0] != '/') {
        return false;
    }
    length = strlen(root);
    return length == 1U ||
           (strncmp(root, path, length) == 0 &&
            (path[length] == '\0' || path[length] == '/'));
}

bool esp32_mquickjs_fs_mount_child(const char *parent, const char *mount,
                                   char *out, size_t out_size)
{
    const char *child;
    const char *end;
    size_t length;

    if (!esp32_mquickjs_fs_path_contains(parent, mount) ||
        strcmp(parent, mount) == 0 || out == NULL) {
        return false;
    }
    child = mount + (strcmp(parent, "/") == 0 ? 1U : strlen(parent) + 1U);
    end = strchr(child, '/');
    length = end == NULL ? strlen(mount) : (size_t)(end - mount);
    if (length >= out_size) {
        return false;
    }
    memcpy(out, mount, length);
    out[length] = '\0';
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
