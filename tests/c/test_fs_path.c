#include "esp32_mquickjs_fs_path.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void expect_true(bool condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        exit(1);
    }
}

static void expect_string(const char *actual, const char *expected, const char *message)
{
    if (strcmp(actual, expected) != 0) {
        fprintf(stderr, "FAIL: %s (expected %s, got %s)\n", message, expected, actual);
        exit(1);
    }
}

static void test_resolve_path(void)
{
    char path[128];

    expect_true(esp32_mquickjs_fs_resolve_path("/littlefs", ".", path, sizeof(path)),
                "dot path should resolve");
    expect_string(path, "/littlefs", "dot path target");

    expect_true(esp32_mquickjs_fs_resolve_path("/littlefs", "dir/file.txt", path, sizeof(path)),
                "relative path should resolve");
    expect_string(path, "/littlefs/dir/file.txt", "relative path target");

    expect_true(esp32_mquickjs_fs_resolve_path("/littlefs", "/littlefs/a/../b", path, sizeof(path)),
                "absolute littlefs path should resolve");
    expect_string(path, "/littlefs/b", "absolute littlefs normalization");

    expect_true(esp32_mquickjs_fs_resolve_path("/littlefs", "dir//nested/./file", path, sizeof(path)),
                "redundant separators should resolve");
    expect_string(path, "/littlefs/dir/nested/file", "redundant separators normalization");

    expect_true(!esp32_mquickjs_fs_resolve_path("/littlefs", "../escape", path, sizeof(path)),
                "parent escape should fail");
    expect_true(!esp32_mquickjs_fs_resolve_path("/littlefs", "/tmp/file", path, sizeof(path)),
                "wrong absolute root should fail");
}

static void test_basename(void)
{
    expect_string(esp32_mquickjs_fs_path_basename("/littlefs/file.txt"),
                  "file.txt",
                  "basename for file");
    expect_string(esp32_mquickjs_fs_path_basename("/littlefs"),
                  "littlefs",
                  "basename for root path");
    expect_string(esp32_mquickjs_fs_path_basename(""),
                  "",
                  "basename for empty string");
}

int main(void)
{
    test_resolve_path();
    test_basename();
    return 0;
}
