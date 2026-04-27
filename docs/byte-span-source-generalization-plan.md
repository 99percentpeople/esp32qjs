# ByteSpanSource Generalization Plan

This document defines the follow-up API cleanup after the display
zero-allocation flush path. The flush path now works, but the public source
shape should be cleaned up before the transport APIs are called stable.

## Problem

`ByteSpanSource` is intended to be a generic transport input: a native object
that can be opened by a transport and iterated as contiguous byte spans.

The current public object also exposes `setRect(...)`. That method is useful
for display buffers because a display export has a rectangular window, but it
is not a generic byte-span concept. Future producers may use different shapes:

- a file-backed source may expose an offset and length
- a camera source may expose a frame plane
- a UART helper may not need any shape mutation at all

Keeping `setRect(...)` on the generic source makes the base abstraction look
display-specific and makes future transport users inherit a method that does
not belong to them.

## Goals

- Keep `ByteSource` as the simple one-span byte payload contract.
- Keep `ByteSpanSource` as the generic native multi-span producer consumed by
  transports.
- Move display-specific shape mutation out of the generic `ByteSpanSource`
  surface.
- Preserve one `DisplayBuffer` owning multiple span sources, each with its own
  rectangle and export options. This keeps double buffering, alternate byte
  order, and multiple flush windows practical.
- Keep display command sequencing and flush policy in JavaScript display
  drivers.
- Keep SPI, I2C, and future UART modules unaware of display buffer internals.

## Proposed Public Shape

The generic transport source should be opaque:

```ts
interface ByteSpanSource {
  // Transport capability only. No display-specific methods.
}
```

`DisplayBuffer.createSpanSource(...)` should return a display-specific source
handle:

```ts
interface DisplayBufferSpanSource extends ByteSpanSource {
  setRect(x: number, y: number, width: number, height: number): this;
}

class DisplayBuffer {
  createSpanSource(options?: DisplaySpanSourceOptions): DisplayBufferSpanSource;
}
```

The display driver flow stays simple:

```js
var source = buffer.createSpanSource({
  byteOrder: "be",
  chunkBytes: 4092
});

source.setRect(x, y, width, height);
device.writeSource(source, { queueDepth: 2 });
```

The important boundary is that `setRect(...)` belongs to
`DisplayBufferSpanSource`, not to every possible `ByteSpanSource`.

## Transport Contract

Transport modules should accept only the generic capability:

```ts
interface SPIDevice {
  writeSource(source: ByteSpanSource, options?: SPIWriteOptions): SPIWriteStats;
}
```

This keeps SPI generic. It consumes spans and does not inspect whether the
producer is a display buffer, a file, a camera frame, or another native source.

I2C should keep `write(addr, ByteSource)` and
`writeChunks(addr, ByteSource[])` as the stable baseline. Add
`writeSource(addr, ByteSpanSource)` only if a measured I2C display or bulk-write
path needs the zero-allocation source model.

Future UART should start with stream/serial semantics and
`write(ByteSource)`. Add `writeSource(ByteSpanSource)` only for a real
high-volume synchronous dump use case; do not force UART to mirror SPI.

## Native Shape

The internal C contract should remain producer-neutral:

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

Producer-specific methods such as `DisplayBufferSpanSource.setRect(...)`
should live on producer-specific JS prototypes while sharing the same internal
transport opener.

One practical implementation path is:

- keep the internal source object and owner-rooting logic shared
- let `esp32_mquickjs_new_byte_span_source(...)` choose a producer-specific
  public prototype
- make the transport opener validate the internal source brand rather than a
  display-specific class
- keep display rectangle state in the display source opaque data, not on the
  `DisplayBuffer` itself

## Migration Plan

1. Add a `DisplayBufferSpanSource` declaration and make `ByteSpanSource` opaque
   in TypeScript.
2. Move public `setRect(...)` documentation from `ByteSpanSource` to
   `DisplayBufferSpanSource`.
3. Rename native error strings from `ByteSpanSource.setRect(...)` to
   `DisplayBufferSpanSource.setRect(...)`.
4. Split the JS prototype so only display-buffer-created span sources expose
   `setRect(...)`.
5. Keep `SPIDevice.writeSource(...)` accepting the generic `ByteSpanSource`.
6. Update display-buffer JS tests to assert `setRect(...)` on
   `DisplayBufferSpanSource` and SPI tests to assert generic source acceptance.
7. Leave `DisplayBuffer.createSpanSource(...)` as the source factory; do not make
   `DisplayBuffer` itself inherit or implement `ByteSpanSource`.

## Compatibility

Existing display driver code can keep the same runtime call shape:

```js
source.setRect(x, y, width, height);
device.writeSource(source, options);
```

The cleanup is mostly about where the method is documented and typed. If a
runtime compatibility window is needed, the old generic class can keep
`setRect(...)` temporarily while docs and declarations move to the display
source subtype.

## Non-Goals

- Do not add display-specific methods to SPI, I2C, or UART.
- Do not make `DisplayBuffer` itself a `ByteSpanSource`.
- Do not use `Stream` for bounded framebuffer payloads.
- Do not require every future transport to implement `writeSource(...)`.
- Do not remove `ByteView`; it remains useful for diagnostics, snapshots, and
  simple byte handoff.
