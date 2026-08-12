# C API Reference

This document covers the APIs exported directly by the firmware runtime.

## Global Helpers

- `help()`
  Print the location of these API documents.
- `print(...values)`
  Write values to the serial console.
- `gc()`
  Run the JavaScript garbage collector.
- `fetch(input, options?)`
  Run an HTTP request through a hidden native Future and return a `Response`.
- `load(path)`
  Evaluate a script from the active filesystem root. It starts at `/littlefs`;
  applications may select any mounted root with `fs.setRoot(path)`.
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
```

## Futures

- `Future.call(fn, thisValue?, args?)`
  Queue a callable without invoking it before return. It starts at the next
  scheduler idle point and remains runtime-owned through settlement.
- `Future.all(futures)`
  Fulfill with results in input order. Other inputs are not cancelled when one fails.
- `Future.race(futures)`
  Settle as `{ index, value }` from the first input without cancelling the rest.
- `Future.sleep(ms)`
  Return a timer-backed Future.
- `Future.timeout(future, timeoutMs)`
  Apply an operation deadline and cancel the input if it expires.
- `future.status()` / `future.wait(timeoutMs?)` / `future.cancel()`
  Inspect, cooperatively wait for, or cancel a Future. A wait timeout does not
  cancel the operation.

Examples:

```js
var scan = Future.call(wifi.scan, wifi, []);
var request = Future.call(fetch, globalThis, ["https://example.com"]);
var values = Future.all([scan, request]).wait(10000);
print(values[0].length, values[1].status);
```

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

All `fs` operations are restricted to the active filesystem root.

- `fs.ROOT`
  Dynamic current filesystem root. It starts at `"/littlefs"`.
- `fs.setRoot(path)`
  Select an existing mounted directory as the root for relative `fs` operations
  and `load()`. The framework validates the mount/path only; the application
  owns policy about which root to select.
- `fs.list(path = ".")`
  Return an array of entries for a directory.
- `fs.stat(path)`
  Return `{ name, path, isDir, size }` for a path.
- `fs.exists(path)`
  Return `true` if the path exists.
- `fs.readText(path)`
  Read a UTF-8 text file.
- `fs.open(path, mode?)`
  Open a file stream. Supported modes are `r`, `rb`, `w`, `wb`, `a`, `ab`, `r+`, `w+`, and `a+`.
- `fs.writeText(path, text)`
  Overwrite a text file and return the number of bytes written.
- `fs.appendText(path, text)`
  Append text and return the number of bytes written.
- `fs.mkdir(path)`
  Create one directory level.
- `fs.rename(fromPath, toPath)`
  Rename a file or directory.
- `fs.remove(path)`
  Remove a file or an empty directory.

Example:

```js
fs.writeText("notes.txt", "hello\n");
print(fs.readText("notes.txt"));
print(fs.stat("notes.txt"));
print(JSON.stringify(fs.list(".")));
fs.rename("notes.txt", "notes-old.txt");
fs.remove("notes-old.txt");
```

## Secondary LittleFS

Profiles may enable `CONFIG_ESP32QJS_SECONDARY_LITTLEFS` and configure its
partition label and base path. The runtime mounts that partition through the
same generic LittleFS API but does not assign it any business meaning or select
it automatically. Application bootstrap code can call `fs.setRoot(path)` after
loading its system services. Bundled `_sys` modules remain available through
`framework.load(path)`.

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

`fs.open()` returns a `Stream`. `Response.body`, `Request.body`, and `Response.stream(...)` also use the same stream interface.

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
  Read up to `size` bytes. Text streams return a UTF-8 string, while streams opened with a binary mode such as `"rb"` return a native `ByteView`. Returns `null` at EOF. Default chunk size is `1024`. Use `typeof chunk === "string"` to distinguish text from binary chunks; call `toArray()` when JavaScript needs to inspect or parse `ByteView` bytes.
- `write(text)`
  Write a string and return the written byte count.
- `flush()`
- `close()`
- `seek(offset, whence?)`
  Move the file cursor and return the new position.
- `tell()`
  Return the current file cursor position.
- `eof()`
  Return `true` once the stream reached EOF.

Example:

```js
var stream = fs.open("_sys/ui/core.js", "r");
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
  Create a response from a string, `Stream`, or omitted body.
- `Response.text(text, init?)`
- `Response.json(value, init?)`
- `Response.stream(stream, init?)`

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
- `json()`

Body consumption notes:

- `Request.text()` / `Request.json()` read the full request body and consume the underlying stream.
- `Response.text()` / `Response.json()` read the full response body and consume the underlying stream.
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
- `i2c.open(options?)`
  Open an I2C master bus and return an `I2CBus` instance. `options` can include `{ sda, scl, freqHz, timeoutMs, internalPullup }`. Each call returns a distinct bus handle; close it when the caller is done with that bus.

`I2CBus` methods:

- `bus.status()`
  Return `{ opened, sda, scl, freqHz, timeoutMs, internalPullup }`.
- `bus.close()`
  Close the bus handle. After closing, the `I2CBus` instance becomes stale and its other methods throw. If a caller forgets to close it, GC finalization will also release the native handle eventually, but explicit `close()` remains the intended lifecycle boundary.
- `bus.scan()`
  Probe `0x03..0x77` and return an array of 7-bit device addresses.
- `bus.write(addr, data)`
  Write an array-like sequence of bytes or native byte view and return the number of bytes written.
- `bus.writeChunks(addr, chunks)`
  Write an array-like list of byte-source chunks to one I2C device while reusing the same device handle. This is intended for data already split by producers such as `displayBuffer.readRectChunks(...)`. It returns `{ chunks, bytes, totalUs }`.
- `bus.read(addr, length)`
  Read `length` bytes and return them as a JavaScript array.
- `bus.writeRead(addr, writeData, readLength)`
  Write bytes, then read bytes in one transaction and return the read array.

Example:

```js
var bus = i2c.open({ sda: 5, scl: 6, freqHz: 400000 });
print(JSON.stringify(bus.status()));
print(JSON.stringify(bus.scan())); // [60] for an SSD1306 at 0x3c
print(bus.write(0x3c, [0x00, 0xAF])); // SSD1306 display on
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
  Perform one synchronous full-duplex transaction from an array-like sequence of bytes or native byte view and return the received bytes as a JavaScript array.
- `device.write(data)`
  Perform one synchronous write-only transaction from an array-like sequence of bytes or native byte view and return the number of transmitted bytes.
- `device.writeChunks(chunks, options?)`
  Queue an array-like list of byte-source chunks for write-only SPI transfers. `options.queueDepth` defaults to `2` and is capped by the device queue size. DMA-capable chunks are queued directly; other chunks are copied into DMA-capable staging buffers. The method returns `{ chunks, bytes, prepUs, queueUs, waitUs, transferUs, totalUs, queueDepth, direct }`.
- `device.writeSource(source, options?)`
  Queue spans from a retained native `ByteSpanSource`, such as `DisplayBuffer.createSpanSource(...)`, without materializing a JavaScript chunk array. SPI treats the source as a generic transport capability; it does not inspect display-buffer internals. `options.queueDepth` and the returned stats object match `writeChunks(...)`.
- `device.read(length, fillByte = 0)`
  Clock `length` bytes and return the bytes read from MISO. `fillByte` controls the dummy value shifted out on MOSI while reading.

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

This module exposes synchronous TTL UART ports. It is intended for bounded peripheral exchanges and byte handoff; `Stream` remains the async/file/http-style IO abstraction.

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
  Default timeout for `read()` and `flush()`.
- `uart.open(options?)`
  Open one UART port and return a `UARTPort`. `options` can include `{ port, tx, rx, baud, dataBits, parity, stopBits, rxBufferSize, txBufferSize, timeoutMs }`. `parity` is `"none"`, `"even"`, or `"odd"`; `stopBits` is `1`, `1.5`, or `2`. Opening an already-open UART port throws.

`UARTPort` methods:

- `port.status()`
  Return `{ opened, port, tx, rx, baud, dataBits, parity, stopBits, rxBufferSize, txBufferSize, timeoutMs }`.
- `port.close()`
  Close the driver and make the JS object stale. GC finalization also releases forgotten ports eventually, but explicit `close()` remains the intended lifecycle boundary.
- `port.write(data)`
  Write an array-like sequence of bytes or native byte view and return the number of bytes accepted by the UART driver.
- `port.writeChunks(chunks)`
  Write an array-like list of byte-source chunks and return `{ chunks, bytes, totalUs }`.
- `port.writeSource(source)`
  Write spans from a generic `ByteSpanSource`, such as `DisplayBuffer.createSpanSource(...)`, and return `{ chunks, bytes, totalUs }`.
- `port.read(length, timeoutMs = uart.DEFAULT_TIMEOUT_MS)`
  Read up to `length` bytes and return the bytes actually received as a JavaScript array.
- `port.available()`
  Return the number of bytes currently buffered for reading.
- `port.flush(timeoutMs = uart.DEFAULT_TIMEOUT_MS)`
  Wait for pending TX bytes to leave the UART driver and hardware FIFO.
- `port.clearRx()`
  Discard buffered RX bytes.

Example:

```js
var port = uart.open({
  port: uart.DEFAULT_PORT,
  tx: uart.DEFAULT_TX,
  rx: uart.DEFAULT_RX,
  baud: 115200,
});

port.write([0x41, 0x54, 0x0d, 0x0a]);
port.flush();
print(JSON.stringify(port.read(64, 500)));
port.close();
```

For automated loopback validation, wire TX to RX and run:

```bash
TEST_JS_CONFIG='{"uartLoopback":{"port":1,"tx":43,"rx":44}}' \
python scripts/remote.py test --scope js --module uart --loopback
```

## `usbSerial` Module

`usbSerial` is a bounded USB Serial/JTAG text-frame transport for headless
applications. It is compiled only when `sys.info().features.usbSerial` is
true and is mutually exclusive with `CONFIG_ESP32QJS_ENABLE_REPL`, because both
consume the same USB input stream.

- `usbSerial.MAX_FRAME_BYTES`
  Compile-time upper bound for one UTF-8 line.
- `usbSerial.open(options?)`
  Start receiving lines and return a bounded EventQueue handle.
  `options.maxFrameBytes` may select a smaller bound.
- `handle.recv(timeoutMs?)`
  Return the next text frame, or `null` at the timeout. CR, LF, and CRLF
  terminate a frame; oversized input is discarded through the next terminator.
- `handle.send(text)` / `handle.status()` / `handle.close()`
  Send one frame, inspect counters, or close the queue. Embedded CR/LF and
  oversized outbound strings are rejected.

```js
var serial = usbSerial.open({ maxFrameBytes: 4096 });
var line = serial.recv(1000);
if (line !== null) serial.send(line);
```

Boot and framework logs can precede protocol traffic. Host clients should wait
for an application-level ready envelope rather than assuming the first serial
line is JSON.

## `displayBuffer` Module

This module exposes native display buffers for heavy pixel work. It is registered only when `sys.info().features.displayBuffer` is enabled. The JS `Surface` owns rendering, `PanelDriver` owns controller sequencing, and `DisplayTransport` owns SPI/I2C/GPIO operations; `displayBuffer` only owns pixels and export bytes.

- `displayBuffer.MONO1`
  Pixel format string `"mono1"`.
- `displayBuffer.RGB565`
  Pixel format string `"rgb565"`.
- `displayBuffer.create(options)`
  Create a native `DisplayBuffer`. Required options are `{ width, height, format }`. Optional fields are `{ layout, storage, stride, pageHeight, chunkBytes, foreground, background }`.
- `displayBuffer.loadFont(path)`
  Load an EQF1 fixed bitmap font from LittleFS and return a native `DisplayFont`.

Formats and layouts:

- `format: "mono1"`
  One bit per pixel. Default layout is `"page-y8"` for SSD1306-style vertical pages. `"linear"` is also supported. Colors are packed numeric values `0` or `1`; booleans are not accepted.
- `format: "rgb565"`
  16-bit RGB565 pixels. Layout must be `"linear"`.
- `storage`
  `"auto"`, `"internal"`, `"psram"`, or `"dma"`. `"auto"` uses internal RAM for small buffers and PSRAM for larger buffers when available. `"dma"` is required for zero-copy RGB565 SPI flushes.

`DisplayBuffer` properties:

- `width`, `height`
- `format`, `layout`
- `stride`, `pageHeight`
- `byteLength`

`DisplayBuffer` methods:

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
- `drawBitmap(x, y, { width, height, pixels }, options?)`
  Draw a mask bitmap with `options.color`. `options.background` is transparent by default; pass a packed color to fill off pixels.
- `drawText(x, y, text, options?)`
  Draw text with `options.color` and `options.font`, a `DisplayFont` returned by `displayBuffer.loadFont(...)`. `options.spacing` controls extra inter-character pixels. Text background is transparent by default; pass `options.background` to fill each glyph cell before drawing, or `null` to keep it transparent explicitly.
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
  Return a retained native `DisplayBufferSpanSource` bound to the buffer. Pass it to `SPIDevice.writeSource(source, options?)` to flush without allocating JS chunk arrays or ByteView wrappers in the loop.
- `createCommandBuffer(options?)`
  Return a retained native `DisplayCommandBuffer` for recording drawing commands and replaying them into a `DisplayBuffer`. Options are `{ commandCapacity, textBytes }`.

`DisplayBufferSpanSource` methods:

- `source.setRect(x, y, width, height)`
  Update the clamped export rectangle and return the same source for reuse in display flush loops. This method belongs to display-buffer-created sources, not to the generic `ByteSpanSource` transport capability.

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
  Record the same packed-color and native-font text options as `DisplayBuffer.drawText(...)`.
- `appendPacked(bytes, options?)`
  Append a compact command byte stream in one native call. This is intended for display drivers that batch many JavaScript drawing primitives per frame before a single `replay(...)`. `options.text` carries the concatenated encoded text payload, and `options.font` is required when the packet contains text commands.
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
  `"be"` or `"rgb565be"` for high byte first, `"le"` or `"rgb565le"` for low byte first. This affects `rgb565` exports.
- `chunkBytes`
  Positive preferred chunk size for `readRectChunks(...)` and `createSpanSource(...)`.
- `reuse`
  Boolean hint for `readRectChunks(...)`. When true and the rectangle can be exported as direct full rows, the returned chunk array and ByteView wrappers may be reused by the same `DisplayBuffer` on later calls. Use this only for immediate synchronous writes; do not keep old reused chunk arrays as snapshots.

Native byte views expose `length`, `byteLength`, and `toArray()`. They can be passed directly to `spi`, `i2c`, and `uart` writes without converting to a JavaScript array.
For high-frequency SPI display flushes, prefer `DisplayBuffer.createSpanSource(...)` with `SPIDevice.writeSource(...)`. `readRect(...)` and `readRectChunks(...)` remain useful for inspection, diagnostics, compatibility, and I2C chunk writes. Transport modules consume generic byte sources or span sources and do not inspect display buffer objects.

Example:

```js
var fb = displayBuffer.create({
  width: 240,
  height: 240,
  format: displayBuffer.RGB565,
  storage: "auto",
  chunkBytes: 4092,
});
var font = displayBuffer.loadFont("_sys/display/fonts/mono5x7.eqf");

fb.clear(0x0000);
fb.drawText(8, 8, "ESP32QJS", { color: 0xffff, font: font });

var chunk = fb.readRect(0, 0, 240, 16, { byteOrder: "be" });
device.write(chunk);
fb.clearDirty();
fb.close();
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
  Edge-trigger constants accepted by `gpio.attachInterrupt(...)`.
- `gpio.LOW`, `gpio.HIGH`
  Numeric helpers (`0` / `1`) used both for output levels and level-trigger interrupt modes in `gpio.attachInterrupt(...)`.
- `gpio.DRIVE_0` .. `gpio.DRIVE_3`
  Drive-strength levels accepted by `gpio.setDriveStrength()` and `gpio.configure()`.
- `gpio.LED_BUILTIN`
  Built-in LED pin number for the current board.
- `gpio.USER_LED_PIN`
  User LED pin number for the current board.
- `gpio.USER_LED_ACTIVE_LOW`
  Whether the board LED is active-low.
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
- `gpio.attachInterrupt(pin, callback, mode = gpio.CHANGE)`
  Attach an Arduino-style GPIO interrupt callback. The ESP-IDF ISR only queues an event; the JavaScript callback runs later on the JS thread and receives `{ pin, level, mode }`.
- `gpio.detachInterrupt(pin)`
  Remove the interrupt callback from a pin and return `gpio.status(pin)`.
- `gpio.reset(pin)`
  Reset the pad back to the ESP-IDF default GPIO state.
- `gpio.led(value)`
  Control the board user LED. `true` turns it on.

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

Attach an interrupt and handle it in JavaScript:

```js
var buttonPin = 9;
var interruptCount = 0;

gpio.pinMode(buttonPin, gpio.INPUT);
gpio.setPull(buttonPin, gpio.PULLUP);

// The callback runs on the JS thread, not directly inside the ISR.
// You can ignore the event argument if you only want Arduino-style behavior.
gpio.attachInterrupt(buttonPin, function (event) {
  interruptCount++;
  print("interrupt", interruptCount, event.pin, event.mode, event.level);
}, gpio.FALLING);

// ... your app logic here ...

// Detach when the pin is no longer needed.
gpio.detachInterrupt(buttonPin);
```

Use level-triggered interrupts explicitly:

```js
var pin = 9;

gpio.pinMode(pin, gpio.INPUT);
gpio.setPull(pin, gpio.PULLDOWN);

// LOW/HIGH reuse the same 0/1 constants as digital levels.
gpio.attachInterrupt(pin, function (event) {
  print("level interrupt", event.mode, event.level);
}, gpio.HIGH);
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
  Return `{ timer, configured, paused, freqHz, dutyResolution, maxDuty, clock }`.
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

This module exposes ESP-IDF ADC oneshot primitives and GPIO/channel mapping helpers. It does not implement board-specific sensor drivers.

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

- `sys.info()`
  Return board/chip identity plus memory/runtime fields:
  `{ runtimeVersion, hostApiVersion, board, chip, features, userLedPin, userLedActiveLow, scriptsDir, flashSize, psramEnabled, psramSize, freePsram, totalInternalHeap, freeInternalHeap, jsHeapSize, jsHeapRegion, littlefsMounted, replEnabled, autoRunIndexJs, formatLittlefsOnMountFail, freeHeap, jsTimeMs }`. `runtimeVersion` follows framework SemVer; `hostApiVersion` is the integer native compatibility level.
  `features` is `{ fs, nvs, gpio, ledc, adc, dac, i2c, spi, uart, usbSerial, socket, websocket, displayBuffer, wifi, http, httpServer }` and is the stable way to discover which optional host modules were compiled into the firmware for the current board.
- `sys.millis()`
  Return monotonic milliseconds from `esp_timer`.
- `sys.micros()`
  Return monotonic microseconds from `esp_timer`.
- `sys.freeHeap()`
  Return current free heap in bytes.
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

Example:

```js
print(JSON.stringify(sys.info().features));
if (sys.info().features.fs) {
  print(fs.ROOT);
}
print(sys.millis());
print(sys.freeHeap());
print(sys.randomHex(16));
var answer = sys.withTimeout(100, function () {
  return 42;
});
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
  Run an event-driven AP scan and return an array of `{ ssid, bssid, rssi, channel, authMode, hidden }`.

Example:

```js
print(JSON.stringify(wifi.status()));
var aps = wifi.scan();
print(aps.length);
wifi.connect("your-ssid", "your-password");
var nextScan = Future.call(wifi.scan, wifi, []);
print(nextScan.wait(10000).length);
print(JSON.stringify(wifi.status()));
wifi.disconnect();
```

## `socket` Module

`socket` is exposed when `sys.info().features.socket` is enabled. It provides
bounded, handle-based raw sockets. The framework does not add line framing,
reconnect policy, authentication, or an application protocol.

- `socket.open(protocol, local_port = 0)`
  Open and bind a `"tcp"` or `"udp"` socket and return its numeric handle.
- `socket.close(socket_id)`
  Close a handle. Closing an already closed handle returns `false`.
- `socket.status(socket_id)`
  Return protocol, local/remote endpoint, connected/listening state, peer-close
  state, and byte counters.
- `socket.get_max_message_bytes(socket_id)`
  Return the maximum bytes accepted by one send or receive call. For TCP this
  is a chunk limit, not a message boundary.
- `socket.tcp.connect(socket_id, remote_ip, remote_port, timeout = 5000)`
  Connect a TCP handle. The host string may be an IP address or DNS name.
- `socket.tcp.listen(socket_id, backlog = 4)`
  Turn a bound TCP handle into a listener.
- `socket.tcp.accept(socket_id, timeout = 0)`
  Return a connected client handle or `null` when no connection is ready.
- `socket.tcp.send(socket_id, data, timeout = 0)`
  Send a raw string and return the number of bytes written.
- `socket.tcp.recv(socket_id, max_bytes, timeout = 0)`
  Return one raw stream chunk or `null`. TCP has no message boundaries.
- `socket.udp.sendto(socket_id, remote_ip, remote_port, data)`
  Send one UDP datagram.
- `socket.udp.recvfrom(socket_id, max_bytes, timeout = 0)`
  Return `{ data, remoteIp, remotePort }` or `null`.

`accept`, `recv`, and `recvfrom` default to non-blocking operation. Their
optional `timeout` is bounded to 60000 ms and remains subordinate to an outer
`sys.withTimeout()` deadline.

```js
var client = socket.open("tcp", 0);
socket.tcp.connect(client, "192.0.2.10", 9000, 5000);
socket.tcp.send(client, "hello", 1000);
print(socket.tcp.recv(client, 1024, 100));
socket.close(client);

var udp = socket.open("udp", 0);
socket.udp.sendto(udp, "192.0.2.10", 9001, "hello");
print(JSON.stringify(socket.udp.recvfrom(udp, 1024, 100)));
socket.close(udp);
```

## `websocketClient` Module

`websocketClient` is exposed when `sys.info().features.websocket` is enabled.
It uses a bounded `EventQueue` handle and does not invoke application callbacks.

- `websocketClient.open(options)`
  Start a connection and return a handle. Required `options.url` uses `ws://`
  or `wss://`; the remaining network, reconnect, authorization, and size options
  are unchanged.
- `handle.recv(timeoutMs?)`
  Return the next `{ type: "open" | "message" | "close" | "error", ... }`
  event, or `null` at the timeout.
- `handle.send(text)` / `handle.status()` / `handle.close()`
  Send text, inspect counters, or close the event source.

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
  HTTP-client limits when `sys.info().features.http` is enabled.
- `fetch(input, options?)` / `http.fetch(input, options?)`
  Run one request through the native HTTP Future driver. Options include
  `method`, `headers`, UTF-8 string or `Stream` `body`, `timeoutMs`, and
  `maxBodyBytes`.
- `http.server(options?)`
  Create a low-level declarative server when
  `sys.info().features.httpServer` is enabled.

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
enter the queue. `Response.stream(...)` writes in chunks and closes the stream
after the response is sent.

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
