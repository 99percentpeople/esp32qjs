# `rmt` Module

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
RMT channel before settlement. With no pending operation, `close()` disables and
deletes the encoder/channel before detaching the JavaScript handle; a native
teardown failure keeps the remaining resources and handle available for a
retry. With pending work, `close()` detaches the JavaScript handle, cancels an
active operation after a confirmed hardware abort, prevents queued operations
from reaching hardware, and releases the channel after all captured Future
states have drained.

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
