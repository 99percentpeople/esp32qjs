# Native Runtime Integration

`components/esp32qjs_runtime` owns the default JavaScript heap, mquickjs context,
LittleFS mount, startup script, runtime task, optional REPL, callback polling,
and task-watchdog subscription.

Applications that only provide JavaScript normally do not need this API. Their
entry point belongs in `apps/<name>/flash_data/index.js`.

## Lifecycle

```c
#include "esp32qjs_runtime.h"

static esp32qjs_runtime_t *runtime;

void app_main(void)
{
    esp32qjs_runtime_config_t config;

    esp32qjs_runtime_default_config(&config);
    ESP_ERROR_CHECK(esp32qjs_runtime_create(&config, &runtime));
    ESP_ERROR_CHECK(esp32qjs_runtime_start(runtime));
}
```

Available operations:

- `esp32qjs_runtime_default_config(config)`
  Load Kconfig-backed defaults.
- `esp32qjs_runtime_create(config, out_runtime)`
  Allocate the JS heap and context, install built-ins, mount LittleFS, and run
  the optional application global installer. Version `0.1.0` supports one
  active runtime.
- `esp32qjs_runtime_start(runtime)`
  Start the runtime task.
- `esp32qjs_runtime_request_stop(runtime)`
  Signal the runtime task and wake it from an idle wait.
- `esp32qjs_runtime_stop(runtime, timeout_ms)`
  Request a stop and wait for the task. Passing `0` uses the configured default.
- `esp32qjs_runtime_destroy(runtime)`
  Stop HTTP servers, detach GPIO/Wi-Fi callbacks, release the context and native
  timer/poller state, unmount LittleFS, and free the JS heap. The runtime must
  already be stopped. It returns `ESP_ERR_INVALID_STATE` while an outgoing
  asynchronous HTTP worker is still active; retry after that worker completes.
- `esp32qjs_runtime_context(runtime)` / `esp32qjs_runtime_engine(runtime)`
  Access low-level handles for trusted native integration.

## Configuration

`esp32qjs_runtime_config_t` controls:

- JS heap size and PSRAM preference
- evaluation/callback deadline
- runtime task stack, priority, stop timeout, and watchdog subscription
- LittleFS mount, required/optional policy, and format-on-failure behavior
- startup script and autorun
- REPL enablement
- application-specific global installation

The configuration and pointed-to application state must remain valid through
`esp32qjs_runtime_create()`. String fields are copied by the runtime.

## Installing Application Globals

Set `config.install_globals` to a callback:

```c
static bool install_app_globals(JSContext *ctx,
                                esp32_mquickjs_runtime_t *engine,
                                void *opaque)
{
    // Use the mquickjs API to attach trusted application functions or objects.
    return true;
}
```

The callback runs after built-in Host API initialization. It must leave a clear
log or JavaScript exception before returning `false`, and it must not start
asynchronous work until `esp32qjs_runtime_create()` has returned successfully.

## Callback Safety

Native-to-JavaScript calls go through `esp32_mquickjs_call()`. It applies the
runtime deadline and never extends an earlier nested deadline. The runtime task
watchdog is a final recovery layer, not a replacement for callback deadlines.

## Current Lifecycle Constraint

Shutdown is cooperative. `stop()` never force-deletes the runtime task, and
`destroy()` never frees callback state still owned by an outgoing HTTP worker.
HTTP servers and registered GPIO/Wi-Fi callbacks are shut down automatically;
object finalizers close synchronous peripheral handles. If `destroy()` reports
`ESP_ERR_INVALID_STATE`, wait for the bounded HTTP request to finish and retry
destruction; the retry drains its completion without invoking the old callback.
