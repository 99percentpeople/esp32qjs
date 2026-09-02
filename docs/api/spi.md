# `spi` Module

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
  Open one SPI master bus and return an `SPIBus` instance. Without options it uses `DEFAULT_HOST`, `DEFAULT_SCLK`, `DEFAULT_MOSI`, and `DEFAULT_MISO`. `options` can override `{ host, sclk, mosi, miso, maxTransferSize, dmaStagingBytes }`. `dmaStagingBytes` defaults to `min(8192, maxTransferSize)` and must be a positive integer no larger than `maxTransferSize`. The bus reserves two reusable internal-DMA TX/RX staging slots of this size; operations never resize them. One JS `SPIBus` maps to one ESP-IDF SPI host; opening the same host twice throws.

`SPIBus` methods:

- `bus.status()`
  Return `{ opened, host, sclk, mosi, miso, maxTransferSize, dmaStagingBytes, deviceCount }`.
- `bus.close()`
  Close every idle child device and then the bus. Any `SPIDevice` objects
  opened from that bus become stale after success. A native device-remove or
  bus-free failure throws, retains the exact remaining handles and fixed staging
  workspace, and leaves the bus handle usable only for another `close()`
  attempt; reopening the same host and runtime initialization can also retry
  orphaned cleanup. If callers forget to close the bus, GC finalization still
  requests the same native cleanup, but explicit `close()` remains the intended
  lifecycle boundary.
- `bus.openDevice(options?)`
  Open an `SPIDevice` on the bus. `options` can include `{ cs, mode, freqHz, queueSize, csHigh, lsbFirst, directExternalDma, timeoutMs }`. `directExternalDma` defaults to `false`; external memory is staged unless it is explicitly enabled. `timeoutMs` defaults to `1000` and must be in `1..60000`. `cs` defaults to `spi.DEFAULT_CS`, which can be `-1` when chip-select is managed manually in JS or external hardware.

`SPIDevice` methods:

- `device.status()`
  Return `{ opened, host, cs, mode, requestedFreqHz, actualFreqHz, queueSize, csHigh, lsbFirst, directExternalDma, timeoutMs, dmaStagingBytes, faulted, lastErrorCode }`.
- `device.close()`
  Remove the device from its parent SPI bus and make the JS object stale only
  after native removal succeeds. A removal failure throws and retains the
  handle in close-only state for another `close()` attempt; the next
  `bus.openDevice()` can also retry orphaned device cleanup.
- `device.transfer(data, options?)`
  Perform a full-duplex operation from an array-like sequence
  of bytes or native byte view and return the received bytes as an owned
  `ByteView`. `options.timeoutMs` overrides the device default for this operation.
- `device.write(data, options?)`
  Perform a write-only operation from an array-like sequence of bytes or native byte view and return the number of transmitted bytes. `options.timeoutMs` overrides the device default.
- `device.writeChunks(chunks, options?)`
  Queue an array-like list of byte-source spans for write-only SPI transfers. `options.queueDepth` defaults to `2` and is capped by the device queue size and the two fixed staging slots; `options.timeoutMs` overrides the device default. Internal DMA sources are direct, external DMA sources are direct only when `directExternalDma` is enabled, and all other sources use the bus staging workspace. The method returns `{ bytes, sourceSpans, transactions, path, stagedBytes, copyUs, queueUs, waitUs, transferUs, totalUs, queueDepth }`; `path` is `direct-internal`, `direct-external`, `staged-internal`, or `mixed`.
- `device.writeSource(source, options?)`
  Queue spans from a retained native `ByteSpanSource`, such as `Bitmap.createSpanSource(...)`, without materializing a JavaScript chunk array. SPI treats the source as a generic transport capability; it does not inspect Bitmap internals. `options.queueDepth` and the returned stats object match `writeChunks(...)`.
- `device.read(length, { fillByte = 0, timeoutMs? }?)`
  Clock `length` bytes and return an owned `ByteView` from MISO. `fillByte`
  controls the dummy value shifted out on MOSI while reading.

All five transaction methods are registered native Future drivers. Direct calls
wait cooperatively; `Future.call()` returns after capture and before hardware
I/O. Operations share one bounded FIFO lane per SPI host, so devices on one
controller stay ordered while separate hosts may progress independently. A
bulk write incrementally consumes its captured chunks or retained
`ByteSpanSource`; `ByteView` read leases, source owners, and staging slots
remain retained until ESP-IDF returns each queued transaction. Splitting one source span keeps the
bus acquired and uses `SPI_TRANS_CS_KEEP_ACTIVE`, so CS stays continuous across
that span.
ESP-IDF requires `portMAX_DELAY` for bus acquisition; SPI invokes it only after
the per-bus Future lane proves there is no earlier transaction in flight, so it
completes immediately. Queue and completion collection remain zero-wait calls,
with ISR wakeups returning control to the runtime poller.

Each operation has an overall deadline plus a no-progress deadline of
`max(100 ms, 4 * theoretical wire time + 50 ms)`, capped by the remaining
overall deadline. At each runtime poll SPI first drains every completion that
ESP-IDF has already published, then evaluates the no-progress deadline. A late
runtime poll therefore does not turn completed DMA work into a timeout; the
deadline expires only while an in-flight transaction still has no observable
completion. SPI does not retry with a different frequency, chunk size, or
memory path. Structured failures use `DMA_STAGING_NO_MEMORY`,
`DMA_TX_UNDERFLOW`, `DMA_RX_OVERFLOW`, `DMA_TRANSFER_TIMEOUT`, or
`DMA_DEVICE_FAULTED`. Their `details` include the ESP error, completed byte
count, DMA path, and requested/actual frequencies without payload data. A
timed-out queued transaction is retained until ESP-IDF returns it; if stopping
cannot be confirmed, the device becomes faulted and rejects new operations.

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
