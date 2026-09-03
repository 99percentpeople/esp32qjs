# Runtime API and execution rules

## Runtime boundaries

ESP32QJS runs one active vendored MQuickJS runtime for trusted device
application code. Build Context `flash_data/**/*.js`, installed application
scripts, and interactive evaluation share the same ES5-like language profile
and feature-gated ESP32QJS globals. Native modules execute in ESP-IDF/FreeRTOS
and publish their JavaScript surface through explicit bindings. Check
`sys.info.features` before using an optional global.

## Language

The device source profile uses `var`, ordinary functions and function
expressions, arrays, plain objects, `try`/`catch`/`finally`, and the documented
built-ins. Script composition uses `load()` and `framework.load()`.
Asynchronous and binary work uses the documented `Future`, `EventQueue`, and
`ByteView` APIs. Generate device code entirely from this profile and the
globals present in the current Artifact documentation index.

### Built-in objects and methods

The vendored standard library provides this concrete built-in surface:

- `Object`: `defineProperty`, `getPrototypeOf`, `setPrototypeOf`, `create`, and
  `keys`; instances provide `hasOwnProperty()` and `toString()`.
- `Function` instances: `call()`, `apply()`, `bind()`, `toString()`, `length`,
  and `name`.
- `Number`: `parseInt`, `parseFloat`, numeric constants, and the instance
  methods `toExponential()`, `toFixed()`, `toPrecision()`, and `toString()`.
- `String`: `fromCharCode` and `fromCodePoint`; instances provide `charAt()`,
  `charCodeAt()`, `codePointAt()`, `slice()`, `substring()`, `concat()`,
  `indexOf()`, `lastIndexOf()`, `match()`, `replace()`, `replaceAll()`,
  `search()`, `split()`, `toLowerCase()`, `toUpperCase()`, `trim()`,
  `trimStart()`, `trimEnd()`, `repeat()`, and `toString()`.
- `Array`: `isArray`; instances provide `concat()`, `push()`, `pop()`,
  `join()`, `reverse()`, `shift()`, `unshift()`, `slice()`, `splice()`,
  `indexOf()`, `lastIndexOf()`, `every()`, `some()`, `forEach()`, `map()`,
  `filter()`, `reduce()`, `reduceRight()`, `sort()`, and `toString()`.
- `Math`: `min`, `max`, `sign`, `abs`, `floor`, `ceil`, `round`, `sqrt`,
  `sin`, `cos`, `tan`, `asin`, `acos`, `atan`, `atan2`, `exp`, `log`, `pow`,
  `random`, `imul`, `clz32`, `fround`, `trunc`, `log2`, and `log10`, together
  with the standard `E`, logarithm, `PI`, and square-root constants.
- `JSON`: `parse()` and `stringify()`.
- `RegExp`: construction from a pattern and flags; instances provide
  `lastIndex`, `source`, `flags`, `exec()`, and `test()`. String `match()`,
  `replace()`, `replaceAll()`, `search()`, and `split()` accept regular
  expressions.
- `Date`: `new Date(...)`, `Date.now()`, and instance `valueOf()`. Use the
  millisecond number returned by `valueOf()` when serializing device time.
- `ArrayBuffer` and `Uint8ClampedArray`, signed and unsigned 8/16/32-bit typed
  arrays, plus `Float32Array` and `Float64Array`. Typed arrays provide
  `length`, `byteLength`, `byteOffset`, `buffer`, `join()`, `toString()`,
  `subarray()`, and `set()`.
- `Error`, `EvalError`, `RangeError`, `ReferenceError`, `SyntaxError`,
  `TypeError`, `URIError`, and `InternalError`; error values expose `name`,
  `message`, `stack`, and `toString()`.
- Global `parseInt()`, `parseFloat()`, `eval()`, `isNaN()`, and `isFinite()`,
  plus `console.log()` and monotonic `performance.now()`.

ESP32QJS globals such as `sys`, `Future`, `EventQueue`, and `ByteView` extend
this built-in surface according to the selected native features.

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

Device-bound `.js` source is validated with the exact vendored MQuickJS parser
before installation or execution. `JAVASCRIPT_SYNTAX_ERROR` identifies source
that does not match this runtime dialect.

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
- Pass finite API timeouts. `sys.withTimeout()` adds an outer wall-clock bound;
  cancellation remains cooperative.

## `EventQueue`

Native event sources use bounded `EventQueue` handles. `EventQueue` cannot be
constructed directly.

- `receive(timeoutMs?)`: synchronously wait for the next event or return `null`
  at timeout.
- `close()`: stop delivery and release the queue.
- Concrete sources may add operations, while repeated input consistently uses
  `receive(timeoutMs?)`. WebSocket and USB Serial handles also expose `send()`
  and `status()`.

Always close queues that persistent code owns.

## Timers

- `setTimeout(fn, delayMs)` / `clearTimeout(handle)`
- `setInterval(fn, intervalMs)` / `clearInterval(handle)`

Store timer IDs and clear them during application cleanup.

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
  the startup failure latch so the next boot evaluates normal workspace startup
  policy. Operator lifecycle code owns this setting.
- `sys.millis()` and `sys.micros()` return monotonic uptime counters.
- `sys.freeHeap()` returns available heap bytes.
- `sys.randomHex(byteLength)` returns 1-64 random bytes as lowercase
  hexadecimal text.
- `sys.config()` returns a fresh plain object containing every immutable
  hardware-profile constant. `sys.config(key)` reads one constant and returns a
  string, number, boolean, or `undefined`. Registered framework keys use the
  `ESP32QJS_*` prefix; application-specific constants should use another prefix
  such as `APP_*`.
- `sys.withTimeout(timeoutMs, fn)` shortens the active wall-clock deadline for
  `fn`; nested calls can only shorten the current deadline.

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
