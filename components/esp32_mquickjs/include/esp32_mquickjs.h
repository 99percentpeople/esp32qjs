#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "sdkconfig.h"
#include "mquickjs.h"

#define ESP32QJS_VERSION "0.1.0"
#define ESP32QJS_HOST_API_VERSION ((uint32_t)1U)

#define ESP32_MQUICKJS_DEFAULT_EVAL_TIMEOUT_MS ((uint32_t)CONFIG_ESP32_MQUICKJS_DEFAULT_EVAL_TIMEOUT_MS)
#define ESP32_MQUICKJS_COOPERATIVE_WAIT_SLICE_MS ((uint32_t)CONFIG_ESP32_MQUICKJS_COOPERATIVE_WAIT_SLICE_MS)
#define ESP32_MQUICKJS_LITTLEFS_BASE_PATH "/littlefs"
#define ESP32_MQUICKJS_LITTLEFS_PARTITION_LABEL "storage"
#define ESP32_MQUICKJS_FS_ROOT_MAX 64U
#define ESP32_MQUICKJS_BOOT_ID_LENGTH 16U
#define ESP32_MQUICKJS_CONTROL_REASON_MAX 64U
#define ESP32_MQUICKJS_HOST_TASK_NAME_MAX 24U
#define ESP32_MQUICKJS_HOST_STARTUP_PATH_MAX 256U
#define ESP32_MQUICKJS_HOST_PARTITION_LABEL_MAX 17U

typedef struct esp32_mquickjs_runtime esp32_mquickjs_runtime_t;
typedef uint32_t esp32_mquickjs_poll_result_t;

typedef bool (*esp32_mquickjs_async_poller_t)(JSContext *ctx,
                                              esp32_mquickjs_runtime_t *runtime,
                                              void *opaque);
typedef bool (*esp32_mquickjs_cooperate_fn)(void *opaque);

typedef enum {
    ESP32_MQUICKJS_RUNTIME_CREATED,
    ESP32_MQUICKJS_RUNTIME_STARTING,
    ESP32_MQUICKJS_RUNTIME_RUNNING,
    ESP32_MQUICKJS_RUNTIME_QUIESCING,
    ESP32_MQUICKJS_RUNTIME_RESTARTING,
    ESP32_MQUICKJS_RUNTIME_STOPPING,
    ESP32_MQUICKJS_RUNTIME_STOPPED,
    ESP32_MQUICKJS_RUNTIME_FAILED,
} esp32_mquickjs_runtime_state_t;

typedef enum {
    ESP32_MQUICKJS_CONTROL_RESTART_RUNTIME,
    ESP32_MQUICKJS_CONTROL_REBOOT,
} esp32_mquickjs_control_action_t;

typedef enum {
    ESP32_MQUICKJS_RESTART_FAILURE_REBOOT,
    ESP32_MQUICKJS_RESTART_FAILURE_STOP,
} esp32_mquickjs_restart_failure_action_t;

typedef enum {
    ESP32_MQUICKJS_CONTROL_ACCEPTED,
    ESP32_MQUICKJS_CONTROL_UNAVAILABLE,
    ESP32_MQUICKJS_CONTROL_ALREADY_PENDING,
    ESP32_MQUICKJS_CONTROL_INVALID_STATE,
} esp32_mquickjs_control_result_t;

typedef struct {
    esp32_mquickjs_control_action_t action;
    uint32_t generation;
    uint64_t requested_at_ms;
    uint64_t due_at_ms;
} esp32_mquickjs_control_receipt_t;

typedef struct {
    bool managed;
    esp32_mquickjs_runtime_state_t state;
    uint32_t generation;
    uint32_t restart_count;
    uint64_t generation_started_us;
    char last_restart_reason[ESP32_MQUICKJS_CONTROL_REASON_MAX + 1U];
    bool pending_control;
    esp32_mquickjs_control_action_t pending_action;
    char pending_reason[ESP32_MQUICKJS_CONTROL_REASON_MAX + 1U];
    uint64_t pending_requested_at_ms;
    uint64_t pending_due_at_ms;

    char task_name[ESP32_MQUICKJS_HOST_TASK_NAME_MAX];
    uint32_t task_stack_size;
    uint32_t task_priority;
    bool task_watchdog_enabled;
    bool task_watchdog_registered;
    bool js_watchdog_enabled;
    bool js_watchdog_registered;
    uint32_t watchdog_timeout_ms;
    uint64_t last_outer_heartbeat_us;

    char startup_script[ESP32_MQUICKJS_HOST_STARTUP_PATH_MAX];
    bool autorun_startup_script;
    bool repl_enabled;

    bool mount_littlefs;
    bool require_littlefs;
    bool format_littlefs_on_mount_fail;
    bool littlefs_mounted;
    char fs_root[ESP32_MQUICKJS_FS_ROOT_MAX];
    bool mount_secondary_littlefs;
    bool require_secondary_littlefs;
    bool secondary_littlefs_mounted;
    char secondary_partition[ESP32_MQUICKJS_HOST_PARTITION_LABEL_MAX];
    char secondary_root[ESP32_MQUICKJS_FS_ROOT_MAX];

    bool restart_runtime_available;
    bool reboot_available;
    uint32_t restart_timeout_ms;
    esp32_mquickjs_restart_failure_action_t restart_failure_action;

    bool software_reason_available;
    char software_reason[ESP32_MQUICKJS_CONTROL_REASON_MAX + 1U];
    bool safe_mode_available;
    bool safe_mode_requested;
    bool safe_mode_active;
    uint32_t startup_failure_count;
    uint32_t startup_failure_limit;
    uint32_t startup_healthy_ms;
    bool startup_pending;
    bool startup_stabilizing;
    char last_startup_failure_reason[ESP32_MQUICKJS_CONTROL_REASON_MAX + 1U];
} esp32_mquickjs_host_status_t;

typedef bool (*esp32_mquickjs_host_status_fn)(void *opaque,
                                              esp32_mquickjs_host_status_t *status);
typedef esp32_mquickjs_control_result_t (*esp32_mquickjs_system_control_fn)(
    void *opaque,
    esp32_mquickjs_control_action_t action,
    const char *reason,
    uint32_t delay_ms,
    esp32_mquickjs_control_receipt_t *receipt);
typedef bool (*esp32_mquickjs_safe_mode_control_fn)(void *opaque, bool enabled);

typedef struct {
    uint32_t timers_active;
    uint32_t timers_capacity;
    uint32_t futures_queued;
    uint32_t futures_pending;
    uint32_t futures_capacity;
    uint32_t futures_user_capacity;
    uint32_t futures_internal_reserve;
    uint32_t event_queues_open;
    uint32_t event_queues_dropped;
    uint32_t async_pollers_registered;
    uint32_t async_pollers_capacity;
} esp32_mquickjs_resource_status_t;

typedef struct {
    uint64_t saved_deadline_us;
    uint64_t started_us;
    bool active;
} esp32_mquickjs_native_wait_t;

#define ESP32_MQUICKJS_POLL_NONE   ((esp32_mquickjs_poll_result_t)0U)
#define ESP32_MQUICKJS_POLL_ASYNC  ((esp32_mquickjs_poll_result_t)(1U << 0))
#define ESP32_MQUICKJS_POLL_OUTPUT ((esp32_mquickjs_poll_result_t)(1U << 1))

struct esp32_mquickjs_runtime {
    uint32_t eval_timeout_ms;
    uint32_t async_generation;
    uint32_t output_generation;
    size_t js_heap_size;
    bool js_heap_in_psram;
    bool littlefs_mounted;
    bool repl_enabled;
    bool auto_run_startup_script;
    bool format_littlefs_on_mount_fail;
    uint64_t deadline_us;
    void (*prepare_output)(void *opaque);
    void *prepare_output_opaque;
    esp32_mquickjs_cooperate_fn cooperate;
    void *cooperate_opaque;
    void *timer_state;
    void *async_state;
    void *future_state;
    void *event_queue_state;
    void *fs_state;
    void *runtime_log_state;
    void *startup_bytecode;
    uint64_t scoped_deadline_us;
    uint16_t native_wait_depth;
    uint16_t load_root_depth;
    char startup_fs_root[ESP32_MQUICKJS_FS_ROOT_MAX];
    char load_root[ESP32_MQUICKJS_FS_ROOT_MAX];
    char boot_id[ESP32_MQUICKJS_BOOT_ID_LENGTH + 1U];
    uint64_t context_started_us;
    esp32_mquickjs_host_status_fn host_status;
    esp32_mquickjs_system_control_fn system_control;
    esp32_mquickjs_safe_mode_control_fn safe_mode_control;
    void *system_opaque;
};

JSContext *esp32_mquickjs_create(void *mem_start,
                                 size_t mem_size,
                                 esp32_mquickjs_runtime_t *runtime,
                                 uint32_t eval_timeout_ms);

/* Returns false while an asynchronous native worker still owns JS state. */
bool esp32_mquickjs_destroy(JSContext *ctx,
                            esp32_mquickjs_runtime_t *runtime);

/* Destroy one JS generation while retaining boot-scoped runtime logs/hooks. */
bool esp32_mquickjs_destroy_generation(JSContext *ctx,
                                       esp32_mquickjs_runtime_t *runtime);

/* Release boot-scoped native state after the last generation is gone. */
void esp32_mquickjs_release_persistent_state(esp32_mquickjs_runtime_t *runtime);

void esp32_mquickjs_set_system_hooks(esp32_mquickjs_runtime_t *runtime,
                                     esp32_mquickjs_host_status_fn status,
                                     esp32_mquickjs_system_control_fn control,
                                     void *opaque);

void esp32_mquickjs_set_safe_mode_hook(
    esp32_mquickjs_runtime_t *runtime,
    esp32_mquickjs_safe_mode_control_fn control);

bool esp32_mquickjs_set_safe_mode(esp32_mquickjs_runtime_t *runtime,
                                  bool enabled);

bool esp32_mquickjs_get_host_status(esp32_mquickjs_runtime_t *runtime,
                                    esp32_mquickjs_host_status_t *status);

bool esp32_mquickjs_request_system_control(
    esp32_mquickjs_runtime_t *runtime,
    esp32_mquickjs_control_action_t action,
    const char *reason,
    uint32_t delay_ms,
    esp32_mquickjs_control_receipt_t *receipt,
    esp32_mquickjs_control_result_t *result);

bool esp32_mquickjs_get_resource_status(
    esp32_mquickjs_runtime_t *runtime,
    esp32_mquickjs_resource_status_t *status);

bool esp32_mquickjs_mount_littlefs(bool format_if_mount_failed);
void esp32_mquickjs_unmount_littlefs(void);
bool esp32_mquickjs_mount_littlefs_partition(const char *partition_label,
                                             const char *base_path,
                                             bool format_if_mount_failed);
void esp32_mquickjs_unmount_littlefs_partition(const char *partition_label);

void esp32_mquickjs_set_eval_timeout(esp32_mquickjs_runtime_t *runtime,
                                     uint32_t eval_timeout_ms);

void esp32_mquickjs_set_cooperate_hook(esp32_mquickjs_runtime_t *runtime,
                                       esp32_mquickjs_cooperate_fn cooperate,
                                       void *opaque);
bool esp32_mquickjs_cooperate(esp32_mquickjs_runtime_t *runtime);
void esp32_mquickjs_native_wait_begin(esp32_mquickjs_runtime_t *runtime,
                                      esp32_mquickjs_native_wait_t *wait);
void esp32_mquickjs_native_wait_end(esp32_mquickjs_runtime_t *runtime,
                                    esp32_mquickjs_native_wait_t *wait);
bool esp32_mquickjs_cooperative_delay(esp32_mquickjs_runtime_t *runtime,
                                      uint32_t delay_ms);

JSValue esp32_mquickjs_eval(JSContext *ctx,
                            esp32_mquickjs_runtime_t *runtime,
                            const char *source,
                            const char *filename,
                            int eval_flags);

JSValue esp32_mquickjs_run(JSContext *ctx,
                           esp32_mquickjs_runtime_t *runtime,
                           JSValue compiled_code);

JSValue esp32_mquickjs_load_startup_from_active_fs(
    JSContext *ctx,
    esp32_mquickjs_runtime_t *runtime,
    const char *script_path);

JSValue esp32_mquickjs_call(JSContext *ctx,
                            esp32_mquickjs_runtime_t *runtime,
                            JSValue function,
                            JSValue this_value,
                            int argc,
                            JSValue *argv);

void esp32_mquickjs_print_exception(JSContext *ctx);

bool esp32_mquickjs_install_globals(JSContext *ctx,
                                    esp32_mquickjs_runtime_t *runtime);

esp32_mquickjs_poll_result_t esp32_mquickjs_poll(JSContext *ctx,
                                                 esp32_mquickjs_runtime_t *runtime);

bool esp32_mquickjs_register_async_poller(esp32_mquickjs_runtime_t *runtime,
                                          esp32_mquickjs_async_poller_t poller,
                                          void *opaque);

void esp32_mquickjs_attach_current_task(esp32_mquickjs_runtime_t *runtime);
void esp32_mquickjs_detach_current_task(esp32_mquickjs_runtime_t *runtime);

void esp32_mquickjs_notify_activity(esp32_mquickjs_runtime_t *runtime);

void esp32_mquickjs_notify_active_runtime_from_isr(int *task_woken);

bool esp32_mquickjs_wait_for_activity(esp32_mquickjs_runtime_t *runtime,
                                      uint32_t timeout_ms);
