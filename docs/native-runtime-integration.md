# Native Runtime Integration

This document is for native ESP-IDF applications that embed
`components/esp32qjs_runtime`. JavaScript applications use the shared
[`docs/api`](api/README.md) reference and do not call this C API.

The managed runtime owns the MQuickJS heap and context, filesystem mounts,
startup script, runtime task, callback polling, optional REPL, and watchdog
subscription. The default firmware entry point already creates it; a custom
entry point only needs this API when it must control that native lifecycle.

## Create and start

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

The lifecycle operations are:

- `esp32qjs_runtime_default_config(config)` loads Kconfig-backed defaults.
- `esp32qjs_runtime_create(config, out_runtime)` allocates the JavaScript heap
  and context, installs built-ins, mounts configured filesystems, and invokes
  the application global installer. Only one runtime may be active.
- `esp32qjs_runtime_start(runtime)` starts the managed runtime task.
- `esp32qjs_runtime_request_stop(runtime)` requests a cooperative stop and
  wakes an idle runtime.
- `esp32qjs_runtime_stop(runtime, timeout_ms)` requests and waits for a stop;
  zero uses the configured timeout.
- `esp32qjs_runtime_destroy(runtime)` releases the stopped runtime. It returns
  `ESP_ERR_INVALID_STATE` while a native asynchronous owner is still draining;
  wait for that owner and retry rather than force-freeing the runtime.
- `esp32qjs_runtime_context(runtime)` and
  `esp32qjs_runtime_engine(runtime)` expose generation-scoped handles for
  trusted native integration. Never cache them across a runtime restart.

## Configuration lifetime

`esp32qjs_runtime_config_t` selects the JavaScript heap, evaluation deadline,
runtime task, stop policy, filesystem mounts, startup script, REPL, watchdog,
restart failure policy, and optional application-global installer. The
configuration and application opaque state must remain valid through
`esp32qjs_runtime_create()`; string fields are copied.

Create the runtime before another task starts RF or ADC hardware. Runtime
creation temporarily owns the SoC entropy source to seed the process-lifetime
secure random generator.

`CONFIG_ESP32QJS_ENABLE_REPL` and
`CONFIG_ESP32_MQUICKJS_FEATURE_USB_SERIAL` select mutually exclusive console
frontends. Do not enable `config.enable_repl` when the REPL is not compiled.

## Install application globals

Set `config.install_globals` to attach trusted native application objects:

```c
static bool install_app_globals(JSContext *ctx,
                                esp32_mquickjs_runtime_t *engine,
                                void *opaque)
{
    /* Install application globals with the MQuickJS API. */
    return true;
}
```

The callback runs after built-in globals are installed and once for every
JavaScript generation. Its opaque state must therefore outlive runtime
restarts. On failure, leave a JavaScript exception or diagnostic log before
returning `false`. Do not start asynchronous work until runtime creation has
completed successfully.

## Stop and teardown ownership

Shutdown is cooperative: `stop()` never force-deletes the runtime task, and
`destroy()` never frees state still owned by a callback or worker. A stop wakes
idle and bounded native waits. Generation teardown closes JavaScript-owned
timers, Futures, EventQueues, protocol endpoints, network clients and servers,
peripheral handles, and native byte sources before releasing the context.

The primary filesystem may be read-only; writable application state belongs on
an explicitly mounted secondary volume. Startup guarding uses only the private
`qjs_rt` NVS namespace. The embedding application supplies policy for optional
workspace loading and recovery rather than adding product behavior to the
runtime component.

See [Native lifecycle contracts](native-lifecycle-contracts.md) for callback,
finalizer, lease, EventQueue, and orphan-reaper invariants. Public JavaScript
deadline, scheduling, filesystem, and system-management behavior is defined by
the shared [API reference](api/README.md).
