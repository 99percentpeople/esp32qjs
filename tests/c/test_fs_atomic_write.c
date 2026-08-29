#include "esp32_mquickjs_fs_atomic_write.h"

#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    int open_result;
    int write_result;
    int replace_result;
    bool null_handle;
    size_t open_calls;
    size_t write_calls;
    size_t replace_calls;
    size_t remove_calls;
    bool target_replaced;
    bool temp_exists;
} atomic_write_probe_t;

static int probe_open(const char *target_path,
                      char *temp_path,
                      size_t temp_path_size,
                      void **out_handle,
                      void *opaque)
{
    atomic_write_probe_t *probe = opaque;

    assert(strcmp(target_path, "/storage/index.js") == 0);
    probe->open_calls++;
    if (probe->open_result != 0) {
        return probe->open_result;
    }
    assert(snprintf(temp_path, temp_path_size, "%s", "/storage/.qjs.tmp") > 0);
    probe->temp_exists = true;
    *out_handle = probe->null_handle ? NULL : probe;
    return 0;
}

static int probe_write_sync_close(void *handle,
                                  const char *data,
                                  size_t data_length,
                                  void *opaque)
{
    atomic_write_probe_t *probe = opaque;

    assert(handle == probe);
    assert(data_length == 3 && memcmp(data, "new", 3) == 0);
    probe->write_calls++;
    return probe->write_result;
}

static int probe_replace(const char *temp_path,
                         const char *target_path,
                         void *opaque)
{
    atomic_write_probe_t *probe = opaque;

    assert(strcmp(temp_path, "/storage/.qjs.tmp") == 0);
    assert(strcmp(target_path, "/storage/index.js") == 0);
    probe->replace_calls++;
    if (probe->replace_result == 0) {
        probe->target_replaced = true;
        probe->temp_exists = false;
    }
    return probe->replace_result;
}

static void probe_remove(const char *temp_path, void *opaque)
{
    atomic_write_probe_t *probe = opaque;

    assert(strcmp(temp_path, "/storage/.qjs.tmp") == 0);
    probe->remove_calls++;
    probe->temp_exists = false;
}

static int run_probe(atomic_write_probe_t *probe)
{
    static const esp32_mquickjs_fs_atomic_write_ops_t ops = {
        .open_temp = probe_open,
        .write_sync_close = probe_write_sync_close,
        .replace = probe_replace,
        .remove_temp = probe_remove,
    };
    char temp_path[64];

    return esp32_mquickjs_fs_atomic_write(
        "/storage/index.js", "new", 3, temp_path, sizeof(temp_path),
        &ops, probe);
}

static void test_interrupted_write_preserves_target_and_removes_temp(void)
{
    atomic_write_probe_t probe = {.write_result = EIO};

    assert(run_probe(&probe) == EIO);
    assert(probe.open_calls == 1);
    assert(probe.write_calls == 1);
    assert(probe.replace_calls == 0);
    assert(probe.remove_calls == 1);
    assert(!probe.target_replaced && !probe.temp_exists);
}

static void test_replace_failure_removes_temp_without_claiming_success(void)
{
    atomic_write_probe_t probe = {.replace_result = EIO};

    assert(run_probe(&probe) == EIO);
    assert(probe.write_calls == 1);
    assert(probe.replace_calls == 1);
    assert(probe.remove_calls == 1);
    assert(!probe.target_replaced && !probe.temp_exists);
}

static void test_open_failure_does_not_remove_uncreated_temp(void)
{
    atomic_write_probe_t probe = {.open_result = ENOSPC};

    assert(run_probe(&probe) == ENOSPC);
    assert(probe.open_calls == 1);
    assert(probe.write_calls == 0);
    assert(probe.replace_calls == 0);
    assert(probe.remove_calls == 0);
}

static void test_invalid_open_handle_is_cleaned(void)
{
    atomic_write_probe_t probe = {.null_handle = true};

    assert(run_probe(&probe) == EIO);
    assert(probe.write_calls == 0);
    assert(probe.replace_calls == 0);
    assert(probe.remove_calls == 1);
    assert(!probe.temp_exists);
}

static void test_synced_write_replaces_target_once(void)
{
    atomic_write_probe_t probe = {0};

    assert(run_probe(&probe) == 0);
    assert(probe.open_calls == 1);
    assert(probe.write_calls == 1);
    assert(probe.replace_calls == 1);
    assert(probe.remove_calls == 0);
    assert(probe.target_replaced && !probe.temp_exists);
}

int main(void)
{
    test_interrupted_write_preserves_target_and_removes_temp();
    test_replace_failure_removes_temp_without_claiming_success();
    test_open_failure_does_not_remove_uncreated_temp();
    test_invalid_open_handle_is_cleaned();
    test_synced_write_replaces_target_once();
    return 0;
}
