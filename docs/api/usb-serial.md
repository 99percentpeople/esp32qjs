# `usbSerial` Module

`usbSerial` is a bounded USB Serial/JTAG text or binary transport for headless
applications. It is compiled only when `sys.info.features.usbSerial` is
true and is mutually exclusive with `CONFIG_ESP32QJS_ENABLE_REPL`, because both
consume the same USB input stream.

- `usbSerial.capabilities()`
  Return the target, ESP-IDF version, supported text/binary modes, and the
  compile-time `maxFrameBytes` limit.
- `usbSerial.open(options?)`
  Return a bounded EventQueue handle. `options.mode` is explicitly `"text"`
  (the default) or `"binary"`; text mode uses `options.maxFrameBytes`, while
  binary mode uses `options.chunkBytes`, to select a smaller bound.
- `handle.receive(timeoutMs?)`
  Return the next text frame or owning `ByteView`, or `null` at the timeout.
  Text mode uses CR/LF boundaries. Binary mode returns native input chunks and
  never inserts, strips, or waits for a newline.
- `handle.send(value)` / `handle.status()` / `handle.stats()` / `handle.close()`
  Send one frame/chunk, inspect counters, or close the queue. Text mode accepts
  bounded strings. Binary mode accepts `ByteView` or `ByteSpanSource` and writes
  the exact bytes without a terminator. Sending remains synchronous, but TX
  backpressure waits cooperatively on the USB write-ready interrupt so timers
  and other ready runtime work continue to run. A physically disconnected USB
  link fails immediately, and only one direct send may be active at a time.
  `Future.call(handle.send, handle, [value])` uses the native send driver and
  returns before the write completes; queued Future sends share one FIFO TX
  lane. The Future driver owns a progress timer, so a stalled host wakes and
  rejects the send even when no unrelated runtime event occurs. This is the
  preferred form for a long-running application.

```js
var serial = usbSerial.open({ maxFrameBytes: 4096 });
var line = serial.receive(1000);
if (line !== null) serial.send(line);
```

Boot and framework logs can precede protocol traffic. Host clients should wait
for a valid COBS-delimited application ready record rather than interpreting
boot text as protocol data.
