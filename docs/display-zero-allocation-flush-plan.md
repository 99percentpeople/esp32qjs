# Display Zero-Allocation Flush Plan

This document defines the preferred long-term design for display flush paths.
It is a follow-up to the `readRectChunks({ reuse: true })` leak fix: that fix
stops per-frame `ByteView` wrapper growth, but the better architecture is to
avoid JS-visible chunks in high-frequency flush loops entirely.

## Problem

The current ST7789 fast path is:

```js
chunks = buffer.readRectChunks(x, y, width, height, options);
device.writeChunks(chunks, spiOptions);
```

That shape is convenient, but it exposes an implementation detail as a public
JS array. The old implementation created a fresh array and fresh `ByteView`
objects every frame. Each `ByteView` also had a native heap allocation for its
wrapper state, so a 20 FPS display loop could allocate dozens of native wrappers
per second.

`reuse: true` is a practical stopgap because direct RGB565 full-row exports can
reuse the same array and `ByteView` wrappers. It should remain available for
debugging and compatibility, but display drivers should move to a retained
native span source instead.

## Goals

- Keep the hot flush path at zero JS object allocation per frame.
- Keep native wrapper allocation bounded to display initialization time.
- Preserve the existing design boundary: `ByteSource` is the low-level in-memory
  payload contract, while `Stream` stays for async IO and transforms.
- Avoid protocol-specific coupling such as `displayBuffer.writeToSpi(...)`.
  Display buffers should produce byte spans; transports should consume byte
  spans.
- Let JavaScript own driver policy: dirty rectangle merging, display commands,
  rotation, and mode-specific choices stay in JS.
- Let C own memory safety: source lifetime, DMA queue ownership, span iteration,
  and scratch-buffer reuse stay in native code.
- Support RGB565 now and leave a clean path for mono and grayscale displays.

## Proposed Public Shape

Create a retained span source from a `DisplayBuffer`, then update its rectangle
before each write:

```js
var source = buffer.createSpanSource({
  byteOrder: "be",
  chunkBytes: 32768
});

source.setRect(x, y, width, height);
device.writeSource(source, { queueDepth: 2 });
```

Naming can still change before implementation. The important API properties are:

- The source object is created once and reused.
- `setRect(...)` mutates native source state without allocating chunk wrappers.
- `writeSource(...)` accepts a generic byte span source, not a display-specific
  type.
- Existing `write(...)` and `writeChunks(...)` can remain for simple payloads and
  diagnostics.

If the transport API is consolidated later, `writeSource(source, options)` can
be folded into `write(source, options)` as long as the underlying C path still
uses span iteration instead of materializing JS arrays.

## Native Contract

Add an internal span interface under `components/esp32_mquickjs/internal/utils/`.

Conceptual shape:

```c
typedef struct {
    const uint8_t *data;
    size_t length;
    JSValue owner;
    bool dma_capable;
} esp32_mquickjs_byte_span_t;

typedef struct {
    void *opaque;
    bool (*next)(JSContext *ctx, void *opaque, esp32_mquickjs_byte_span_t *out);
    void (*close)(JSContext *ctx, void *opaque);
} esp32_mquickjs_byte_span_source_t;
```

Rules:

- `next(...)` returns contiguous byte spans until exhausted.
- `owner` is rooted while a DMA transaction using the span is in flight.
- The source may use retained scratch buffers for conversion, but not per-frame
  heap allocation.
- A direct DMA-capable span can point into the display framebuffer.
- A non-DMA or converted span is copied into a retained transport DMA buffer.
- The C helper must have one release path for success, JS exceptions, and SPI
  transaction errors.

## DisplayBuffer Source

`DisplayBuffer.createSpanSource(options?)` returns a native object bound to one
buffer. It stores:

- the owner `DisplayBuffer`
- current clamped rectangle
- byte order
- chunk byte limit
- retained scratch buffer, only when conversion is required
- format/layout-specific iterator state

Fast paths:

- `rgb565` + linear layout + `byteOrder: "be"` + full-row span:
  return direct framebuffer spans.
- `mono1` + page layout + page-aligned rectangle:
  return direct page spans.

Fallback paths:

- Partial-row RGB565 or endian conversion:
  encode into a retained scratch span.
- Future grayscale or packed formats:
  provide format-specific encoder callbacks using the same span contract.

The fallback must still avoid allocating a JS array or `ByteView` per frame.

## SPI Consumption

`SPIDevice.writeSource(source, options?)` should:

- open the generic byte span source through a shared helper
- queue up to the configured SPI queue depth
- root the span owner while a direct DMA transaction is in flight
- release the owner root as soon as the queued transaction completes
- reuse the existing two-slot DMA staging buffers for copied spans
- return the same kind of stats object as `writeChunks(...)`

This keeps SPI unaware of display buffer internals. It only consumes spans.

## JavaScript Driver Flow

The ST7789 driver should allocate its span source once:

```js
this.flushSource = this.nativeBuffer.createSpanSource({
  byteOrder: "be",
  chunkBytes: this.chunkBytes
});
```

Then `flushRect(...)` becomes:

```js
this._setWindow(x, y, width, height);
this.flushSource.setRect(x, y, width, height);
this.device.writeSource(this.flushSource, {
  queueDepth: this.deviceOptions.queueSize
});
```

Dirty rectangle merge policy remains in JS. The native API only moves bytes.

## Migration Plan

1. Add the internal span source contract and tests for open, next, close, and
   owner rooting.
2. Implement `DisplayBuffer.createSpanSource(...)` for RGB565 direct full-row
   spans.
3. Add `SPIDevice.writeSource(...)` using the shared span helper and existing SPI
   queue logic.
4. Move ST7789 from `readRectChunks({ reuse: true })` to the retained source.
5. Add mono/page-layout support so SSD1306 can use the same model when useful.
6. Keep `readRect(...)` and `readRectChunks(...)` as inspection/export APIs.
7. After one stable pass, document `reuse: true` as a compatibility/debug hint,
   not the recommended display flush mechanism.

## Validation

Required tests:

- Host C tests for span iteration and release paths.
- JS tests for `DisplayBuffer.createSpanSource(...)` rectangle clamping and
  repeated `setRect(...)`.
- SPI JS tests that `writeSource(...)` rejects invalid inputs and returns stats.
- Board-backed ST7789 run for at least several full mode cycles.

Acceptance criteria on `xiao_esp32s3` ST7789:

- `esp32.freeHeap()` remains stable across FULL, PART, SHAPE, and TEXT modes.
- `gc()` after stopping the demo does not reveal deferred per-frame growth.
- FULL mode performance is not worse than the current `reuse: true` path.
- Direct DMA stats remain visible so future performance work has evidence.

## Non-Goals

- Do not move display command sequencing into C.
- Do not add a SPI-specific method to `DisplayBuffer`.
- Do not make `Stream` responsible for bounded framebuffer payloads.
- Do not remove `ByteView`; it is still useful for low-frequency export,
  inspection, and generic payload handoff.
