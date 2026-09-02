# `uart` Module

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
  cancel an operation. If driver deletion fails, `close()` throws and retains
  the driver, event queue, watcher lock, and JS generation for a later cleanup
  retry. GC finalization also requests cleanup for forgotten ports, but explicit
  `close()` remains the intended lifecycle boundary.
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
