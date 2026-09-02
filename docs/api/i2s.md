# `i2s` Module

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
  Duplex lifecycle tracks RX and TX independently: if RX start fails, a newly
  enabled TX side is rolled back, and any failed rollback or stop retains the
  exact enabled side for a later `start()`, `stop()`, or `close()` retry.
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
  configuration, receive queue overruns, and `sendQueueOverflows`. Its `dma`
  object includes `storage: "internal"`, the actual aligned `bufferBytes` per
  descriptor, and `totalBufferBytes` across RX/TX directions.
- `channel.close()`
  With no pending operation, disable and delete RX/TX before detaching the
  JavaScript handle; a native teardown failure retains the remaining handles,
  DMA accounting, peripheral lease, and JavaScript handle for retry. With
  pending work, detach idempotently, request cancellation of active reads and
  writes, prevent queued operations from reaching DMA, and release after all
  captured Future states finish releasing their reservations. A deferred
  native teardown failure remains owned by the slot for open/runtime-init retry.

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
memory or try alternate DMA layouts. The persistent ring remains allocated by
ESP-IDF, but its admission, exact PCM payload accounting, and release are
registered with the framework memory manager.

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
