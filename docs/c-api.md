# C API Reference

This document covers the APIs exported directly by the firmware runtime.

## Global Helpers

- `help()`
  Print the location of these API documents.
- `print(...values)`
  Write values to the runtime's standard-output sink. Normal profiles use the
  serial console; the headless Agent profile forwards a bounded copy to its
  host diagnostics cache so protocol byte streams are not polluted.
- `gc()`
  Run the JavaScript garbage collector explicitly for diagnostics. Normal
  applications do not need to call it: the runtime accounts for JavaScript-owned
  native allocations and collects them at scheduler safe points when an
  execution turn completes, native allocation debt grows, or internal memory is
  under pressure.
- `fetch(input, options?)`
  Run an HTTP request through a hidden native Future and return a `Response`.
- `load(path)`
  Evaluate a script from the immutable volume currently stored in global `fs`.
  The initial volume is rooted at `/littlefs`; applications can install another
  mounted volume with `globalThis.fs = fs.volume(path)`.
- `framework.load(path)`
  Evaluate a bundled framework script below `/littlefs/_sys`, regardless of
  the active application root. Nested `load(...)` calls made while evaluating the
  framework module also remain on the system partition.
- `sleep(ms)` / `delay(ms)`
  Wait for `ms` milliseconds while the Future scheduler, timers, deadlines,
  watchdog, and stop requests continue to advance.

Startup behavior:

- If `/littlefs/index.js` exists, it is loaded automatically before the first `js>` prompt appears.
- `index.js` is the single startup entry point. Keep it empty when you want the board to boot into the REPL, or use it to `load(...)` scripts, drivers, and app code.
- This is the recommended place for board startup logic such as loading a panel entry point (`framework.load("display/st7789.js")`), `framework.load("ui.js")`, and `wifi.connect(...)`.
- Optional examples can live under `demo/` and be started manually, for example `load("demo/display_perf.js")`.

Examples:

```js
print("hello");
gc();
sleep(50);
wifi.connect("your-ssid", "your-password", 10000);
sys.time.sync({ servers: ["pool.ntp.org"], timeoutMs: 10000 });
print(fetch("https://example.com").status);
load("demo/display_perf.js");
print(Future.call(function () { return 123; }).wait(1000));
```

Example `index.js`:

```js
print("[startup] boot script running");
framework.load("display/st7789.js");
framework.load("ui.js");
wifi.connect("your-ssid", "your-password", 10000);
sys.time.sync({ servers: ["pool.ntp.org"], timeoutMs: 10000 });
```

## Futures

- `Future.call(fn, thisValue?, args?)`
  Queue a callable without invoking it before return. It starts at the next
  scheduler idle point and remains runtime-owned through settlement. For a
  registered native driver, the call synchronously validates and captures
  immutable arguments, generations, buffers, and leases before returning, but
  does not start I/O or wait on hardware. Capture errors produce an already
  rejected Future.
- `Future.all(futures)`
  Fulfill with results in input order. Other inputs are not cancelled when one fails.
  The combinator handles rejection from every attached input.
- `Future.race(futures)`
  Settle as `{ index, value }` from the first input without cancelling the rest.
  All inputs retain a rejection handler after the race settles, so a losing
  input that rejects later is not reported as unobserved.
- `Future.sleep(ms)`
  Return a timer-backed Future.
- `Future.timeout(future, timeoutMs)`
  Apply an operation deadline and cancel the input if it expires. Input
  rejection is handled by the timeout wrapper even when it settles first.
- `future.status()` / `future.wait(timeoutMs?)` / `future.cancel()`
  Inspect, cooperatively wait for, or cancel a Future. A wait timeout does not
  cancel the operation. Cancelling queued work settles it immediately. A
  running native operation may reject cancellation (`false`), confirm it
  immediately (`true` and `"cancelled"`), or accept a request (`true`) while
  remaining `"pending"` until its driver reports that teardown is complete.
- `future.map(fn)`
  Transform a fulfilled value without flattening a returned Future. Rejections
  and cancellations propagate without invoking `fn`.
- `future.flatMap(fn)`
  Chain an operation whose callback returns another Future. Returning any other
  value rejects the chained Future.

Queued and pending operations occupy the bounded public Future table. On
settlement, status and result move to the JavaScript handle so the scheduler
slot is immediately reusable. Synchronous native adapters use a separate
reserved slot pool, keeping transport and cancellation paths responsive when
public Future capacity is full.

A native driver may expose a non-null resource key. Operations with equal keys
enter a bounded FIFO lane: the core captures their arguments and leases at
`Future.call()` time, but starts only one operation for that resource at a
time. Waiting operations do not occupy worker tasks, while drivers with
different keys may run concurrently. The per-resource waiting limit is
`CONFIG_ESP32_MQUICKJS_FUTURE_RESOURCE_LANE_QUEUE_LEN`; exceeding it rejects
the new Future with `Future resource lane queue is full`. Cancelling a queued
Future releases its captured state without starting the driver.

Finite synchronous hardware operations are cooperative scheduler yield points.
While one is waiting for an interrupt, readiness event, or timeout, timer
callbacks and ready Futures may run before the hardware method returns. This is
single-threaded cooperative re-entry, not parallel JavaScript execution. Idle
application jobs are not started from a nested synchronous wait.

Examples:

```js
var scan = Future.call(wifi.scan, wifi, []);
var request = Future.call(fetch, globalThis, ["https://example.com"]);
var values = Future.all([scan, request]).wait(10000);
print(values[0].length, values[1].status);
```

## Event queues

`EventQueue` represents a bounded stream of externally produced events. Each
queue supports at most one pending receiver.

- `queue.receive(timeoutMs?)`
  Return the next event. With no argument it waits cooperatively without a
  deadline; `0` performs a non-blocking check. A closed and drained queue
  returns `null`.
- `queue.stats()`
  Return `{ open, queued, capacity, dropped, receiverPending }` for this queue.
  `dropped` is cumulative for the queue lifetime.
- `queue.close()`
  Stop the source and wake a pending receiver. Closing does not discard events
  already queued; they remain receivable until the queue is drained.

Cancelling a Future created from `queue.receive` does not close the source.
Each native producer declares whether overflow drops the newest or oldest
event, and events that own native payloads are released on drop and finalizer
drain paths.

## Timers

- `setTimeout(fn, ms)`
  Run `fn` once after `ms` milliseconds and return an opaque timer handle.
- `clearTimeout(handle)`
  Cancel a timeout only when the handle still identifies that timer. A stale
  handle cannot cancel a newer timer that reused the same native slot.
- `setInterval(fn, ms)`
  Run `fn` repeatedly and return an opaque timer handle. Periods below
  `CONFIG_ESP32_MQUICKJS_MIN_INTERVAL_MS` are clamped to that configured
  minimum. An interval is canceled automatically if its callback throws or
  exceeds the JavaScript callback deadline.
- `clearInterval(handle)`
  Cancel an interval using the same generation-checked handle semantics.

Example:

```js
var handle = setInterval(function () { print("tick"); }, 500);
clearInterval(handle);
```

## `fs` Module

All `fs` operations are restricted to the immutable root captured by their
`FsVolume` receiver.

- `fs.ROOT`
  Read-only root of this volume. The initial global volume uses `"/littlefs"`.
- `fs.volume(root)`
  Create an immutable `FsVolume` for an exact mounted filesystem root. The
  framework validates the mount only; applications own policy about which
  volume to retain or install globally.
- `fs.info()`
  Return live LittleFS capacity for this volume as
  `{ root, totalBytes, usedBytes, freeBytes }`.
- `fs.watch()`
  Return an `EventQueue` for filesystem changes under this volume. Events are
  `{ type, path }`, with `toPath` on `rename`; `type` is `write`, `remove`,
  `rename`, or `mkdir`. Paths are relative to the root captured when the queue
  is created. Independent watchers may coexist; close each queue when it is no
  longer needed. Use
  `Future.call(changes.receive, changes, [timeoutMs])` to wait without blocking
  the JavaScript runtime.
- `fs.list(path = ".")`
  Return an array of entries for a directory.
- `fs.stat(path)`
  Return `{ name, path, isDir, size }` for a path.
- `fs.exists(path)`
  Return `true` if the path exists.
- `fs.readText(path, options?)`
  Read a trusted text file with an allocation bound. `options.maxBytes`
  defaults to and cannot exceed
  `CONFIG_ESP32_MQUICKJS_FS_READ_TEXT_MAX_BYTES` (65536 by default). Files over
  the selected bound throw an error with
  `{ code: "FS_READ_LIMIT_EXCEEDED", path, maxBytes, actualBytes }`; consume
  larger files through `Stream`. This convenience does not validate encoding,
  so applications that accept arbitrary bytes must use `"rb"` and own their
  decoding policy.
- `fs.open(path, mode?)`
  Open a file stream. Supported modes are `r`, `rb`, `w`, `wb`, `a`, `ab`, `r+`, `w+`, and `a+`.
  File open and file-stream `read`, `write`, `flush`, `seek`, and `close` use
  native Future drivers, so `Future.call(...)` does not block the JavaScript
  runtime task on VFS calls. Direct calls cooperatively wait on the same driver.
- `fs.writeText(path, text)`
  Atomically replace a text file and return the number of bytes written. The
  implementation writes and syncs a same-directory temporary file, closes it,
  then renames it over the destination. Watchers receive one `write` event
  only after the rename commits.
- `fs.appendText(path, text)`
  Append text and return the number of bytes written. Append is synced before
  returning but does not promise power-loss atomicity.
- `fs.mkdir(path)`
  Create one directory level.
- `fs.rename(fromPath, toPath)`
  Rename a file or directory.
- `fs.remove(path)`
  Remove a file or an empty directory.

Example:

```js
var changes = fs.watch();
var systemFs = fs;
var dataFs = fs.volume("/littlefs");
var nextChange = Future.call(changes.receive, changes, [5000]);
fs.writeText("notes.txt", "hello\n");
var pending = Future.call(dataFs.readText, dataFs, ["notes.txt"]);
print(JSON.stringify(nextChange.wait()));
print(JSON.stringify(fs.info()));
print(fs.readText("notes.txt"));
print(fs.stat("notes.txt"));
print(JSON.stringify(fs.list(".")));
fs.rename("notes.txt", "notes-old.txt");
fs.remove("notes-old.txt");
changes.close();
pending.wait();
```

## Secondary LittleFS

Profiles may enable `CONFIG_ESP32QJS_SECONDARY_LITTLEFS` and configure its
partition label and base path. The runtime mounts that partition through the
same generic LittleFS API but does not assign it any business meaning or select
it automatically. Application bootstrap code can create a volume after loading
its system services, retain both handles, and optionally assign the application
volume to `globalThis.fs`. Bundled `_sys` modules remain available through
`framework.load(path)`:

```js
var systemFs = fs;
var dataFs = systemFs.volume("/data");
globalThis.fs = dataFs;
```

## `nvs` Module

`nvs` is an optional bounded string store backed by the default ESP-IDF NVS
partition. Enable it with `CONFIG_ESP32_MQUICKJS_FEATURE_NVS`. This module does
not enable NVS encryption or provision encryption keys; `nvs.status()` makes
the build's encryption state explicit so product policy can reject unsafe
secret storage.

- `nvs.MAX_VALUE_BYTES`
  Maximum UTF-8 value size, currently `2048` bytes.
- `nvs.getString(namespace, key)`
  Return the stored string or `null` when the namespace/key does not exist.
- `nvs.setString(namespace, key, value)`
  Commit one string atomically and return its UTF-8 byte length. Values cannot
  contain NUL. Updates use `NVS_READWRITE_PURGE`, which asks ESP-IDF to purge
  the prior flash value rather than merely marking it deleted.
- `nvs.erase(namespace, key)`
  Purge one key and return whether it existed.
- `nvs.clear(namespace)`
  Purge every key in one namespace and return whether the namespace existed.
- `nvs.status()`
  Return `{ initialized, encrypted, maxValueBytes }`.

Namespaces and keys are 1–15 ASCII letters, digits, `_`, or `-`. The binding
cannot access arbitrary partitions and does not provide iteration, numeric
values, blobs, raw security keys, partition erase, or encryption-key
provisioning.

```js
if (!nvs.status().encrypted) {
  print("development NVS is plaintext");
}
nvs.setString("my_app", "mode", "quiet");
print(nvs.getString("my_app", "mode"));
nvs.erase("my_app", "mode");
```

This is a persistence and wear-leveling boundary, not an authorization
boundary. All application JavaScript is trusted; code with access to `nvs` can
read any valid namespace/key it knows. Production applications storing secrets
must configure ESP-IDF encrypted NVS and their device key lifecycle explicitly.

## `Stream` Type

`fs.open()` returns a `Stream`. `Response.body`, `Request.body`, and
`Response.stream(...)` also use the same stream interface. A `ByteView` is a
read-only native byte view; `toArray()` makes an explicit JavaScript copy. A
`ByteSpanSource` is a retained, one-shot producer of native spans. Its
`close()` method is idempotent and releases producer-owned resources. Owned
`ByteView` values also have an idempotent `close()`; call it after the last
consumer or `toArray()` conversion to release native storage deterministically.
Opening a `ByteSpanSource` acquires a read lease until the consumer finishes.
The current span remains valid until the consumer asks for the next span or
closes the iterator, so cooperative UART and USB consumers do not make a
defensive full-span copy. Calling `close()` or changing a reusable source with
`setRect()` while it is leased throws a busy error.

- `Stream.SEEK_SET`
- `Stream.SEEK_CUR`
- `Stream.SEEK_END`
  Seek constants for `stream.seek(...)`.

Stream instance shape:

- `kind`
  Stream kind. File streams currently report `"file"`.
- `path`
  Full LittleFS path for file-backed streams, for example `"/littlefs/notes.txt"`.
- `mode`
  The mode string passed to `fs.open(...)`.
- `readable`
- `writable`
- `read(size?)`
  Read up to `size` bytes. Text modes are a trusted-text convenience and do not
  validate encoding or preserve character boundaries. Use a binary mode such
  as `"rb"` for arbitrary data and application-owned decoding; binary reads
  return an owned native `ByteView`. Returns `null` at EOF. Default chunk size
  is `1024`. Call `toArray()` only when JavaScript needs to inspect or parse a
  `ByteView`, and close that view in `finally`.
- `write(value)`
  Write and return the exact byte count. Text file modes accept strings.
  Binary file modes accept `ByteView`, array-like byte data, or a retained
  `ByteSpanSource`; the Future driver opens a source lease during capture and
  writes one span at a time without materializing the complete source. No
  newline is added.
- `flush()`
- `close()`
  Successful `flush()` or `close()` publishes one filesystem `write` event if
  the stream has committed changes; individual spans do not publish events.
- `seek(offset, whence?)`
  Move the file cursor and return the new position.
- `tell()`
  Return the current file cursor position.
- `eof()`
  Return `true` once the stream reached EOF.

Example:

```js
var stream = fs.open("_sys/display.js", "r");
print(stream.tell());
print(JSON.stringify(stream.read(32)));
stream.seek(0, Stream.SEEK_SET);
stream.close();
```

## `Headers`, `Request`, and `Response`

- `new Headers(init?)`
  Create a header collection from a plain object or another `Headers`.
- `new Request(input, init?)`
  Create a request from a URL string or another `Request`.
- `new Response(body?, init?)`
  Create a response from a string, `Stream`, `ByteView`, `ByteSpanSource`, or
  omitted body.
- `Response.text(text, init?)`
- `Response.json(value, init?)`
- `Response.stream(stream, init?)`
- `Response.bytes(body, init?)`
  Create a response from a `ByteView` or `ByteSpanSource` without UTF-8
  conversion.

`Headers` methods:

- `headers.get(name)`
- `headers.set(name, value)`
- `headers.has(name)`
- `headers.delete(name)`
- `headers.entries()`
- `headers.toObject()`

`Request` shape:

- `method`
- `url`
- `path`
- `queryString`
- `query`
- `route`
  The matched route pattern as a string.
- `mountPath`
  The mounted path prefix computed by the router, primarily for middleware.
- `relativePath`
  The path after removing the mounted prefix, primarily for file-serving middleware.
- `headers`
  A `Headers` object.
- `body`
  A `Stream`.
- `text()`
- `bytes(maxBytes?)`
- `json()`

`Response` shape:

- `ok`
- `status`
- `statusText`
- `url`
- `headers`
  A `Headers` object.
- `body`
  A `Stream`.
- `text()`
- `bytes(maxBytes?)`
- `json()`

Body consumption notes:

- `Request.text()` / `Request.bytes()` / `Request.json()` read the full request
  body and consume the underlying stream. `bytes()` returns an owned
  `ByteView` and enforces its optional bound.
- `Response.text()` / `Response.bytes()` / `Response.json()` do the same for a
  response body.
- If you read directly from `request.body` or `response.body`, close the stream manually when you are done.

Examples:

```js
var request = new Request("https://example.com", {
  method: "POST",
  headers: { "content-type": "application/json" },
  body: JSON.stringify({ hello: "world" }),
});

var response = fetch(request);
print(response.status, response.ok);
print(response.text().length);

var headers = new Headers({ "content-type": "text/plain; charset=utf-8" });
print(headers.get("content-type"));
```

## `i2c` Module

- `i2c.DEFAULT_SDA`
  Default SDA pin from Kconfig.
- `i2c.DEFAULT_SCL`
  Default SCL pin from Kconfig.
- `i2c.DEFAULT_FREQ_HZ`
  Default bus speed, `400000`.
- `i2c.DEFAULT_TIMEOUT_MS`
  Default transfer timeout in milliseconds.
- `i2c.openBus(options?)`
  Open an I2C master controller and return an `I2CBus`. `options` can include `{ sda, scl, freqHz, timeoutMs, internalPullup }`. The legacy `i2c.open()` entry point is not part of the v1 contract.

`I2CBus` methods:

- `bus.status()`
  Return `{ opened, controller, sda, scl, freqHz, timeoutMs, internalPullup, deviceCount }`.
- `bus.close()`
  Close the bus handle. After closing, the `I2CBus` instance becomes stale and its other methods throw. If a caller forgets to close it, GC finalization will also release the native handle eventually, but explicit `close()` remains the intended lifecycle boundary.
- `bus.scan()`
  Probe `0x03..0x77` and return an array of 7-bit device addresses.
- `bus.openDevice({ address, freqHz?, timeoutMs? })`
  Create an `I2CDevice` with a persistent ESP-IDF device handle. The device inherits the bus frequency and timeout unless overridden.

`I2CDevice` methods:

- `device.status()`
  Return `{ opened, controller, address, freqHz, timeoutMs }`.
- `device.close()`
  Close the device handle. Close devices before their owning bus.
- `device.write(data)`
  Execute one transaction containing one `ByteSource` and return its byte count.
- `device.writeSegments(segments)`
  Send all native segments inside one START/STOP transaction without joining them into a JavaScript array. It returns the total byte count.
- `device.writeBatch(chunks)`
  Execute chunks as independent ordered transactions and return `{ chunks, bytes, totalUs }`.
- `device.read(length)`
  Read `length` bytes into an owned `ByteView`. Close the view after use.
- `device.writeRead(writeData, readLength)`
  Write bytes, then read bytes in one transaction and return an owned
  `ByteView`.

`scan()`, `write()`, `writeSegments()`, `writeBatch()`, `read()`, and
`writeRead()` are native Future operations. Operations for the same controller
use one bounded FIFO resource lane; separate controllers may progress
independently. I2C master readiness still uses `gpio.watch()` rather than a
bus-specific watcher.

Example:

```js
var bus = i2c.openBus({ sda: 5, scl: 6, freqHz: 400000 });
var device;
print(JSON.stringify(bus.status()));
print(JSON.stringify(bus.scan())); // [60] for an SSD1306 at 0x3c
device = bus.openDevice({ address: 0x3c });
print(device.write([0x00, 0xAF])); // SSD1306 display on
device.close();
bus.close();
```

## `spi` Module

- `spi.HOST_2`
  General-purpose SPI host `2`.
- `spi.HOST_3`
  General-purpose SPI host `3` when the target exposes a second GPSPI controller.
- `spi.DEFAULT_HOST`
  Default SPI host, currently `spi.HOST_2`.
- `spi.DEFAULT_SCLK`
  Board/profile default SCLK GPIO used by `spi.openBus()`.
- `spi.DEFAULT_MOSI`
  Board/profile default MOSI GPIO used by `spi.openBus()`. This can be `-1` for read-only buses.
- `spi.DEFAULT_MISO`
  Board/profile default MISO GPIO used by `spi.openBus()`. This can be `-1` for write-only buses.
- `spi.DEFAULT_CS`
  Board/profile default chip-select GPIO used by `SPIBus.openDevice()`. This can be `-1` when chip-select is managed manually or is device-specific.
- `spi.DEFAULT_FREQ_HZ`
  Default device clock, `1000000`.
- `spi.DEFAULT_QUEUE_SIZE`
  Default per-device queue size.
- `spi.DEFAULT_MAX_TRANSFER_SIZE`
  Default bus max transfer size in bytes.
- `spi.openBus(options?)`
  Open one SPI master bus and return an `SPIBus` instance. Without options it uses `DEFAULT_HOST`, `DEFAULT_SCLK`, `DEFAULT_MOSI`, and `DEFAULT_MISO`. `options` can override `{ host, sclk, mosi, miso, maxTransferSize }`. One JS `SPIBus` maps to one ESP-IDF SPI host; opening the same host twice throws.

`SPIBus` methods:

- `bus.status()`
  Return `{ opened, host, sclk, mosi, miso, maxTransferSize, deviceCount }`.
- `bus.close()`
  Close the bus. Any `SPIDevice` objects opened from that bus become stale. If callers forget to close them, GC finalization still releases the native handles eventually, but explicit `close()` remains the intended lifecycle boundary.
- `bus.openDevice(options?)`
  Open an `SPIDevice` on the bus. `options` can include `{ cs, mode, freqHz, queueSize, csHigh, lsbFirst }`. `cs` defaults to `spi.DEFAULT_CS`, which can be `-1` when chip-select is managed manually in JS or external hardware.

`SPIDevice` methods:

- `device.status()`
  Return `{ opened, host, cs, mode, freqHz, queueSize, csHigh, lsbFirst }`.
- `device.close()`
  Remove the device from its parent SPI bus and make the JS object stale.
- `device.transfer(data)`
  Perform one full-duplex transaction from an array-like sequence
  of bytes or native byte view and return the received bytes as an owned
  `ByteView`.
- `device.write(data)`
  Perform one write-only transaction from an array-like sequence of bytes or native byte view and return the number of transmitted bytes.
- `device.writeChunks(chunks, options?)`
  Queue an array-like list of byte-source chunks for write-only SPI transfers. `options.queueDepth` defaults to `2` and is capped by the device queue size. DMA-capable chunks are queued directly; other chunks are copied into DMA-capable staging buffers. The method returns `{ chunks, bytes, prepUs, queueUs, waitUs, transferUs, totalUs, queueDepth, direct }`.
- `device.writeSource(source, options?)`
  Queue spans from a retained native `ByteSpanSource`, such as `Bitmap.createSpanSource(...)`, without materializing a JavaScript chunk array. SPI treats the source as a generic transport capability; it does not inspect Bitmap internals. `options.queueDepth` and the returned stats object match `writeChunks(...)`.
- `device.read(length, fillByte = 0)`
  Clock `length` bytes and return an owned `ByteView` from MISO. `fillByte`
  controls the dummy value shifted out on MOSI while reading.

All five transaction methods are registered native Future drivers. Direct calls
wait cooperatively; `Future.call()` returns after capture and before hardware
I/O. Operations share one bounded FIFO lane per SPI host, so devices on one
controller stay ordered while separate hosts may progress independently. A
bulk write incrementally consumes its captured chunks or `ByteSpanSource` and
can keep up to the selected queue depth of ESP-IDF DMA transactions in flight.

Example:

```js
var bus = spi.openBus();
var device = bus.openDevice({
  cs: spi.DEFAULT_CS,
  mode: 0,
  freqHz: spi.DEFAULT_FREQ_HZ,
});

print(JSON.stringify(device.transfer([0x9f])));
device.close();
bus.close();
```

For automated loopback validation, run `python scripts/remote.py test --scope js --module spi --loopback`.
The loopback case is opt-in, uses `spi.DEFAULT_*` by default, and reads `testConfig.spiLoopback`
only when you need to override selected fields. For example:

```bash
TEST_JS_CONFIG='{"spiLoopback":{"sclk":1,"mosi":2,"miso":2}}' \
python scripts/remote.py test --scope js --module spi --loopback
```

## `uart` Module

This module exposes cooperative Future-backed TTL UART ports. It is intended
for bounded peripheral exchanges and byte handoff; direct calls wait
cooperatively and `Future.call()` returns control immediately.

- `uart.DEFAULT_PORT`
  Board/profile default UART peripheral.
- `uart.DEFAULT_TX`
  Board/profile default TX GPIO. This can be `-1` when TX must be supplied explicitly.
- `uart.DEFAULT_RX`
  Board/profile default RX GPIO. This can be `-1` when RX must be supplied explicitly.
- `uart.DEFAULT_BAUD`
  Default baud rate, `115200`.
- `uart.DEFAULT_RX_BUFFER_SIZE`
  Default UART driver RX buffer size.
- `uart.DEFAULT_TX_BUFFER_SIZE`
  Default UART driver TX buffer size.
- `uart.DEFAULT_TIMEOUT_MS`
  Default timeout for UART read, write, and flush operations.
- `uart.open(options?)`
  Open one UART port and return a `UARTPort`. `options` can include `{ port, tx, rx, baud, dataBits, parity, stopBits, rxBufferSize, txBufferSize, timeoutMs }`. `parity` is `"none"`, `"even"`, or `"odd"`; `stopBits` is `1`, `1.5`, or `2`. Opening an already-open UART port throws.

`UARTPort` methods:

- `port.status()`
  Return `{ opened, port, tx, rx, baud, dataBits, parity, stopBits, rxBufferSize, txBufferSize, timeoutMs }`.
- `port.close()`
  Close the driver and make the JS object stale. It refuses with a busy error
  while a read, write, or flush is active or queued; it does not implicitly
  cancel an operation. GC finalization also releases forgotten ports eventually, but
  explicit `close()` remains the intended lifecycle boundary.
- `port.write(data)`
  Write an array-like sequence of bytes or native byte view and return the
  number of bytes accepted by the UART driver. TX backpressure keeps the
  synchronous call surface but waits cooperatively on UART write-ready events;
  timers and ready Futures continue to run. The TX lane executes writes in
  FIFO order. The port's configured `timeoutMs` is the total deadline for enqueueing
  the complete value; `timeoutMs: 0` performs one immediate non-blocking
  attempt. If a write fails after producing output, the thrown error exposes
  the exact partial count as `error.bytesWritten`; callers must not retry the
  whole value blindly.
- `port.writeChunks(chunks)`
  Write an array-like list of byte-source chunks and return `{ chunks, bytes, totalUs }`.
- `port.writeSource(source)`
  Write spans from a generic `ByteSpanSource`, such as `Bitmap.createSpanSource(...)`, and return `{ chunks, bytes, totalUs }`.

`write()`, `writeChunks()`, and `writeSource()` each use one operation-wide
deadline; chunk or span boundaries do not restart it. Their operational errors
all report the cumulative `bytesWritten` count. Each method has a native Future
driver, so `Future.call(port.write, port, [data])` snapshots or leases the input
and returns before UART backpressure clears. Prefer this form in persistent
applications so the current JavaScript call stack does not wait.

- `port.read(length, timeoutMs = uart.DEFAULT_TIMEOUT_MS)`
  Read up to `length` bytes and return the bytes actually received as a
  owned `ByteView`. It returns `null` when a positive-length read times out
  before receiving data. RX readiness is interrupt-driven; the timeout uses a
  one-shot native timer rather than readiness polling. Reads use a FIFO RX lane
  independent from the TX lane.
- `port.available()`
  Return the number of bytes currently buffered for reading.
- `port.flush(timeoutMs = uart.DEFAULT_TIMEOUT_MS)`
  Wait for pending TX bytes to leave the UART driver and hardware FIFO.
- `port.clearRx()`
  Discard buffered RX bytes.
- `port.watch(options?)`
  Return an `EventQueue` for continuous RX readiness and UART hardware errors.
  Options are `{ minBytes = 1, idleMs = 0, capacity = 8, includeErrors = true }`;
  `capacity` must be in `1..64`. Each port permits one watcher. Closing the
  watcher leaves the port open.

Watcher events do not contain received payload bytes. A readable event is
`{ type: "readable", sequence, timestampUs, availableBytes, reason }`, where
`reason` is `"threshold"` or `"idle"`. Error events are
`{ type: "error", sequence, timestampUs, code }`, with `code` equal to
`"fifoOverflow"`, `"bufferFull"`, `"break"`, `"parity"`, or `"frame"`.
Readable events are coalesced: while one is queued, additional received bytes
do not allocate another event. After consumption, the watcher rearms if data
remains buffered. Use `watcher.stats().dropped` and sequence gaps to detect
queue pressure.

Example:

```js
var port = uart.open({
  port: uart.DEFAULT_PORT,
  tx: uart.DEFAULT_TX,
  rx: uart.DEFAULT_RX,
  baud: 115200,
});

var events = port.watch({ minBytes: 1, idleMs: 20 });
port.write([0x41, 0x54, 0x0d, 0x0a]);
port.flush();
if (events.receive(500) !== null) {
  var input = port.read(64, 0);
  if (input !== null) {
    try {
      print(JSON.stringify(input.toArray()));
    } finally {
      input.close();
    }
  }
}
events.close();
port.close();
```

For automated loopback validation, wire TX to RX and run:

```bash
TEST_JS_CONFIG='{"uartLoopback":{"port":1,"tx":43,"rx":44}}' \
python scripts/remote.py test --scope js --module uart --loopback
```

## `rmt` Module

`rmt` is exposed when `sys.info.features.rmt` is true. It provides bounded
hardware pulse symbols and RX/TX channel lifecycle only. Protocols such as NEC,
device-specific pulse interpretation, and board policy remain in application
JavaScript.

- `rmt.capabilities()`
  Return target-dependent DMA support, the minimum hardware memory block, the
  4096-symbol logical-buffer limit, the 32767-tick duration limit, and explicit
  finite-loop support.
- `rmt.createSymbols(capacity)`
  Allocate a native `RMTSymbolBuffer`. Its `capacity` is fixed and its `length`
  is the logical number of symbols.
- `rmt.open({ direction, pin, resolutionHz, memorySymbols?, dma?, invert? })`
  Open one `"rx"` or `"tx"` channel. `resolutionHz` is required so tick units
  are never inferred. `memorySymbols` controls the ESP-IDF hardware block; it is
  not a chunk size exposed to JavaScript.

`RMTSymbolBuffer` provides `push(duration0Ticks, level0, duration1Ticks,
level1)`, `get(index)`, `set(index, ...)`, `clear()`, and idempotent `close()`.
Durations must fit the native 15-bit fields. A buffer is leased while an RMT
operation is pending and cannot be changed during that lease.

`RMTChannel` provides `start()`, `stop()`, `status()`, and idempotent `close()`.
TX uses `transmit(symbols, { loopCount?, endLevel?, timeoutMs? })`; loop counts
must be finite. `timeoutMs` defaults to 1000 for both directions. RX uses
`receive(symbols, { minPulseNs?, idleThresholdNs, timeoutMs? })`, fills the
caller's buffer, updates its logical length, and returns
`{ length, truncated, timestampUs }` or `null` at timeout. Operations enter one
bounded FIFO resource lane per
channel; different channels may progress independently. Use `Future.call()`
for non-blocking composition. A queued operation can be cancelled without
touching hardware; cancellation of an active operation synchronously aborts the
RMT channel before settlement. `close()` detaches the JavaScript handle,
cancels an active operation after a confirmed hardware abort, prevents queued
operations from reaching hardware, and releases the channel after all captured
Future states have drained.

```js
var symbols = rmt.createSymbols(2);
var channel = rmt.open({
  direction: "tx",
  pin: 4,
  resolutionHz: 1000000
});
try {
  symbols.push(560, 1, 560, 0);
  symbols.push(560, 1, 1690, 0);
  channel.start();
  channel.transmit(symbols, { loopCount: 0, timeoutMs: 100 });
  channel.stop();
} finally {
  channel.close();
  symbols.close();
}
```

## `i2s` Module

`i2s` is exposed when `sys.info.features.i2s` is true. Standard I2S supports
`direction: "rx" | "tx" | "duplex"`; PDM remains receive-only.

- `i2s.capabilities()`
  Return `{ ports, standard, standardRx, standardTx, standardDuplex, pdm,
  dataBits, limits }`. Limits include the ESP-IDF 4092-byte maximum for one DMA
  descriptor and 65536-byte bounds for one JavaScript read or write.
- `i2s.open(options)`
  Open one generation-checked `I2SChannel` without starting DMA. `port` is a
  number or `"auto"`. Common options include `sampleRateHz`, `timeoutMs`, and
  `dma: { descriptorCount, framesPerDescriptor }`.

Standard mode accepts `dataBits`, `slotBits`, `slotMode`, `slotMask`, `format`,
and `pins: { bclk, ws, din?, dout?, mclk? }`. RX requires `din`, TX requires
`dout`, and duplex requires both; a duplex pair is allocated on one I2S port.
Formats are `philips`, `msb`, `pcmShort`, and `pcmLong`. PDM accepts only
`direction: "rx"` with `pins: { clk, din }`, or uses explicitly selected
hardware constants. PDM output is signed 16-bit little-endian mono PCM; raw PDM
is not exposed.

`I2SChannel` methods:

- `channel.start()` / `channel.stop()`
  Explicit, idempotent DMA lifecycle operations. `stop()` disables the
  channel but retains its native channel and DMA ring for the next `start()`.
- `channel.read(frameCount, timeoutMs?)`
  RX/duplex only. Return `null` at timeout or
  `{ data, frames, byteLength, timestampUs, sequence, overruns }`, where `data`
  is an owned `ByteView`.
- `channel.write(data, timeoutMs?)`
  TX/duplex only. Accept a `ByteSource` or `ByteSpanSource` and return
  `{ frames, byteLength, timestampUs }`. The combined byte length must align to
  the configured PCM frame, including when a frame crosses span boundaries.
- `channel.status()`
  Return direction, port, running/read/write state, PCM layout, DMA
  configuration, receive queue overruns, and `sendQueueOverflows`.
- `channel.close()`
  Detach the JavaScript handle idempotently, request cancellation of active
  reads and writes, prevent queued operations from reaching DMA, and release
  the native handles after all captured Future states finish releasing their
  reservations.

Reads use one bounded FIFO RX lane and writes use an independent bounded FIFO
TX lane. Duplex allows one operation from each direction to be active
concurrently while preserving FIFO ordering within each direction. Direct calls
wait cooperatively; use `Future.call()` when the current runtime position must
remain non-blocking. Open each long-lived
audio channel once after Wi-Fi initialization, then reuse `start()` / `stop()`;
do not close and reopen it for every recording or playback attempt. On PSRAM
boards, transient PCM operation buffers are allocated from PSRAM so the
internal DMA-capable heap remains available to the persistent DMA ring. A PSRAM
allocation failure returns `I2S_NO_MEMORY` and does not fall back to internal
memory or try alternate DMA layouts.

Close every returned audio `ByteView` in a `finally` block after its consumer
finishes. Producers of `ByteSpanSource` data must likewise close the source in
`finally`; I2S closes a source it consumes on success and every failure path.

```js
var channel = i2s.open({
  direction: "rx",
  mode: "pdm",
  port: "auto",
  sampleRateHz: 16000,
  pins: { clk: 42, din: 41 },
  dma: { descriptorCount: 6, framesPerDescriptor: 240 },
  timeoutMs: 1000
});
try {
  channel.start();
  var chunk = channel.read(320, 1000);
  if (chunk !== null) {
    try {
      print(chunk.frames, chunk.byteLength, chunk.overruns);
    } finally {
      chunk.data.close();
    }
  }
} finally {
  channel.stop();
  channel.close();
}
```

## `camera` Module

`camera` is registered only when `sys.info.features.camera` is true on a
supported ESP32-S3 build. It is independent of I2S and provides explicit still
capture only: no background video, codecs, MJPEG, RTSP, or upload policy.

- `camera.capabilities()`
  Return target, PSRAM status/size, compiled sensor drivers, pixel formats, and
  frame sizes. Version 1 probes OV2640 or OV3660 after initialization; callers
  do not select a sensor model.
- `camera.open(options?)`
  Open the singleton camera. Options include `pixelFormat`, `frameSize`,
  `jpegQuality`, `frameBuffers`, `grabMode`, `bufferLocation`, `xclkFreqHz`,
  `timeoutMs`, and `pins`. Explicit pins override selected hardware constants;
  the resolved XCLK, SCCB, D0-D7, VSYNC, HREF, and PCLK map must be complete.

The safe defaults are JPEG, QVGA, quality 12, one PSRAM framebuffer, and
`whenEmpty`. Continuous acquisition is enabled only when the application
explicitly pairs two framebuffers with `latest`.

`Camera` methods:

- `cam.capture(timeoutMs?)`
  Return one `CameraFrame` or `null`. Only one capture Future may be pending and
  only one framebuffer may be leased. The native acquisition observes the
  timeout and returns `null` without closing the camera. Cancelling or
  interrupting an in-flight capture closes that camera instance after its
  worker exits, so the stale handle must not be reused.
- `cam.status()`
  Return configuration and ownership state. `status().sensor.model` reports
  the detected `ov2640` or `ov3660`.
- `cam.controls()` / `cam.setControl(name, value)`
  Read or update `frameSize`, `jpegQuality`, `brightness`, `contrast`,
  `saturation`, `horizontalMirror`, or `verticalFlip`.
- `cam.close()`
  Start an irreversible close, cancel active capture, and immediately revoke
  derived `CameraFrame` and unopened frame-source handles. Revoked frame data
  operations throw a closed-frame `ReferenceError`; `frame.close()` remains
  idempotent. The close waits only for active native bitmap/source readers,
  then releases the driver in a worker. Caller cancellation stops waiting but
  does not cancel cleanup. Only the current call stack is suspended; timers,
  transports, and other Futures continue to run. Use
  `Future.call(cam.close, cam, [])` when the caller does not need to wait.
  Repeated calls are safe.

`CameraFrame` exposes read-only `width`, `height`, `format`, `byteLength`,
`timestampUs`, and `sequence` fields:

- `frame.source({ chunkBytes? })`
  Return one active, one-shot `ByteSpanSource`. Chunks are limited to 32768
  bytes. A consuming transport closes the source and returns the framebuffer on
  success, failure, cancellation, or timeout.
- `frame.read(offset?, limit?)`
  Copy at most 32768 bytes into an owned `ByteView` for diagnostics. Close the
  view after inspection.
- `frame.close()`
  Return the framebuffer. It rejects while a source is active and is idempotent
  after the source has released the frame.

```js
var cam = camera.open({
  pixelFormat: "jpeg",
  frameSize: "qvga",
  jpegQuality: 12,
  frameBuffers: 1,
  grabMode: "whenEmpty",
  bufferLocation: "psram"
});
var frame = null;
var source = null;
try {
  frame = cam.capture(5000);
  if (frame !== null) {
    source = frame.source({ chunkBytes: 8192 });
    var response = fetch("https://example.com/frame", {
      method: "POST",
      headers: { "content-type": "image/jpeg" },
      body: source,
      timeoutMs: 10000
    });
    print(response.status, cam.status().sensor.model);
  }
} finally {
  if (source !== null) source.close();
  if (frame !== null) frame.close();
  cam.close();
}
```

## `usbSerial` Module

`usbSerial` is a bounded USB Serial/JTAG text or binary transport for headless
applications. It is compiled only when `sys.info.features.usbSerial` is
true and is mutually exclusive with `CONFIG_ESP32QJS_ENABLE_REPL`, because both
consume the same USB input stream.

- `usbSerial.MAX_FRAME_BYTES`
  Compile-time upper bound for one text frame or received binary chunk.
- `usbSerial.open(options?)`
  Return a bounded EventQueue handle. `options.mode` is explicitly `"text"`
  (the default) or `"binary"`; `options.maxFrameBytes` may select a smaller
  bound.
- `handle.recv(timeoutMs?)`
  Return the next text frame or owning `ByteView`, or `null` at the timeout.
  Text mode uses CR/LF boundaries. Binary mode returns native input chunks and
  never inserts, strips, or waits for a newline.
- `handle.send(value)` / `handle.status()` / `handle.close()`
  Send one frame/chunk, inspect counters, or close the queue. Text mode accepts
  bounded strings. Binary mode accepts `ByteView` or `ByteSpanSource` and writes
  the exact bytes without a terminator. Sending remains synchronous, but TX
  backpressure waits cooperatively on the USB write-ready interrupt so timers
  and other ready runtime work continue to run. A physically disconnected USB
  link fails immediately, and only one send may be active at a time.
  `Future.call(handle.send, handle, [value])` uses the native send driver and
  returns before the write completes; this is the preferred form for a
  long-running application.

```js
var serial = usbSerial.open({ maxFrameBytes: 4096 });
var line = serial.recv(1000);
if (line !== null) serial.send(line);
```

Boot and framework logs can precede protocol traffic. Host clients should wait
for a valid COBS-delimited application ready record rather than interpreting
boot text as protocol data.

## `bitmap` Module

This module exposes native Bitmaps for heavy pixel work. It is registered only when `sys.info.features.bitmap` is enabled. The JS `Surface` owns rendering, `PanelDriver` owns controller sequencing, and `DisplayTransport` owns SPI/I2C/GPIO operations; `bitmap` only owns pixels, transforms, and exported bytes.

- `bitmap.MONO1`
  Pixel format string `"mono1"`.
- `bitmap.GRAY8`
  Pixel format string `"gray8"`.
- `bitmap.RGB565`
  Pixel format string `"rgb565"`.
- `bitmap.RGB888`
  Pixel format string `"rgb888"`.
- `bitmap.create(options)`
  Create a native `Bitmap`. Required options are `{ width, height, format }`. Optional fields are `{ layout, storage, stride, pageHeight, chunkBytes, foreground, background }`.
- `bitmap.convert(source, options?)`
  Create a new Bitmap and run one fused crop, rotate, flip, resize, color-convert, and dither pass on the Future worker queue. The new Bitmap is published only after the operation succeeds.
- `bitmap.loadFont(path)`
  Load an EQF1 fixed bitmap font from LittleFS and return a native `DisplayFont`.

Formats and layouts:

- `format: "mono1"`
  One bit per pixel. Default layout is `"page-y8"` for SSD1306-style vertical pages. `"linear"` is also supported. Colors are packed numeric values `0` or `1`; booleans are not accepted.
- `format: "rgb565"`
  16-bit big-endian RGB565 pixels. Layout must be `"linear"`.
- `format: "gray8"`
  One luminance byte per pixel. Layout must be `"linear"`.
- `format: "rgb888"`
  Three bytes per pixel in RGB order with no alpha channel. Layout must be `"linear"`.
- `storage`
  `"auto"`, `"internal"`, `"psram"`, or `"dma"`. `"auto"` uses internal RAM for small buffers and PSRAM for larger buffers when available. `"dma"` is required for zero-copy RGB565 SPI flushes.

`Bitmap` properties:

- `width`, `height`
- `format`, `layout`
- `stride`, `pageHeight`
- `byteLength`

`Bitmap` methods:

- `close()`
  Release native memory. Other methods throw after close.
- `clear(color?)` / `fill(color?)`
  Fill the whole buffer and mark it dirty.
- `setPixel(x, y, color)` / `getPixel(x, y)`
  Write or read one packed color. `mono1` returns `0` or `1`, not a boolean.
- `fillRect(x, y, width, height, color?)`
- `drawCircle(cx, cy, radius, color?)`
- `fillCircle(cx, cy, radius, color?)`
- `drawEllipse(cx, cy, rx, ry, color?)`
- `fillEllipse(cx, cy, rx, ry, color?)`
- `drawRect(x, y, width, height, color?)`
- `drawRoundRect(x, y, width, height, radius, color?)`
- `fillRoundRect(x, y, width, height, radius, color?)`
- `drawLine(x0, y0, x1, y1, color?)`
- `drawPolyline(points, color?)`
- `drawPolygon(points, color?)`
- `fillPolygon(points, color?)`
  Draw or fill point lists shaped as `[[x, y], ...]`, `[{ x, y }, ...]`, or flat `[x0, y0, x1, y1, ...]`.
- `drawTriangle(x0, y0, x1, y1, x2, y2, color?)`
- `fillTriangle(x0, y0, x1, y1, x2, y2, color?)`
- `drawQuadraticBezier(x0, y0, cx, cy, x1, y1, color?, options?)`
- `drawCubicBezier(x0, y0, c1x, c1y, c2x, c2y, x1, y1, color?, options?)`
  Draw native Bezier curves. `options.segments` is clamped to `2..128`.
- `drawMask(x, y, { width, height, pixels }, options?)`
  Draw a mask bitmap with `options.color`. `options.background` is transparent by default; pass a packed color to fill off pixels.
- `blit(source, options?)`
  Transform into this Bitmap. `source` may be a Bitmap, an open raw
  `CameraFrame`, or `{ width, height, format, pixels, stride?, layout?,
  byteOrder?, bitOrder? }`. Raw formats are `"mono1"`, `"gray8"`, `"rgb565"`,
  and `"rgb888"`. RGB565 descriptors accept `byteOrder: "be" | "le"`; mono1
  descriptors accept `layout: "linear" | "page-y8"` and
  `bitOrder: "lsb" | "msb"`.

  Options are `{ sourceRect?, destinationRect?, rotation?, flipX?, flipY?,
  filter?, normalize?, threshold?, dither? }`. Rotation is clockwise
  `0`, `90`, `180`, or `270`. Processing order is crop, rotate, flip, resize,
  and encode. `normalize` applies only to gray8/mono1 output; `threshold` and
  `dither: "bayer4x4"` apply only to mono1. The Bayer matrix is anchored to
  absolute destination coordinates so tiled blits have no seams.

  Bitmap, CameraFrame, and ByteView inputs are read-leased while the worker is
  active and the target is write-leased. Busy close or mutation attempts fail
  clearly. Array-like pixels are copied to native staging memory before work.
  In-place or aliased blits are rejected. Cancellation may preserve completed
  rows and conservatively marks the full clipped destination dirty.
- `drawText(x, y, text, options?)`
  Draw text with `options.color` and `options.font`, a `DisplayFont` returned by `bitmap.loadFont(...)`. `options.spacing` controls extra inter-character pixels. Text background is transparent by default; pass `options.background` to fill each glyph cell before drawing, or `null` to keep it transparent explicitly.
- `measureText(text, options?)`
  Return `{ width, height, lines }`.
- `getDirty()`
  Return `{ x, y, width, height }` for the current bounding dirty rectangle, or `null`.
- `clearDirty()`
- `markDirty(x, y, width, height)`
- `readRect(x, y, width, height, options?)`
  Return a native byte view for the clamped rectangle.
- `readRectChunks(x, y, width, height, options?)`
  Return an array of native byte views split by `options.chunkBytes` or the buffer's `chunkBytes`. Passing `options.reuse: true` lets direct full-row exports reuse an internal chunk array and ByteView wrappers, which avoids per-frame wrapper allocation in display flush loops.
- `createSpanSource(options?)`
  Return a retained native `BitmapSpanSource` bound to the buffer. Pass it to `SPIDevice.writeSource(source, options?)` to flush without allocating JS chunk arrays or ByteView wrappers in the loop.
- `createCommandBuffer(options?)`
  Return a retained native `DisplayCommandBuffer` for recording drawing commands and replaying them into a `Bitmap`. Options are `{ commandCapacity, textBytes }`.

`BitmapSpanSource` methods:

- `source.setRect(x, y, width, height)`
  Update the clamped export rectangle and return the same source for reuse in display flush loops. This method belongs to Bitmap-created sources, not to the generic `ByteSpanSource` transport capability.

`DisplayCommandBuffer` methods:

- `reset()`
  Drop recorded commands while keeping native allocations for reuse.
- `close()`
  Release native command and text storage. Other methods throw after close.
- `clear(color?)` / `fill(color?)`
- `fillRect(x, y, width, height, color?)`
- `drawRect(x, y, width, height, color?)`
- `drawLine(x0, y0, x1, y1, color?)`
- `drawRoundRect(x, y, width, height, radius, color?)`
- `fillRoundRect(x, y, width, height, radius, color?)`
- `drawText(x, y, text, options?)`
  Record the same packed-color and native-font text options as `Bitmap.drawText(...)`.
- `appendPacked(bytes, options?)`
  Append a compact command byte stream in one native call. Packed colors are
  unsigned 32-bit little-endian values so all Bitmap formats share one command
  protocol. This is intended for display drivers that batch many JavaScript
  drawing primitives per frame before a single `replay(...)`. `options.text`
  carries the concatenated encoded text payload, and `options.font` is required
  when the packet contains text commands.
- `replay(target)`
  Execute all recorded commands into `target` once. The target must use the same pixel format as the buffer that created the command buffer.
- `stats()`
  Return `{ count, capacity, textBytes, textCapacity }`.

`DisplayFont` properties:

- `name`
- `width`, `height`
- `advance`, `lineHeight`

EQF1 fixed bitmap font files use a 16-byte header followed by glyph bytes:

- Bytes `0..3`: ASCII `EQF1`.
- Byte `4`: format, currently `1` for fixed bitmap.
- Byte `5`: flags, currently `0` for column-major, least-significant-bit first vertical pixels.
- Bytes `6..11`: `first`, `last`, `width`, `height`, `advance`, `lineHeight`.
- Bytes `12..15`: little-endian glyph byte length.
- Glyph data: `(last - first + 1) * width * ceil(height / 8)` bytes.

Use `scripts/font_to_eqf.py` to generate EQF1 files from BDF or a small JSON
bitmap description:

```sh
python3 scripts/font_to_eqf.py input.bdf shared/flash_data/_sys/display/fonts/my.eqf \
  --first 0x20 --last 0x7f --missing question
```

For BDF input, the converter uses `FONTBOUNDINGBOX` as the fixed glyph cell and
each glyph's `BBX` to place pixels inside that cell. Override `--width`,
`--height`, `--advance`, and `--line-height` when the source BDF metrics do not
match the desired display cell. EQF1 remains an 8-bit continuous range format,
so the output range must be within `0x00..0xff`.

For small Chinese UI strings, use an `eqf1-map` manifest. The same JSON file is
used by the generator and by `display.loadMappedFont(...)` at runtime:

```sh
python3 scripts/font_to_eqf.py --format manifest shared/flash_data/_sys/fonts/droid-cjk.json
```

The manifest maps source characters to safe printable ASCII EQF1 slots before
calling the native text renderer. This is deliberate: JavaScript strings are
passed to C as UTF-8, so mapped slots above `0x7f` are not single bytes at the C
API boundary.

The JSON input is useful for tiny hand-written fonts:

```json
{
  "width": 1,
  "height": 7,
  "advance": 2,
  "lineHeight": 8,
  "glyphs": {
    "0x41": ["1", "1", "1", "1", "1", "1", "0"],
    "0x42": { "columns": [62] }
  }
}
```

Export options:

- `byteOrder`
  `"be"` for high byte first or `"le"` for low byte first. This affects `rgb565` exports.
- `chunkBytes`
  Positive preferred chunk size for `readRectChunks(...)` and `createSpanSource(...)`.
- `reuse`
  Boolean hint for `readRectChunks(...)`. When true and the rectangle can be exported as direct full rows, the returned chunk array and ByteView wrappers may be reused by the same `Bitmap` on later calls. Use this only for immediate synchronous writes; do not keep old reused chunk arrays as snapshots.

Native byte views expose `length`, `byteLength`, and `toArray()`. They can be passed directly to `spi`, `i2c`, and `uart` writes without converting to a JavaScript array.
For high-frequency SPI display flushes, prefer `Bitmap.createSpanSource(...)` with `SPIDevice.writeSource(...)`. `readRect(...)` and `readRectChunks(...)` remain useful for inspection, diagnostics, compatibility, and I2C chunk writes. Transport modules consume generic byte sources or span sources and do not inspect Bitmap objects.

Example:

```js
var fb = bitmap.create({
  width: 240,
  height: 240,
  format: bitmap.RGB565,
  storage: "auto",
  chunkBytes: 4092,
});
var font = bitmap.loadFont("_sys/display/fonts/mono5x7.eqf");

fb.clear(0x0000);
fb.drawText(8, 8, "ESP32QJS", { color: 0xffff, font: font });

var chunk = fb.readRect(0, 0, 240, 16, { byteOrder: "be" });
device.write(chunk);
fb.clearDirty();
fb.close();
```

Grayscale camera preview should pass the leased frame directly instead of
calling `frame.read().toArray()` or scaling pixels in JavaScript:

```js
var preview = bitmap.create({
  width: 128,
  height: 64,
  format: "mono1"
});
var cam = camera.open({
  pixelFormat: "grayscale",
  frameSize: "qqvga",
  frameBuffers: 1
});
var frame = null;

try {
  frame = cam.capture(1000);
  if (frame !== null) {
    preview.blit(frame, {
      destinationRect: { x: 0, y: 0, width: preview.width, height: preview.height },
      rotation: 90,
      filter: "nearest",
      dither: "bayer4x4",
      normalize: true
    });
  }
} finally {
  if (frame !== null) frame.close();
  cam.close();
  preview.close();
}
```

## `gpio` Module

- `gpio.DISABLED`
  Disabled mode string for `gpio.pinMode()` or `gpio.configure()`.
- `gpio.INPUT`
  Input mode string for `gpio.pinMode()`.
- `gpio.OUTPUT`
  Output mode string for `gpio.pinMode()`.
- `gpio.INPUT_OUTPUT`
  Input + output mode string for bidirectional pins.
- `gpio.OUTPUT_OPEN_DRAIN`
  Open-drain output mode string.
- `gpio.INPUT_OUTPUT_OPEN_DRAIN`
  Open-drain input + output mode string.
- `gpio.FLOATING`
  Disable internal pulls.
- `gpio.PULLUP`
  Enable the internal pull-up.
- `gpio.PULLDOWN`
  Enable the internal pull-down.
- `gpio.PULLUP_PULLDOWN`
  Enable both internal pulls when supported by the pad.
- `gpio.CHANGE`, `gpio.RISING`, `gpio.FALLING`
  Edge-trigger constants accepted by `gpio.watch(...)`.
- `gpio.LOW`, `gpio.HIGH`
  Numeric helpers (`0` / `1`) used both for output levels and level-trigger
  interrupt modes in `gpio.watch(...)`.
- `gpio.DRIVE_0` .. `gpio.DRIVE_3`
  Drive-strength levels accepted by `gpio.setDriveStrength()` and `gpio.configure()`.
- `gpio.LED_BUILTIN`
  LED pin from the current wiring profile, or `-1` when unset.
- `gpio.USER_LED_PIN`
  User LED pin from the current wiring profile, or `-1` when unset.
- `gpio.USER_LED_ACTIVE_LOW`
  Whether the configured LED is active-low.
- `gpio.isValid(pin)`
  Return `true` when `pin` is a usable digital GPIO on the current target.
- `gpio.isOutputCapable(pin)`
  Return `true` when the pad supports output mode, drive strength, and hold.
- `gpio.pinMode(pin, mode)`
  Configure a GPIO as `gpio.DISABLED`, `gpio.INPUT`, `gpio.OUTPUT`, `gpio.INPUT_OUTPUT`, `gpio.OUTPUT_OPEN_DRAIN`, or `gpio.INPUT_OUTPUT_OPEN_DRAIN`.
- `gpio.setPull(pin, mode)`
  Set internal pull resistors with `gpio.FLOATING`, `gpio.PULLUP`, `gpio.PULLDOWN`, or `gpio.PULLUP_PULLDOWN`.
- `gpio.status(pin)`
  Return the live pad configuration object:
  `{ pin, valid, outputCapable, mode, pull, level, inputEnabled, outputEnabled, openDrain, pullup, pulldown, driveStrength, held, functionSelect, signalOut, outputControlledByPeripheral, outputEnableInverted, sleepEnabled, interruptAttached, interruptMode, interruptDropped }`.
- `gpio.configure(pin, options)`
  Apply `{ mode, pull, level, driveStrength, hold }` in one call and return `gpio.status(pin)`.
- `gpio.digitalWrite(pin, value)`
  Set a GPIO output level. If the pin is not already output-enabled, the helper promotes it to `gpio.OUTPUT`.
- `gpio.digitalRead(pin)`
  Read a GPIO level and return `true` or `false`.
- `gpio.toggle(pin)`
  Flip the current output level and return the new boolean level.
- `gpio.getDriveStrength(pin)`
  Return the current numeric drive strength (`0..3`).
- `gpio.setDriveStrength(pin, strength)`
  Update the pad drive strength and return the applied numeric value.
- `gpio.hold(pin, enabled)`
  Enable or disable pad hold on output-capable GPIOs.
- `gpio.watch(pin, mode = gpio.CHANGE)`
  Return a bounded `EventQueue` of `{ pin, level, mode }` interrupt events. The
  ESP-IDF ISR only enqueues native records and never invokes JavaScript. Only one
  watcher may own a pin; creating another closes and replaces the old watcher.
  Closing the queue detaches the interrupt.
- `gpio.reset(pin)`
  Reset the pad back to the ESP-IDF default GPIO state.
- `gpio.led(value)`
  Control the configured user LED. `true` turns it on. Throws when no LED pin
  is defined by the wiring profile.

Examples:

Configure an output and toggle it:

```js
var pin = gpio.USER_LED_PIN >= 0 ? gpio.USER_LED_PIN : gpio.LED_BUILTIN;

// Make the LED pin a push-pull output with a known initial level.
print(gpio.isValid(pin), gpio.isOutputCapable(pin));
print(JSON.stringify(gpio.configure(pin, {
  mode: gpio.OUTPUT,
  pull: gpio.FLOATING,
  driveStrength: gpio.DRIVE_1,
  level: gpio.HIGH,
})));

// Flip the output and read back the new boolean level.
sleep(100);
print(gpio.toggle(pin)); // false
gpio.led(false);
```

Read an input with pull-up enabled:

```js
var buttonPin = 9;

// Typical button wiring uses INPUT + pull-up and reads LOW when pressed.
gpio.pinMode(buttonPin, gpio.INPUT);
gpio.setPull(buttonPin, gpio.PULLUP);

print("button level:", gpio.digitalRead(buttonPin));
print(JSON.stringify(gpio.status(buttonPin)));
```

Watch an interrupt without blocking the startup call stack:

```js
var buttonPin = 9;
var interruptCount = 0;
var watching = true;

gpio.pinMode(buttonPin, gpio.INPUT);
gpio.setPull(buttonPin, gpio.PULLUP);
var interrupts = gpio.watch(buttonPin, gpio.FALLING);

function armInterrupt() {
  if (!watching) return;
  Future.call(interrupts.receive, interrupts, []).map(function (event) {
    if (!watching || event === null) return;
    interruptCount++;
    print("interrupt", interruptCount, event.pin, event.mode, event.level);
    armInterrupt();
  });
}

armInterrupt();

// Later, when the pin is no longer needed:
watching = false;
interrupts.close();
```

Use level-triggered interrupts explicitly:

```js
var pin = 9;

gpio.pinMode(pin, gpio.INPUT);
gpio.setPull(pin, gpio.PULLDOWN);

// LOW/HIGH reuse the same 0/1 constants as digital levels. Level-triggered
// sources can fill the bounded queue quickly, so consume or close it promptly.
var levels = gpio.watch(pin, gpio.HIGH);
// Consume it with Future.call(levels.receive, levels, []) as above, then close.
levels.close();
```

## `ledc` Module

This module exposes the ESP-IDF LEDC low-level timer/channel primitives. It does not implement higher-level drivers such as servos or `analogWrite(...)`.

- `ledc.AUTO_CLOCK`, `ledc.APB_CLOCK`, `ledc.XTAL_CLOCK`, `ledc.RC_FAST_CLOCK`
  Clock-source strings accepted by `ledc.timerConfig(...)`.
- `ledc.SLEEP_NO_ALIVE_NO_PD`, `ledc.SLEEP_NO_ALIVE_ALLOW_PD`, `ledc.SLEEP_KEEP_ALIVE`
  Sleep-mode strings accepted by `ledc.channelConfig(...)`.
- `ledc.CHANNEL_COUNT`
  Number of LEDC channels on the active target.
- `ledc.TIMER_COUNT`
  Number of LEDC timers on the active target.
- `ledc.MAX_DUTY_RESOLUTION_BITS`
  Maximum duty-resolution bits supported by the active target.
- `ledc.timerConfig(timer, options)`
  Configure or deconfigure one timer. `options` accepts `{ freqHz, dutyResolution, clock, deconfigure }`.
- `ledc.channelConfig(channel, options)`
  Configure or deconfigure one channel. `options` accepts `{ pin, timer, duty, hpoint, outputInvert, sleepMode, deconfigure }`.
- `ledc.timerStatus(timer)`
  Return the runtime's tracked timer state as
  `{ timer, configured, paused, freqHz, dutyResolution, maxDuty, clock }`
  without entering the live driver read path. Use `ledc.getFreq(timer)` when a
  live hardware frequency read is explicitly required.
- `ledc.channelStatus(channel)`
  Return `{ channel, configured, pin, timer, duty, hpoint, maxDuty, outputInvert, sleepMode }`.
- `ledc.setDuty(channel, duty)`
- `ledc.setDutyWithHpoint(channel, duty, hpoint)`
- `ledc.setDutyAndUpdate(channel, duty, hpoint?)`
  Update duty/hpoint state and return `ledc.channelStatus(channel)`.
- `ledc.updateDuty(channel)`
  Apply pending duty changes to hardware and return `ledc.channelStatus(channel)`.
- `ledc.getDuty(channel)`
- `ledc.getHpoint(channel)`
  Read live channel state from the driver.
- `ledc.setFreq(timer, freqHz)`
  Update timer frequency and return `ledc.timerStatus(timer)`.
- `ledc.getFreq(timer)`
  Read the live timer frequency.
- `ledc.bindChannelTimer(channel, timer)`
  Rebind a channel to another timer and return `ledc.channelStatus(channel)`.
- `ledc.stop(channel, idleLevel = false)`
  Stop PWM output on a channel.
- `ledc.timerPause(timer)` / `ledc.timerResume(timer)`
  Pause or resume a timer and return `ledc.timerStatus(timer)`.

Example:

```js
var pin = gpio.USER_LED_PIN >= 0 ? gpio.USER_LED_PIN : gpio.LED_BUILTIN;

ledc.timerConfig(0, { freqHz: 5000, dutyResolution: 8 });
ledc.channelConfig(0, {
  pin: pin,
  timer: 0,
  duty: 0,
  sleepMode: ledc.SLEEP_NO_ALIVE_NO_PD,
});

for (var duty = 0; duty <= 255; duty += 32) {
  ledc.setDutyAndUpdate(0, duty);
  sleep(40);
}

ledc.stop(0, false);
ledc.channelConfig(0, { deconfigure: true });
ledc.timerConfig(0, { deconfigure: true });
```

## `adc` Module

This module exposes ESP-IDF ADC oneshot primitives and GPIO/channel mapping helpers. It does not implement development-board-specific sensor drivers.

- `adc.UNIT_1`, `adc.UNIT_2`
  ADC unit identifiers accepted by `adc.open(...)`, `adc.status(...)`, and the read/configure helpers.
- `adc.ATTEN_DB_0`, `adc.ATTEN_DB_2_5`, `adc.ATTEN_DB_6`, `adc.ATTEN_DB_12`
  Attenuation constants accepted by `adc.configure(...)`.
- `adc.BITWIDTH_DEFAULT`, `adc.BITWIDTH_9` .. `adc.BITWIDTH_13`
  Bit-width constants accepted by `adc.configure(...)`.
- `adc.UNIT_COUNT`
  Number of ADC units on the active target.
- `adc.MAX_CHANNEL_COUNT`
  Maximum channels available on any ADC unit for the active target.
- `adc.open(unit)`
  Open one ADC unit for oneshot reads and return `adc.status(unit)`.
- `adc.close(unit)`
  Close one ADC unit and release any per-channel calibration state.
- `adc.status(unit)`
  Return `{ unit, opened, channelCount, channels }`, where `channels` contains `{ channel, configured, atten, bitwidth, pin, calibrated }`.
- `adc.configure(unit, channel, options)`
  Configure a channel with `{ atten, bitwidth }` and return `adc.status(unit)`.
- `adc.read(unit, channel)`
  Perform one raw oneshot read and return the integer ADC result.
- `adc.readMilliVolts(unit, channel)`
  Return a calibrated result in mV when calibration is available; otherwise it throws a clear calibration-availability error.
- `adc.ioToChannel(pin)`
  Map a GPIO to `{ unit, channel }` or return `null` when the pad is not ADC-capable.
- `adc.channelToIo(unit, channel)`
  Map a unit/channel pair back to its GPIO number or return `null`.

Example:

```js
var ref = adc.ioToChannel(0);

if (ref) {
  adc.open(ref.unit);
  adc.configure(ref.unit, ref.channel, {
    atten: adc.ATTEN_DB_12,
    bitwidth: adc.BITWIDTH_12,
  });

  print(adc.read(ref.unit, ref.channel));
  try {
    print(adc.readMilliVolts(ref.unit, ref.channel));
  } catch (error) {
    print(error.message || error);
  }

  adc.close(ref.unit);
}
```

## `dac` Module

This module exposes ESP-IDF DAC oneshot primitives and GPIO/channel mapping helpers. It is registered only on boards that compile with the `dac` feature enabled.

- `dac.CHANNEL_0`, `dac.CHANNEL_1`
  DAC channel identifiers accepted by `dac.open(...)`, `dac.close(...)`, `dac.status(...)`, `dac.write(...)`, and `dac.channelToIo(...)`.
- `dac.CHANNEL_COUNT`
  Number of DAC channels on the active target.
- `dac.RESOLUTION_BITS`
  DAC output resolution in raw digital bits.
- `dac.MAX_VALUE`
  Maximum raw value accepted by `dac.write(...)`.
- `dac.open(channel)`
  Open one DAC oneshot channel and return `dac.status(channel)`.
- `dac.close(channel)`
  Close one DAC channel and release its oneshot handle.
- `dac.status(channel?)`
  Return one channel status object or, when called with no arguments, an array of all channel statuses.
- `dac.write(channel, value)`
  Output one raw DAC value `0..dac.MAX_VALUE` and return `dac.status(channel)`.
- `dac.ioToChannel(pin)`
  Map a GPIO to `{ channel, pin }` or return `null` when the pad is not DAC-capable.
- `dac.channelToIo(channel)`
  Map a DAC channel back to its GPIO number.

Status objects look like:

```js
{
  channel: 0,
  opened: true,
  pin: 25,
  resolutionBits: 8,
  maxValue: 255,
  lastValue: 128
}
```

Example:

```js
var ref = dac.ioToChannel(25);

if (ref) {
  dac.open(ref.channel);
  dac.write(ref.channel, 128);
  print(JSON.stringify(dac.status(ref.channel)));
  dac.close(ref.channel);
}
```

## `sys` Module

- `sys.info`
  Read-only lazy namespace for stable facts. Its branches are `version`,
  `hardware`, `features`, and `runtime`. Each scalar is a getter; structured
  leaves such as `hardware.chip`, `hardware.flash`, and `runtime.heap` return a
  fresh detached snapshot only when read. `hardware.hardwareId` is `hw-` plus
  the lowercase factory eFuse Base MAC. `version.hostApi` remains `1`.
- `sys.status`
  Read-only lazy namespace for live `boot`, `cpu`, `memory`, `rtos`, and
  `runtime` state. Re-reading a leaf takes a new measurement. Heap capability
  views overlap and must not be added together.
- `sys.tasks(options?)`
  Return a bounded, task-ID-sorted FreeRTOS snapshot. `options.limit` is
  `1..CONFIG_ESP32_MQUICKJS_SYS_TASK_SNAPSHOT_MAX`. The call throws when task
  snapshots are not compiled in or when the native snapshot capacity would be
  exceeded. No task handles or stack addresses are exposed.
- `sys.restartRuntime(options?)` / `sys.reboot(options?)`
  Schedule runtime-generation replacement or a full software reboot at the
  next safe runtime-loop point. Options are `{ reason?, delayMs? }`, with a
  1–64-byte reason and `delayMs` from 0 through 60000. The returned
  `{ action, reason, generation, requestedAtMs, dueAtMs }` value is an
  acceptance receipt, not completion proof. Only one request may be pending.
- `sys.config()` returns a fresh object containing every immutable value from
  the selected hardware profile. Use it to inspect the complete configured
  wiring without guessing constant names. `sys.config(key)` reads one value;
  missing keys return `undefined`. Registered `ESP32QJS_*` values supply
  defaults to matching framework drivers, while application-owned keys should
  use an `APP_*` prefix. Explicit driver options still take precedence over
  profile constants, which in turn take precedence over safe Kconfig defaults.
- `sys.millis()`
  Return monotonic milliseconds from `esp_timer`.
- `sys.micros()`
  Return monotonic microseconds from `esp_timer`.
- `sys.freeHeap()`
  Return current free default-capability heap in bytes. On PSRAM builds this
  value can be dominated by external RAM and must not be used to decide
  whether a TLS handshake or DMA allocation is possible. Diagnose memory with
  `sys.status.memory.internal`, `.dma`, and `.psram`, especially each view's
  `largestFreeBlockBytes` and `minimumFreeBytes`.
- `sys.status.memory.manager`
  Reports the framework-wide allocation pressure state, startup-derived
  internal/DMA reserves, managed internal/PSRAM byte counts, idle movable
  bytes, migration/eviction counters, and classified allocation failures.
  Driver-owned DMA descriptors and other opaque ESP-IDF allocations are not
  included in the managed byte counters.
- `sys.randomHex(byteLength)`
  Return 1–64 cryptographically strong random bytes as two lowercase
  hexadecimal characters per byte. Before JavaScript-visible RF or ADC modules
  initialize, the runtime temporarily enables the SoC entropy source and seeds
  a process-lifetime CTR-DRBG; calls draw from that DRBG and wipe temporary
  native buffers. Native embedders must therefore install the standard globals
  before another task starts using RF or ADC hardware.
- `sys.withTimeout(timeoutMs, callback)`
  Run `callback` with a scoped wall-clock deadline between 1 and 60000
  milliseconds and return its value. A nested call only shortens an already
  active deadline; it never extends the surrounding callback or evaluation
  budget. The deadline remains active across cooperative native waits. A
  timeout is normalized after restoring the outer deadline and becomes the
  catchable `InternalError: sys.withTimeout() deadline exceeded`.
- `sys.time.status()`
  Return `{ synchronized, synchronizing, unixTimeMs }`. `unixTimeMs` is `null`
  until the wall clock is valid for certificate-date checks.
- `sys.time.sync({ servers, timeoutMs? })`
  When networking is selected, synchronize the system wall clock from one to
  four caller-selected SNTP server names and return
  `{ synchronized: true, unixTimeMs }`. An already valid clock returns
  immediately. Only one SNTP operation may be active: a concurrent call is
  rejected with `error.code === "TIME_SYNC_BUSY"`; calls are never merged or
  queued, so an accepted call always uses its own server list. Cancellation or
  timeout stops that call's SNTP operation. The operation uses whichever ESP
  network interface currently has an IP address; it does not own link policy.
  `timeoutMs` is an integer from 1 through 60000 milliseconds and defaults to
  15000; zero is rejected rather than treated as an infinite or immediate-check
  timeout.

Example:

```js
print(sys.info.version.framework);
print(JSON.stringify(sys.info.hardware.chip));
print(JSON.stringify(sys.status.memory.internal));
if (sys.info.features.fs) {
  print(fs.ROOT);
}
print(sys.millis());
print(sys.freeHeap());
print(sys.randomHex(16));
print(JSON.stringify(sys.time.status()));
var answer = sys.withTimeout(100, function () {
  return 42;
});
```

See [System Management API v1](sys-management-api.md) for exact getter shapes,
nullability, lifecycle semantics, and the migration map from the removed flat
`sys.info()` call.

## `net` Module

`net` is exposed when `sys.info.features.net` is enabled. It initializes the
shared ESP-NETIF runtime and observes every interface registered by Wi-Fi,
Ethernet, PPP, or an embedding application. It does not create link drivers,
store credentials, connect an interface, or test Internet reachability.

- `net.status()`
  Return `{ ready, primaryInterface, interfaces, truncated }`. Each interface
  contains `{ key, description, name, up, ready, defaultRoute, routePriority,
  ipv4, ipv6 }`. `ready` means the interface is up and has a non-zero IPv4 or
  preferred IPv6 address. Snapshots include at most the configured interface
  limit, eight by default.
- `net.watch()`
  Return an independent bounded `EventQueue` of
  `{ type: "status", status }`. The first event is an initial snapshot; later
  events are convergent snapshots after IP or default-route changes. Close the
  queue when finished.

```js
var changes = net.watch();
print(JSON.stringify(changes.receive(0).status));
var next = Future.call(changes.receive, changes, [10000]);
print(JSON.stringify(next.wait().status));
changes.close();
```

## `wifi` Module

Wi-Fi credentials are kept in RAM. Rebooting the board clears the active station config.

- `wifi.DEFAULT_TIMEOUT_MS`
  Default station connect timeout in milliseconds.
- `wifi.status()`
  Return `{ initialized, started, connected, scanning, ssid, hostname, ip, netmask, gateway, lastDisconnectReason, lastDisconnectReasonName }`.
- `wifi.connect(ssid, password, timeoutMs = wifi.DEFAULT_TIMEOUT_MS)`
  Start station mode through the native Future driver and return the updated status object.
- `wifi.disconnect()`
  Disconnect the station and return the updated status object.
- `wifi.scan()`
  Run an event-driven AP scan, including hidden access points, and return an array of `{ ssid, bssid, rssi, channel, authMode, hidden }`. Hidden beacon records have an empty `ssid` and require the caller to obtain and enter the exact SSID separately.

Example:

```js
print(JSON.stringify(wifi.status()));
var aps = wifi.scan();
print(aps.length);
wifi.connect("your-ssid", "your-password");
var clock = sys.time.sync({
  servers: ["pool.ntp.org", "time.cloudflare.com"],
  timeoutMs: 10000
});
print(clock.unixTimeMs);
var nextScan = Future.call(wifi.scan, wifi, []);
print(nextScan.wait(10000).length);
print(JSON.stringify(wifi.status()));
wifi.disconnect();
```

The firmware does not embed a time provider; Agent/workspace policy supplies
the server list. Complete `sys.time.sync(...)` after connecting and before any
public HTTPS, TLS, or secure WebSocket operation so certificate validity dates
are checked against a current clock. SNTP provides ordinary wall-clock setup,
not authenticated time: it does not defend against an active network attacker
who can tamper with both DNS/network traffic and time synchronization.
After synchronization, `Date.now()` and `new Date()` use the same wall clock;
`sys.millis()`, `sys.micros()`, and `performance.now()` remain monotonic uptime
clocks and are not affected by SNTP adjustments.

## `socket` Module

`socket` is exposed when `sys.info.features.socket` is enabled. It provides
bounded, handle-based TCP/UDP sockets. Verified outbound TLS streams are
available only when the separately selectable `sys.info.features.tls` build
capability is enabled. The framework does not add line framing, reconnect
policy, authentication, or an application protocol.

- `socket.open(protocol, options = {})`
  Open a `"tcp"` or `"udp"` socket and return its numeric handle.
  `options.localPort` binds the local port. For an outbound verified TLS client,
  use `socket.open("tcp", { tls: true })`; TLS uses the system CA certificate
  bundle, verifies the DNS name and certificate validity dates, and does not
  support listening or a fixed local port. When the TLS capability is omitted,
  requesting `tls: true` fails before allocating a socket handle.
- `socket.close(socket_id)`
  Close a handle. Closing an already closed handle returns `false`.
- `socket.status(socket_id)`
  Return protocol, local/remote endpoint, connected/listening state, peer-close
  state, and byte counters.
- `socket.get_max_message_bytes(socket_id)`
  Return the maximum bytes accepted by one send or receive call. For TCP this
  is a chunk limit, not a message boundary.
- `socket.tcp.connect(socket_id, remote_host, remote_port, timeout = 5000)`
  Connect a TCP handle. The host string may be an IP address or DNS name. DNS
  resolution is dispatched through the asynchronous lwIP resolver, so a slow
  lookup does not stop JavaScript, timers, other Futures, or EventQueues. The
  timeout covers resolution, TCP connect, and the TLS handshake when enabled.
  TLS connection setup runs in a bounded worker so ESP-IDF network waits do not
  block the JavaScript task; PSRAM profiles prefer external RAM for its stack.
- `socket.tcp.listen(socket_id, backlog = 4)`
  Turn a bound TCP handle into a listener.
- `socket.tcp.accept(socket_id, timeout = 0)`
  Return a connected client handle or `null` when no connection is ready.
- `socket.tcp.send(socket_id, data, timeout = 0)`
  Send a `ByteView`, array-like byte source, or `ByteSpanSource` and return the
  number of bytes written. A source is consumed one span at a time across
  partial plain/TLS writes, then closed on every terminal path. Source bodies
  are bounded by `CONFIG_ESP32_MQUICKJS_SOCKET_MAX_SOURCE_BYTES` (1 MiB by
  default) without requiring a single contiguous copy.
- `socket.tcp.recv(socket_id, max_bytes, timeout = 0)`
  Return one raw stream chunk or `null`. TCP has no message boundaries.
- `socket.udp.sendto(socket_id, remote_host, remote_port, data)`
  Send one UDP datagram. DNS names use the same asynchronous resolver path.
- `socket.udp.recvfrom(socket_id, max_bytes, timeout = 0)`
  Return `{ data, remoteHost, remotePort }` or `null`.

`accept`, `recv`, and `recvfrom` default to non-blocking operation. Their
optional `timeout` is bounded to 60000 ms and remains subordinate to an outer
`sys.withTimeout()` deadline.

```js
var client = socket.open("tcp", { localPort: 0 });
socket.tcp.connect(client, "192.0.2.10", 9000, 5000);
var payload = fs.open("payload.bin", "rb");
var payloadBytes = payload.read(1024);
payload.close();
socket.tcp.send(client, payloadBytes, 1000);
print(socket.tcp.recv(client, 1024, 100));
socket.close(client);

var secureClient = socket.open("tcp", { tls: true });
socket.tcp.connect(secureClient, "example.com", 443, 5000);
socket.tcp.send(secureClient, payloadBytes, 1000);
socket.close(secureClient);

var udp = socket.open("udp", { localPort: 0 });
socket.udp.sendto(udp, "192.0.2.10", 9001, "hello");
print(JSON.stringify(socket.udp.recvfrom(udp, 1024, 100)));
socket.close(udp);
```

Verified TLS failures from raw sockets and HTTPS fetches carry a stable `code`
of `TLS_ALLOC_FAILED`, `TLS_TIME_INVALID`, `TLS_VERIFY_FAILED`,
`TLS_HANDSHAKE_FAILED`, or `TLS_TIMEOUT`, plus numeric `espTlsError`,
`mbedtlsError`, and `verifyFlags` fields. Certificate contents and secrets are
not included. Close or cancel always releases the per-connection TLS context.
PSRAM profiles retain the standard 16 KiB RX and 4 KiB TX records while placing
mbedTLS allocations in external RAM; non-PSRAM profiles continue to use
internal memory. The full ESP-IDF certificate bundle accepts valid
cross-signed public-CA chains. Peer and intermediate certificate dates remain
verified; a bundle-generated trust anchor has no encoded validity dates and is
treated as the trusted public key it represents.
If external RAM encryption is not enabled, TLS session
material in PSRAM remains readable to an attacker with physical memory access.

## `rpc` Module

`rpc` is exposed only when `sys.info.features.rpc` is enabled. It is a generic
`esp32qjs.rpc/1` connection codec: COBS record framing, CRC-32 corruption
detection, deterministic CBOR, incremental reassembly, and transparent
`ByteSpanSource` streaming. The framework does not define opcodes, request
dispatch, authentication, authorization, retries, queues, workspace paths, or
an application schema.

- `rpc.createCodec(options)`
  Create a codec and return its numeric handle. `options.fields` is the
  non-empty ordered list that maps application field names to CBOR integer
  keys. `dynamicFields` optionally names fields whose nested JSON-like maps use
  CBOR text keys exclusively, including numeric-looking JavaScript property
  names. `allowStringKeys` applies the same text-key rule at the root when true.
  `streamDirectory` optionally selects where incoming transparent streams are
  spooled; without it, that codec rejects streamed input.
- `rpc.releaseCodec(codecId)`
  Release a codec. All decoders created from it must be released first.
- `rpc.createDecoder(codecId)` / `rpc.releaseDecoder(decoderId)`
  Allocate or release incremental connection state. Keep one decoder per
  physical connection; each decoder accepts arbitrary input chunk boundaries.
- `rpc.feed(decoderId, data)`
  Feed one raw transport chunk and return zero or more complete
  `{ opcode, requestId, flags, logicalLength, payload }` messages. The caller
  never parses or reassembles segments.
- `rpc.resetDecoder(decoderId)`
  Discard an incomplete message and any temporary streamed input while keeping
  the decoder handle.
- `rpc.encode(codecId, opcode, requestId, flags, payload)`
  Encode one logical message. A regular payload returns an array of owning
  `ByteView` frames. If the final CBOR value is a `ByteSpanSource`, it returns a
  one-shot `ByteSpanSource` that produces already framed bytes lazily. The
  caller writes either result to a binary transport without adding delimiters
  or performing its own segmentation.
- `rpc.bytes(value)`
  Copy byte data into an owning `ByteView` suitable for a CBOR byte string.
- `rpc.fileSource(path)` / `rpc.sourceInfo(source)`
  Create a one-shot file-backed `ByteSpanSource` for any readable regular file
  within the RPC stream limit, and inspect its `{ size, crc32 }` metadata.
  Locally created sources report a null CRC until transferred.
- `rpc.adoptFile(source, path)`
  Atomically rename an unused inbound temporary stream to an
  application-selected destination. It does not impose a workspace policy.
- `rpc.status()`
  Return the wire protocol name and aggregate codec, decoder, message, and
  error counters.

The codec accepts only the deterministic, definite-length CBOR subset. It
rejects tags, indefinite values, duplicate/non-canonical map keys, invalid
UTF-8, non-finite floats, excessive nesting, trailing data, interleaved logical
messages, and invalid frame CRCs. CRC-32 detects accidental transport or
storage corruption; it is not authentication.

```js
var codec = rpc.createCodec({
  fields: ["ok", "result", "data"],
  dynamicFields: ["result"],
  streamDirectory: "/data"
});
var decoder = rpc.createDecoder(codec);
var frames = rpc.encode(codec, 1, 7, 0, {
  data: rpc.bytes([0, 1, 2, 255])
});

// A TCP/serial receive callback may pass chunks of any size.
var messages = rpc.feed(decoder, incomingChunk);

rpc.releaseDecoder(decoder);
rpc.releaseCodec(codec);
```

The public C wire contract and incremental decoder are declared in
`include/esp32qjs_rpc_wire.h`, so a firmware application may use the framing
layer without adopting the JavaScript Agent product.

## `websocketClient` Module

`websocketClient` is exposed when `sys.info.features.websocket` is enabled.
The current ESP-IDF WS/WSS transport is selected together with the TLS
capability, so a WebSocket client build requires `sys.info.features.tls`.
It uses a bounded `EventQueue` handle and does not invoke application callbacks.

- `websocketClient.open(options)`
  Start a connection and return a handle. Required `options.url` uses `ws://`
  or `wss://`; the remaining network, reconnect, authorization, and size options
  are unchanged.
- `handle.recv(timeoutMs?)`
  Return the next `{ type: "open" | "message" | "close" | "error", ... }`
  event, or `null` at the timeout.
- `handle.send(text)` / `handle.status()` / `handle.close()`
  Send text, inspect counters, or close the event source. The send operation has
  a native Future driver; use `Future.call(handle.send, handle, [text])` to
  return before network backpressure clears. Only one send may be active.

```js
var client = websocketClient.open({
  url: "wss://agent.example/ws",
  authorization: "Bearer paired-device-token",
  autoReconnect: true
});
var event = client.recv(10000);
if (event && event.type === "open") {
  client.send(JSON.stringify({ type: "protocol.ping", id: 1 }));
}
```

Only complete text messages are accepted. Close, ping, and pong control frames
are handled natively and are not reported as application errors.

## `http` Module

The namespace is present when the HTTP client or server feature is enabled.

- `http.DEFAULT_TIMEOUT_MS` / `http.MAX_BODY_BYTES`
  HTTP-client limits when `sys.info.features.http` is enabled.
- `fetch(input, options?)` / `http.fetch(input, options?)`
  Run one request through the native HTTP Future driver. Options include
  `method`, `headers`, UTF-8 string, `Stream`, `ByteView`, or `ByteSpanSource`
  `body`, `timeoutMs`, and `maxBodyBytes`. Before the HTTP worker starts,
  binary input is materialized into a length-exact native PSRAM-first buffer,
  preserving embedded NUL bytes and enforcing a 1 MiB request-body default
  limit. A source is closed on all terminal paths.

The HTTP client remains available without TLS for `http://` URLs. An
`https://` URL is rejected before its worker starts when
`sys.info.features.tls` is false; there is no insecure fallback.

Call `sys.time.sync(...)` once after a network interface connects and before
public HTTPS.
HTTPS uses the same TLS verification and structured error categories described
in the socket section; it has no insecure or skip-verification option.
- `http.server(options?)`
  Create a low-level declarative server when
  `sys.info.features.httpServer` is enabled.

```js
var left = Future.call(fetch, globalThis, ["https://example.com/a"]);
var right = Future.call(fetch, globalThis, ["https://example.com/b"]);
var responses = Future.all([left, right]).wait(10000);
print(responses[0].status, responses[1].status);
```

### HTTP Server API

- `server.route(method, path)`
  Register a declarative route. Supported methods are `GET`, `POST`, `PUT`,
  `PATCH`, `DELETE`, `HEAD`, `OPTIONS`, and `ANY`; `*` in a path uses the
  built-in glob matcher.
- `server.start()` / `server.stop()`
  Start or stop listening while retaining the server and route table.
- `server.receive(timeoutMs?)`
  Return the next matching `Request`, or `null` at the timeout. The request
  queue is bounded and rejects overflow with HTTP 503.
- `server.respond(request, response)`
  Complete one live request with a `Response`. A request is generation-checked
  and cannot be completed twice.
- `server.removeRoute(path, method?)` / `server.clearRoutes()`
  Remove matching declarative routes.
- `server.close()`
  Stop the listener, close its EventQueue, reject pending requests, and release
  the native slot. Repeated close is safe.

Request bodies larger than 8192 bytes are rejected with HTTP 413 before they
enter the queue. `request.bytes(maxBytes?)` preserves binary input as an owned
`ByteView`. `Response.stream(...)` and `Response.bytes(...)` write in chunks
and close the stream/source after the response is sent, including failure and
cancellation paths. Known-length bodies set `Content-Length`; an explicitly
mismatched length is rejected before dispatch. Binary bodies do not receive an
implicit `Content-Type`, while existing string-body defaults remain unchanged.

```js
var server = http.server({ port: 8080, host: "0.0.0.0" });
server.route("GET", "/ping");
server.route("POST", "/echo");
server.start();

var request = server.receive(1000);
if (request !== null) {
  if (request.method === "POST") {
    server.respond(request, Response.text(request.text()));
  } else {
    server.respond(request, Response.text("pong"));
  }
}
```
