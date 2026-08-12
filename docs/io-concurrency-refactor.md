# I/O Concurrency Refactor

Status: proposed pre-v1 redesign. This document supersedes the callback and
`*.async` recommendations in `api-stability-plan.md`.

## Decision

The public framework should expose one synchronous form for each I/O operation.
I/O modules must not add callback overloads or module-specific asynchronous
namespaces such as `wifi.async` and `http.async`.

True asynchronous and concurrent use should be composed through one generic
`task` facility. Internally, synchronous calls and `task.start(...)` must use
the same native operation implementation:

```text
wifi.scan() ---------------------> native I/O operation --+
                                                         +--> scheduler --> result
task.start(wifi.scan, wifi, []) -> native I/O operation --+
```

This is a single-runtime design. It must not create a second JavaScript runtime
or execute one MQuickJS context from multiple FreeRTOS tasks.

## Required semantics

“Synchronous without freezing the runtime” means:

- the calling JavaScript expression receives the final value or throws;
- native waits never busy-spin or hold the JavaScript task in one long driver
  wait;
- the scheduler continues advancing already-started native operations, timers,
  event sources, stop requests, watchdog service, and deadlines at bounded safe
  points;
- `task.start(...)` can start several native I/O operations before any caller
  waits for them;
- all JavaScript execution still happens serially on one MQuickJS context.

It does **not** mean arbitrary JavaScript executes in parallel. A synchronous
call cannot both return a final value immediately and suspend an arbitrary JS
stack unless the engine provides continuations, generators, or `async`/`await`.
The vendored MQuickJS dialect provides none of those. Native I/O concurrency is
therefore the supported concurrency boundary.

## Current API census

The ROM property tables currently declare 223 framework C-function entries,
plus the five inherited globals `print`, `gc`, `load`, `setTimeout`, and
`clearTimeout`. Constructors and property getters are not included in this
count.

| Area | Current callable entries | Notes |
| --- | ---: | --- |
| Core data and streams | 25 | Headers, Request/Response, Deferred, Stream, ByteView |
| Display primitives | 47 | Buffer, command buffer, span source, font loading |
| Filesystem/runtime | 23 | `fs`, `framework`, `nvs`, `sys` |
| Peripheral I/O | 75 | GPIO, LEDC, ADC, DAC, I2C, SPI, UART |
| Connectivity | 44 | USB Serial, socket, WebSocket, Wi-Fi, HTTP client/server |
| Additional globals | 9 | Extra helpers merged into the base global object |

The current asynchronous public surface is fragmented:

- two module-specific namespaces exist:
  - `wifi.async.connect`, `wifi.async.scan`;
  - `http.async.fetch`, `http.async.cancel`;
- eleven other logical methods register callbacks outside those namespaces:
  - `gpio.attachInterrupt`;
  - `usbSerial.open`;
  - `websocketClient.open`;
  - the eight `HttpServer` route methods;
- callback result conventions differ:
  - Wi-Fi and HTTP use `(result, error)`;
  - USB Serial uses `(error, data)`;
  - GPIO and WebSocket use one event object;
  - HTTP route handlers synchronously return a response.

Generic callback-related helpers also exist: `defer`, `waitFor`, timers, and
`sys.withTimeout`. UI render functions and driver factories accept callbacks,
but those callbacks execute synchronously as lexical scopes or factories and
are not asynchronous I/O APIs.

## Current wait behavior

The implementation does not yet meet the required synchronous semantics.

| API family | Current implementation | JavaScript event dispatch while waiting |
| --- | --- | --- |
| `waitFor` / `Deferred.wait` | Polls the runtime until a deferred settles | Yes |
| `sleep` / `delay` | FreeRTOS delay in cooperative slices | No |
| `wifi.connect` | Event-group waits in slices | No |
| `wifi.scan` | Blocking ESP-IDF scan call | No |
| synchronous HTTP fetch | Worker task plus sliced semaphore wait | No |
| socket connect/send/recv | Nonblocking fd plus sliced `select` polling | No |
| queued SPI transfers | Short queue/result waits | No |
| I2C and UART | Direct driver calls with driver timeouts | No |
| filesystem and NVS | Direct LittleFS/NVS calls | No |

`esp32_mquickjs_cooperative_delay()` currently checks stop requests, watchdogs,
and scoped deadlines, but it does not call `esp32_mquickjs_poll()`. Consequently
the CPU and FreeRTOS remain healthy during several waits, while timers and
JavaScript-visible completion events remain pending until the synchronous call
returns.

The current truly in-flight implementations are useful foundations:

- HTTP already performs requests in worker tasks and supports four slots;
- asynchronous Wi-Fi already uses ESP events and queues;
- socket descriptors are nonblocking;
- SPI already exposes queued transactions;
- GPIO, USB Serial, WebSocket, and HTTP server requests already bridge events
  back to the JS task.

These backends should be retained but hidden behind one operation abstraction.

## Public target API

### I/O modules

Every module exposes only direct, synchronous operations:

```js
var accessPoints = wifi.scan();
var response = fetch("https://example.com", { timeoutMs: 5000 });
var bytes = port.read(64, 1000);
var chunk = socket.tcp.recv(socketId, 1024, 1000);
```

No I/O method accepts an optional callback. No I/O module exposes an `async`
property. A timeout changes only the deadline; it never changes the return
type or execution model.

### Generic tasks

Proposed minimal surface:

```ts
interface TaskStatus {
  state: "pending" | "fulfilled" | "rejected" | "cancelled";
}

interface Task<T> {
  status(): TaskStatus;
  wait(timeoutMs?: number): T;
  cancel(): boolean;
}

interface TaskModule {
  start<T>(fn: Function, thisValue: unknown, args?: ArrayLike<unknown>): Task<T>;
  all<T>(tasks: ArrayLike<Task<T>>): Task<T[]>;
  race<T>(tasks: ArrayLike<Task<T>>): Task<{ index: number; value: T }>;
  sleep(ms: number): Task<void>;
  timeout<T>(inner: Task<T>, timeoutMs: number): Task<T>;
}

declare var task: TaskModule;
```

The array argument keeps call sites compatible with the ES5-like dialect:

```js
var scanTask = task.start(wifi.scan, wifi, []);
var fetchTask = task.start(fetch, globalThis, [
  "https://example.com",
  { timeoutMs: 5000 }
]);
var values = task.all([scanTask, fetchTask]).wait(10000);
```

`task.start` accepts only native functions registered as asynchronous-capable
operations. It must reject arbitrary JavaScript functions instead of implying
that JavaScript can run on another thread. Bound functions should initially be
rejected; callers pass an explicit `thisValue`.

`Task.wait()` is the one generic synchronous wait primitive. Direct synchronous
I/O wrappers use the same scheduler internally, making this equivalence true:

```text
wifi.scan()
    == task.start(wifi.scan, wifi, []).wait()
```

### Event sources

Long-lived or repeated input should use a generic event-source shape rather
than module-owned callbacks:

```ts
interface EventSource<T> {
  next(timeoutMs?: number): T | null;
  close(): boolean;
}
```

Recommended migrations:

```js
var interrupts = gpio.watch(pin, gpio.CHANGE);
var event = interrupts.next(1000);

var serial = usbSerial.open({ maxFrameBytes: 8192 });
var frame = serial.recv(1000);
```

`EventSource.next` must itself be registered with `task.start`, so several
sources can be awaited concurrently. If callback subscription is desirable,
it belongs only on the generic task layer, for example
`task.subscribe(source, callback)`. The source module remains callback-free.

### Callback policy

After migration, asynchronous callbacks are allowed only on generic scheduling
tools such as `task.subscribe` or timer helpers. The following callbacks remain
valid because they execute synchronously before their API returns:

- `sys.withTimeout(timeout, callback)` scope callbacks;
- UI layout/render scope callbacks;
- driver and handler factories that are invoked immediately.

No compatibility aliases should preserve `wifi.async`, `http.async`, or old
callback overloads when the v1 surface switches.

## Native operation architecture

Each potentially waiting native method should have one private descriptor:

```c
typedef struct {
    bool (*start)(JSContext *ctx,
                  JSValue this_value,
                  int argc,
                  JSValue *argv,
                  esp32_mquickjs_operation_t **out_operation);
    esp32_mquickjs_operation_state_t (*poll)(esp32_mquickjs_operation_t *operation);
    JSValue (*finish)(JSContext *ctx, esp32_mquickjs_operation_t *operation);
    bool (*cancel)(esp32_mquickjs_operation_t *operation);
    void (*destroy)(esp32_mquickjs_operation_t *operation);
} esp32_mquickjs_operation_vtable_t;
```

The standard-library generator should register the native function index and
operation descriptor together. `task.start(fn, thisValue, args)` resolves that
registration. A small MQuickJS helper may be needed to safely obtain the index
of a native C function; the task layer must not depend on private `JSValue` bit
layouts directly.

An operation follows these rules:

1. Validate arguments and copy all worker-owned input while on the JS task.
2. Do not use a `JSValue`, JS string pointer, or JS object from a worker task.
3. Start hardware, a bounded worker-pool item, or a nonblocking descriptor.
4. Send only native completion data to the runtime queue.
5. Construct the final JS value in `finish()` on the JS task.
6. Make cancellation and destruction generation-checked and idempotent.
7. Restore or release every resource during runtime shutdown.

The scheduler should use a bounded slot table rather than one unbounded task per
request. Suggested initial limits are eight operations overall and the existing
module-specific limits underneath it. Fair polling must cap completions handled
per pass.

## Backend strategy by module

| Module | Recommended backend |
| --- | --- |
| HTTP client | Reuse the current worker implementation; merge sync/async slots into generic operations |
| Wi-Fi | Use nonblocking ESP events for both connect and scan; remove blocking scan mode |
| socket | Register nonblocking fd readiness with the scheduler; keep TCP/UDP framing-free |
| SPI | Reuse queued transactions and completion polling; serialize by bus/device where required |
| UART | Use the UART event/ring-buffer path for reads and TX completion operations |
| I2C | Run blocking ESP-IDF transactions in the bounded worker pool unless an async driver is available |
| filesystem/NVS | Use the bounded worker pool and serialize flash/filesystem mutations |
| GPIO/ADC/DAC/LEDC | Keep immediate register/oneshot calls direct; expose repeated events as event sources |
| USB Serial | Replace `open(callback)` with a handle plus `recv()`/event source |
| WebSocket | Prefer a JS framework library above `socket`; if retained natively, expose a handle plus `recv()` |
| HTTP server | Replace route callbacks with declarative routes and a request event source |

Hardware concurrency remains resource-dependent. Two HTTP requests may run in
parallel, while two I2C operations on one bus must be serialized. `task.all`
means concurrent scheduling, not a guarantee that a peripheral can physically
execute requests simultaneously.

## HTTP server target

HTTP route callbacks are application execution hidden inside an I/O module, so
they should also leave the native API. A low-level target can be:

```js
var server = http.server({ port: 8080 });
server.route("GET", "/ping");
server.start();

var request = server.receive(1000);
if (request !== null) {
  server.respond(request, Response.text("pong"));
}
```

A higher-level JS library can implement routing and middleware. A generic
`task.subscribe(server.requests(), handler)` helper may provide callback style
without putting callbacks back into `http`.

## API cleanup found during the audit

These are independent correctness fixes to include with the refactor:

- `fs.setRoot()` and the `framework` global exist in firmware and documentation
  but are missing from `types/esp32qjs-c-api.d.ts`;
- `WebSocketClientOpenOptions` declares `autoReconnect` twice;
- `socket.get_max_message_bytes()` is inconsistent with the otherwise
  camel-cased API and incorrectly suggests that TCP has message boundaries;
- socket payloads are strings while UART/SPI/I2C use `ByteSource`; the generic
  socket layer should use `ByteSource` for sends and `ByteView` for receives,
  leaving text encoding and NDJSON framing to JS;
- `remoteIp` is also used for DNS hostnames and should be named `remoteHost` or
  documented strictly as an IPv4 address.

Because the project is pre-v1, these should be direct replacements rather than
deprecated aliases.

## Migration phases

### Phase 1: scheduler foundation

- Add `Task`, the bounded operation table, completion queue, cancellation, and
  deadline integration.
- Add native-function-to-operation registration without exposing module policy
  in the core scheduler.
- Make `Task.wait()` pump scheduler completions and timers at safe points.
- Add architecture tests that forbid module `.async` properties in the target
  declarations once migration begins.

### Phase 2: HTTP and Wi-Fi proof

- Convert the existing asynchronous HTTP and Wi-Fi implementations into native
  operation descriptors.
- Make their direct synchronous methods call the descriptor and wait.
- Prove two HTTP tasks complete in approximately the slower request duration,
  not the sum of both request durations.
- Delete `http.async` and `wifi.async` after all first-party JS is migrated.

### Phase 3: socket and peripheral waits

- Convert socket readiness, UART reads/flush, queued SPI, and I2C operations.
- Ensure every wait has an absolute deadline and honors an outer
  `sys.withTimeout()` without extending it.
- Define per-resource serialization and BUSY/queue behavior.

### Phase 4: event APIs

- Introduce `EventSource`.
- Replace GPIO interrupt, USB Serial, native WebSocket, and HTTP server callbacks.
- Move higher-level dispatch loops into JS libraries composed through `task`.

### Phase 5: storage and cleanup

- Move potentially long filesystem/NVS work to the bounded worker pool.
- Remove `Deferred`, `waitFor`, module callback state, duplicate async workers,
  and compatibility error messages referring to removed APIs.
- Regenerate declarations and rewrite runnable documentation examples.

## Acceptance tests

The refactor is complete only when all of the following hold:

- no public I/O method accepts a callback and no public module exposes `.async`;
- direct synchronous I/O permits a timer and an already-started Task to make
  progress during the wait;
- `task.all` demonstrates overlapping native I/O rather than sequential timing;
- cancellation, timeouts, stale handles, resource close, and runtime shutdown
  are deterministic and leak-free;
- all callbacks that remain are invoked only by generic task/timer tooling or
  are documented synchronous scope callbacks;
- a static architecture test rejects direct module-level `JS_Call(...)` and
  callback fields outside the task core;
- C3 and S3 memory tests cover the maximum configured operation count;
- full Python, MQuickJS syntax, firmware build, and board-backed network tests
  pass on both supported boards.
