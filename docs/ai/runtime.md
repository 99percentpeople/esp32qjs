# Runtime API and execution rules

## Runtime boundaries

Do not infer one JavaScript environment from another repository directory.

| Code location                                                      | Runtime                    | Constraints                                                                                           |
| ------------------------------------------------------------------ | -------------------------- | ----------------------------------------------------------------------------------------------------- |
| `exec`, `/workspace/*.js`, Build Context `flash_data/**/*.js` | Vendored MQuickJS on ESP32 | ES5-like source, feature-gated ESP32QJS globals, no Node.js or browser environment                    |
| `web/`, host-side Agent code, host tests                         | Bun/TypeScript             | Modern JavaScript/TypeScript and allowlisted host APIs; never send this source directly to the device |
| `firmware/` native modules                                       | ESP-IDF C/C++ and FreeRTOS | Native ownership, task, heap, and driver rules; these are not JavaScript APIs until explicitly bound  |

The ESP32 runs one active MQuickJS runtime. Device code is trusted application
code, but it is not a Node.js process, browser, ECMAScript-module loader, or
untrusted-code sandbox. Optional globals exist only when the corresponding
firmware feature is compiled; check `sys.info.features` before use.

## Language

The device runs a vendored MQuickJS ES5-like dialect. Use `var`, ordinary
functions, function expressions, arrays, plain objects, `try`/`catch`/`finally`,
and supported built-ins.

Do not emit `const`, `let`, classes, arrow functions, template literals,
destructuring, spread/rest syntax, optional chaining, nullish coalescing,
generators, async/await, `import`, or `export`. Do not use `require`, `process`,
Node `Buffer`, DOM APIs, or other assumed host globals. Use ESP32QJS APIs such
as `ByteView`, `Future`, `EventQueue`, `load`, and `framework.load` only when
their documented feature is present.

## `ByteView`

Native binary APIs return an owned, immutable `ByteView`. Read `length` or
`byteLength`, use `getUint8(offset)` to inspect a small in-range protocol field
without allocating, and use `toArray()` only when a full JavaScript copy is
actually required. Call idempotent `close()` after the last consumer; native
operations retain their own read lease while in flight.

### Catch bindings are function-wide

MQuickJS does not give catch parameters the independent block binding expected
from modern engines. Within a single function, every catch parameter must have
a unique name, and that name must not duplicate a function parameter or `var`
declaration anywhere in the same function. Reusing `_` or `error` is invalid:

```text
(function () {
    try {
        firstOperation();
    } catch (error) {
        print(error);
    }
    try {
        secondOperation();
    } catch (error) { // SyntaxError: catch variable already exists
        print(error);
    }
})()
```

Use names tied to the failing operation:

```js
(function () {
    try {
        firstOperation();
    } catch (firstOperationError) {
        print(firstOperationError);
    }
    try {
        secondOperation();
    } catch (secondOperationError) {
        print(secondOperationError);
    }
})()
```

The same catch name may be used inside a different nested function because that
is a separate function scope. Prefer unique descriptive names anyway when
editing a large function.

### Syntax validation

The host validates `exec` code and complete `.js` content produced by `write`
or `edit` with the exact vendored MQuickJS parser before dispatch or mutation.
`JAVASCRIPT_SYNTAX_ERROR` means the device call or file mutation did not start;
fix the reported source instead of retrying it unchanged. Node, Bun, TypeScript,
and browser parsers are not syntax authorities for device code.

## `exec({code, timeout?})`

- `code`: 1–4096 UTF-8 bytes of device JavaScript.
- `timeout`: optional host deadline in milliseconds, 1–60000; default 10000.
- The host validates source with the vendored MQuickJS parser before sending an
  `exec`; a syntax failure means no device operation was dispatched.
- The final expression is serialized as the result. A top-level `return` is invalid.
- Valid results are primitives or JSON-compatible values. Native handles, cycles, and large buffers must be projected to small plain objects.
- `print()` produces a device log; it is not the tool result.

Use an IIFE for multi-statement inspection:

```js
(function () {
    var chip = sys.info.hardware.chip;
    return {
        mcu: sys.info.hardware.target,
        chip: chip.model,
        freeHeap: sys.freeHeap(),
        wifi: sys.info.features.wifi
    };
})()
```

An undefined final expression returns `{ "kind": "undefined" }`. This means execution succeeded. If a value was needed, issue one corrected read-only call; never automatically repeat a mutation.

## `Future`

- `Future.call(fn, thisValue?, args?)`
- `Future.all(futures)`
- `Future.race(futures)`
- `Future.sleep(ms)`
- `Future.timeout(future, timeoutMs)`
- instance `status()`
- instance `wait(timeoutMs?)`
- instance `cancel()`
- instance `map(fn)` for a normal value transformation
- instance `flatMap(fn)` when the callback returns another Future

Direct device APIs are synchronous from the calling JavaScript stack. Use
Futures to schedule bounded cooperative work:

```js
(function () {
    var scan = Future.call(wifi.scan, wifi, []);
    var pause = Future.sleep(20);
    var values = Future.all([scan, pause]).wait(10000);
    return { networks: values[0] };
})()
```

Cancellation is cooperative. A wait timeout does not prove that an underlying mutation did not run.
`map()` does not flatten a Future returned by its callback; use `flatMap()` for
that behavior. Every operation that may wait on hardware, network, storage, or
an event source has a native `Future.call()` path so it can be submitted before
the current JavaScript turn returns.

## Scheduling and blocking

- MQuickJS executes one JavaScript context; Futures do not create parallel
  JavaScript threads.
- Cooperative native waits poll runtime activity, so timers, completed Futures,
  and registered event sources can progress while the current call is waiting.
- A callback may therefore run during a nested cooperative wait. Do not let it
  mutate or close a handle currently owned by the waiting operation.
- CPU-heavy JavaScript loops and synchronous native processing do not yield.
  Move bulk image conversion, resizing, dithering, and byte transport to native
  APIs instead of materializing large arrays in JavaScript.
- `camera.capture()`, `bitmap.convert()`, and `Bitmap.blit()` perform bounded
  native work through the Future worker queue and pump runtime events while the
  synchronous JavaScript call waits. The following statement still waits for
  the operation to finish. Callbacks that run during that wait cannot close or
  mutate leased Bitmap, CameraFrame, or ByteView values; such attempts fail
  with a busy error. Panel transfer remains synchronous to the display driver.
- Always pass finite API timeouts. The host `exec` deadline and
  `sys.withTimeout()` are wall-clock bounds; cancellation remains cooperative,
  and an uncertain mutation must not be replayed automatically.

## `EventQueue`

Native event sources use bounded `EventQueue` handles. `EventQueue` cannot be
constructed directly.

- `receive(timeoutMs?)`: synchronously wait for the next event or return `null`
  at timeout.
- `close()`: stop delivery and release the queue.
- Concrete sources may add operations, but repeated input consistently uses
  `receive(timeoutMs?)`; the old transport-specific `recv()` alias does not
  exist. WebSocket and USB Serial handles also expose `send()` and `status()`.

Always close queues that persistent code owns.

## Timers

- `setTimeout(fn, delayMs)` / `clearTimeout(handle)`
- `setInterval(fn, intervalMs)` / `clearInterval(handle)`

Store timer IDs and clear them during application cleanup. Avoid leaving detached one-off work after `exec`.

## `sys`

- `sys.info` is a lazy getter tree for stable version, hardware, compiled
  feature, and runtime-configuration facts. Read only the needed leaf.
- `sys.status` is a lazy getter tree for boot, CPU, memory, RTOS, and active
  runtime-generation state.
- `sys.tasks({limit?})` takes an explicit bounded FreeRTOS task snapshot when
  that build capability is enabled.
- `sys.restartRuntime({reason?, delayMs?})` and `sys.reboot(...)` schedule
  lifecycle control at the next safe runtime-loop point and return an
  acceptance receipt.
- `sys.status.runtime.watchdog` reports the system-task and outer-JavaScript
  watchdogs. `sys.status.runtime.startup` reports startup health and safe-mode
  state.
- `sys.safeMode` is a persistent operator-only boolean. Writing `false` clears
  the startup failure latch for the next boot; it does not load workspace code
  immediately. Generated and startup code must never modify it.
- `sys.millis()` and `sys.micros()` return monotonic uptime counters.
- `sys.freeHeap()` returns available heap bytes.
- `sys.randomHex(byteLength)` returns 1-64 random bytes as lowercase
  hexadecimal text.
- `sys.config()` returns a fresh plain object containing every immutable
  hardware-profile constant. `sys.config(key)` reads one constant and returns a
  string, number, boolean, or `undefined`. Registered framework keys use the
  `ESP32QJS_*` prefix; application-specific constants should use another prefix
  such as `APP_*`.
- `sys.withTimeout(timeoutMs, fn)` shortens the active wall-clock deadline for `fn`; it cannot extend the host `exec` deadline.

For device boot uptime use `sys.status.boot.uptimeMs` or `sys.millis()`; for the
current JavaScript generation use `sys.status.runtime.uptimeMs`. For detailed
default heap state use `sys.status.memory.default.freeBytes`; `sys.freeHeap()`
is its low-allocation convenience form. Memory capability views overlap and
must not be summed.

Use one targeted feature query instead of a chain of `typeof` probes:

```js
(function () {
    return {
        wifi: sys.info.features.wifi,
        i2c: sys.info.features.i2c,
        userLedPin: gpio.USER_LED_PIN
    };
})()
```

Do not invoke `sys.restartRuntime()` or `sys.reboot()` unless the user
explicitly requested that exact lifecycle operation. Operator code should use
the dedicated device lifecycle control message because a control issued through
ordinary `exec` can disconnect before its tool result arrives and has an
uncertain outcome.

## Application lifecycle

Persistent startup code should own a single state object and clean it before starting again:

```js
(function (global) {
    "use strict";

    if (global.app && typeof global.app.stop === "function") {
        global.app.stop();
    }

    var ledAvailable = gpio.USER_LED_PIN >= 0 || gpio.LED_BUILTIN >= 0;
    var ledOn = false;
    var timer = null;
    if (ledAvailable) {
        timer = setInterval(function () {
            ledOn = !ledOn;
            gpio.led(ledOn);
        }, 1000);
    }

    global.app = {
        stop: function () {
            if (timer !== null) clearInterval(timer);
            if (ledAvailable) gpio.led(false);
        }
    };
})(this);
```

Do not use obsolete `defer`, `waitFor`, `wifi.async`, or callback-style interrupt helpers documented for older firmware versions.
