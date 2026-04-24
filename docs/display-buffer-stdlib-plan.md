# Display Buffer Standard Library Plan

This document proposes a built-in display buffer standard library for firmware-side drawing and flush preparation.
The goal is to keep high-level display drivers and UI widgets in JavaScript, while moving the hot pixel buffer operations into C.

## Implementation Status

The first native implementation is now in place:

- `CONFIG_ESP32_MQUICKJS_FEATURE_DISPLAY_BUFFER` gates the module and `esp32.info().features.displayBuffer` reports availability.
- `displayBuffer.create(...)` supports `mono1` and `rgb565`, layouts `linear` and `page-y8`, native allocation, close/finalizer cleanup, metadata, dirty bounds, drawing primitives, and rectangle export.
- The shared byte-source helper is used by SPI and I2C writes, so native byte views returned by `readRect(...)` and `readRectChunks(...)` can be passed directly to transport writes.
- The ST7789 JavaScript driver uses a native RGB565 buffer when available and keeps panel command sequencing in JavaScript.

Still deferred from this plan: future formats such as `gray4`/`rgb888`, `page-rows`, multi-rect dirty tracking, `copyFrom(...)`, `blitFrom(...)`, and any transport-specific display shortcut.

## Background

The current JavaScript display stack is useful for quick experiments, but performance profiling on `xiao_esp32s3` with a 240x240 ST7789 screen shows the bottleneck is not SPI throughput.
At 40MHz SPI, the data write portion is close to the theoretical bus limit.
Most frame time is spent in JavaScript pixel work:

- `MonoSurface.fillRect()` and `drawText()` call `setPixel()` per pixel.
- ST7789 flush converts the mono bit buffer to RGB565 in JavaScript.
- Large dirty regions trigger thousands of JS function calls even when the final SPI payload is small enough.

The next useful abstraction is a generic native buffer layer that display drivers can reuse.
It should not be tied to ST7789 or SSD1306, and it should not require modifying the `mquickjs` vendor submodule.

## Goals

- Provide a reusable C-backed display buffer that can be created from JavaScript.
- Support different screen sizes without fixed compile-time dimensions.
- Support both full-frame and paged buffer modes.
- Support pixel formats needed by current and near-future display drivers.
- Provide fast drawing primitives for the common UI operations.
- Expose enough metadata for JS drivers to flush the buffer correctly.
- Keep device transport separate: SPI/I2C drivers still own panel commands and bus writes.
- Keep C APIs low-level and focused on heavy buffer work; policy and orchestration stay in JavaScript.
- Gate the feature per board with a normal ESP-IDF config symbol.

## Non-Goals

- Do not build a complete UI framework in C.
- Do not make panel drivers such as ST7789 mandatory firmware features.
- Do not change the `mquickjs` vendor API for typed array shortcuts.
- Do not expose raw internal pointers directly to JavaScript.
- Do not require every display to use a full framebuffer.
- Do not manage animation loops, buffer swapping policy, frame pacing, or display-driver state in C.

## Feature Gate

Recommended Kconfig symbol:

```text
CONFIG_ESP32_MQUICKJS_FEATURE_DISPLAY_BUFFER
```

Runtime discovery should expose:

```js
esp32.info().features.displayBuffer === true
```

If disabled, the `displayBuffer` module is not registered.
JS-side display libraries should fall back to pure JS buffers or fail with a clear error depending on the driver.

## Module Shape

Recommended global module name:

```js
displayBuffer
```

This module is a low-level buffer primitive, not a complete display driver.
The existing JS `display` library can use it internally when available.
JavaScript owns higher-level management such as front/back buffer selection, dirty-region policy, frame scheduling, and panel command sequencing.

Example:

```js
var fb = displayBuffer.create({
  width: 240,
  height: 240,
  format: "rgb565",
  storage: "psram",
  chunkBytes: 32768
});

fb.clear(0x0000);
fb.fillRect(10, 10, 80, 24, 0xffff);
fb.drawText(16, 18, "ESP32QJS", 0xffff);
```

Paged example for small mono displays:

```js
var mono = displayBuffer.create({
  width: 128,
  height: 64,
  format: "mono1",
  layout: "page-y8",
  pageHeight: 8
});

mono.clear(false);
mono.drawText(0, 0, "ready", true);
```

## Creation Options

`displayBuffer.create(options)` should accept:

- `width`: required logical width in pixels.
- `height`: required logical height in pixels.
- `format`: required pixel format.
- `layout`: optional memory layout, default depends on `format`.
- `storage`: optional allocation target, default `"auto"`.
- `stride`: optional bytes per row or page row.
- `pageHeight`: optional page height for paged layouts.
- `chunkBytes`: optional preferred chunk size for exporting flush data.
- `foreground`: optional default draw color.
- `background`: optional default clear color.

Recommended `storage` values:

- `"auto"`: use internal RAM when small, PSRAM when large.
- `"internal"`: prefer internal heap.
- `"psram"`: prefer PSRAM heap.
- `"dma"`: allocate DMA-capable memory when the whole buffer must be bus-writeable.

Large RGB framebuffers on ESP32-S3 should normally use PSRAM plus a smaller DMA-capable staging chunk.

## Pixel Formats

Initial formats:

- `mono1`: 1 bit per pixel. Suitable for SSD1306 and low-memory masks.
- `rgb565`: 16 bits per pixel. Suitable for ST7789 and most SPI TFT panels.

Future formats:

- `gray4`
- `gray8`
- `rgb332`
- `rgb888`

`rgb565` byte order should be explicit when exported:

- `rgb565be`: high byte first, suitable for most SPI TFT RAM writes.
- `rgb565le`: low byte first, useful for internal operations or panels that require it.

## Layouts

Recommended layouts:

- `linear`: row-major pixels. Default for `rgb565`.
- `page-y8`: vertical 8-pixel pages. Default for `mono1`, matching SSD1306-style memory.
- `page-rows`: caller-defined row groups for partial update buffers.

The buffer object should expose:

```js
fb.width
fb.height
fb.format
fb.layout
fb.stride
fb.pageHeight
fb.byteLength
```

## Drawing API

Initial methods:

```js
fb.clear(color?)
fb.fill(color)
fb.setPixel(x, y, color)
fb.getPixel(x, y)
fb.fillRect(x, y, width, height, color)
fb.drawRect(x, y, width, height, color)
fb.drawLine(x0, y0, x1, y1, color)
fb.drawBitmap(x, y, bitmap, options?)
fb.drawText(x, y, text, color, options?)
fb.measureText(text, options?)
```

The first implementation should prioritize:

- `clear`
- `fillRect`
- `drawRect`
- `drawText`
- `measureText`
- `flush/export` helpers

Those cover the current UI demo and the measured hot paths.

## Buffer Ownership and Double Buffering

The module should not provide an automatic double-buffer manager.
Double buffering is useful for some applications, but it is policy rather than a low-level primitive.
JavaScript should create and manage multiple buffers when needed:

```js
var front = displayBuffer.create({ width: 240, height: 240, format: "rgb565" });
var back = displayBuffer.create({ width: 240, height: 240, format: "rgb565" });

back.clear(0x0000);
back.drawText(10, 10, "frame", 0xffff);
screen.flushBuffer(back);

var tmp = front;
front = back;
back = tmp;
```

For SPI TFT panels such as ST7789, this is not hardware page flipping.
The selected buffer still has to be flushed over SPI.
The value of double buffering is avoiding partially drawn intermediate states and making the JS rendering model cleaner.

C-side helper methods should stay primitive:

```js
back.copyFrom(front, rect?)
back.blitFrom(source, sx, sy, width, height, dx, dy)
```

These helpers are acceptable because they move heavy memory copies into C.
They should not decide when to swap buffers or when to flush the panel.

## Dirty Regions

The buffer should optionally track dirty bounds:

```js
fb.getDirty()
fb.clearDirty()
fb.markDirty(x, y, width, height)
```

Recommended first version:

- Track one bounding dirty rectangle.
- Let JS drivers decide whether to flush it as one rectangle or split it.
- Add multi-rect tracking only after there is evidence that it helps.

## Export and Flush Helpers

Transport should remain outside the buffer module.
The buffer prepares bytes; the display driver sends them.
The interface between buffers and transports should be a shared byte-source abstraction, not protocol-specific APIs.

Recommended export API:

```js
var chunk = fb.readRect(x, y, width, height, options?);
var chunks = fb.readRectChunks(x, y, width, height, options?);
```

`readRect(...)` should return a generic native byte view.
It is not an SPI object, not an I2C object, and not a display-driver object.
It is only a byte source with a pointer, length, and owner reference.

Example:

```js
var chunk = fb.readRect(0, 0, 240, 32, { byteOrder: "be" });

spiDevice.write(chunk);
i2c.write(0x3c, chunk);
```

In this example:

- `displayBuffer` owns memory and drawing primitives.
- `chunk` is a generic byte source.
- `spi` and `i2c` own their protocol-specific bus/device lifecycle.
- Neither transport module needs to know that the bytes came from a display buffer.

## Shared Byte Source Contract

Add a small internal C contract that any module can produce or consume:

```c
typedef struct {
    const uint8_t *data;
    size_t length;
    JSValue owner;
} esp32_mquickjs_byte_source_t;
```

Recommended helper functions:

```c
bool esp32_mquickjs_get_byte_source(JSContext *ctx,
                                    JSValue value,
                                    const char *api_name,
                                    esp32_mquickjs_byte_source_t *out,
                                    uint8_t **out_owned,
                                    JSValue *out_error);

JSValue esp32_mquickjs_new_byte_view(JSContext *ctx,
                                     JSValue owner,
                                     const uint8_t *data,
                                     size_t length);
```

The shared helper should support:

- Native byte views produced by `displayBuffer`.
- Existing JS array-like byte sequences by copying into owned memory.
- Future byte producers such as file slices or stream buffers.

Transport modules should call only the shared helper.
They should not inspect `displayBuffer` classes, pixel formats, or layouts.
Buffer modules should create byte views only.
They should not store SPI/I2C handles or call transport functions.

The `owner` reference keeps the producer object alive while a byte view exists.
If the view is temporary and used synchronously, the consumer can release it after the C call.
If a future async transport is added, it must retain the owner until the transfer completes.

## Byte Source vs Stream

`ByteSource` and `Stream` should remain separate concepts.
They solve different problems and should not be collapsed into one API.

`ByteSource` is a low-level memory view:

- It represents bytes that already exist in memory.
- It is optimized for synchronous native calls into lower-level protocols.
- It is suitable for SPI/I2C writes, display chunks, packet payloads, and other bounded memory transfers.
- It should have simple lifetime rules based on an owner reference.
- It should not expose async reads, backpressure, transforms, or buffering policy.

`Stream` is an IO abstraction:

- It represents data that may arrive over time.
- It can support asynchronous reads and writes.
- It is the right place for transforms, decoding, encoding, compression, splitting, joining, and adapters.
- It is suitable for files, HTTP bodies, sockets, serial IO, and future pipeline-style APIs.
- It owns IO flow concerns such as blocking, async completion, backpressure, and end-of-stream.

The conversion direction should be explicit:

```js
var chunk = fb.readRect(0, 0, 240, 32);
spiDevice.write(chunk);       // ByteSource path

var body = response.body;     // Stream path
var text = body.text();
```

Future bridge helpers may exist, but they must be explicit:

- `stream.write(byteSource)` can copy or enqueue a bounded memory block into an IO stream.
- `stream.readBytes(maxLength)` can return a `ByteSource` or byte array for one bounded read.
- `displayBuffer` should not become a `Stream`.
- `Stream` should not become the default transport type for SPI/I2C display flushes.

This separation keeps display flushing deterministic and cheap, while preserving `Stream` as the higher-level async IO and transformation abstraction.

## Object Lifetime

Buffers are native-backed JS objects and must be GC-safe.

Required behavior:

- `fb.close()` releases native memory immediately.
- GC finalization releases memory if `close()` was not called.
- Methods throw a clear error after close.
- Allocation failures throw an exception, not a partial object.

Example:

```js
var fb = displayBuffer.create({ width: 240, height: 240, format: "rgb565" });
try {
  fb.fillRect(0, 0, 240, 240, 0xffff);
} finally {
  fb.close();
}
```

## Memory Budget

Typical buffer sizes:

- 128x64 `mono1`: 1024 bytes.
- 240x240 `mono1`: 7200 bytes.
- 240x240 `rgb565`: 115200 bytes.
- 320x240 `rgb565`: 153600 bytes.
- 320x240 `rgb565` with 32KB staging chunk: about 185KB total.

Rules:

- Full `rgb565` buffers should prefer PSRAM on ESP32-S3 boards.
- DMA staging chunks should be bounded by `chunkBytes`.
- Small `mono1` buffers can use internal RAM.
- Board profiles with low RAM can disable the feature.

## Relationship To Existing JS Display Library

The JS `display` library should remain the user-facing display abstraction.
It can choose a native buffer when available:

```js
if (esp32.info().features.displayBuffer) {
  // use displayBuffer-backed surface
} else {
  // use pure JS MonoSurface fallback
}
```

For ST7789:

- Replace JS `MonoSurface` inheritance with a `displayBuffer`-backed surface.
- Use `rgb565` format to avoid mono-to-RGB565 conversion on every flush.
- Keep panel command sequencing in JS for the first version.
- Flush through generic byte views consumed by `spi`.
- Add transport-specific native shortcuts only if a future measurement proves the shared byte-source path is still a bottleneck.

For SSD1306:

- Use `mono1` with `page-y8` layout.
- Existing I2C command/data write logic can stay in JS initially.

## Proposed C Implementation

Suggested files:

- `components/esp32_mquickjs/src/core/esp32_mquickjs_byte_source.c`
- `components/esp32_mquickjs/src/core/esp32_mquickjs_byte_source.h`
- `components/esp32_mquickjs/src/modules/display_buffer/esp32_mquickjs_display_buffer.c`
- `components/esp32_mquickjs/src/modules/display_buffer/esp32_mquickjs_display_buffer.h`
- `components/esp32_mquickjs/src/modules/display_buffer/CMakeLists.txt` if module-local build structure is introduced later.
- stdlib registration in `components/esp32_mquickjs/src/core/mqjs_stdlib_esp32.c`.

Native object state:

```c
typedef struct {
    uint16_t width;
    uint16_t height;
    uint16_t stride;
    uint16_t page_height;
    uint8_t format;
    uint8_t layout;
    uint8_t storage;
    uint8_t closed;
    size_t byte_length;
    uint8_t *data;
    uint8_t *chunk;
    size_t chunk_size;
    int dirty_x0;
    int dirty_y0;
    int dirty_x1;
    int dirty_y1;
} esp32_mquickjs_display_buffer_t;
```

Implementation rules:

- Keep all per-pixel loops in C.
- Keep color normalization simple and deterministic.
- Clamp rectangles instead of throwing for out-of-bounds drawing.
- Throw for invalid formats, invalid dimensions, and allocation failures.
- Avoid dynamic allocation inside hot drawing loops.
- Do not store references to SPI/I2C devices or panel objects inside the buffer.
- Do not run timers, callbacks, or background flush tasks from this module.

## Testing Plan

Host C tests:

- Verify shared byte-source helper accepts byte views and existing array-like byte inputs.
- Verify byte views keep their owner alive for the duration of the view.
- Create and close buffers for each supported format/layout.
- Verify byte length and stride calculation.
- Verify `clear`, `setPixel`, `fillRect`, `drawRect`, and `drawText` output bytes.
- Verify `copyFrom`/`blitFrom` when those helpers are added.
- Verify dirty rectangle tracking.
- Verify invalid options fail.

Board JS tests:

- Module existence follows `esp32.info().features.displayBuffer`.
- Basic draw API works.
- Paged `mono1` buffer matches expected SSD1306 byte layout.
- `rgb565` buffer exports high-byte-first pixel data.
- `spi.write(...)` and `i2c.write(...)` accept generic byte views without knowing they came from `displayBuffer`.
- GC/close behavior does not crash repeated allocation.

Display validation:

- ST7789 demo on `xiao_esp32s3`.
- SSD1306 smoke test if hardware is available.
- Compare FPS before and after replacing JS `MonoSurface` hot paths.

## Milestones

1. Add the shared byte-source helper and native byte-view object.
2. Extend `spi` and `i2c` to consume the shared byte-source helper.
3. Add feature gate and empty `displayBuffer` module.
4. Implement native buffer allocation, close, metadata, and `clear`.
5. Add `mono1` and `rgb565` draw primitives.
6. Add dirty tracking and `readRect`/chunk export as generic byte views.
7. Port ST7789 JS driver to `displayBuffer` while keeping panel commands in JS.
8. Measure again and decide whether any transport-specific native shortcut is justified.

## Open Questions

- Should the public module name stay `displayBuffer`, or be shortened before API freeze?
- Should `readRectChunks(...)` return an iterator-like object, an array of byte views, or require callback iteration to avoid allocations?
- Should `rgb565` buffers store big-endian bytes directly for SPI, or native-endian pixels plus conversion on export?
- Should text rendering stay fixed 5x7 initially, or should font registration be part of the first API?
- Should panel-specific helpers live under this module or in separate modules such as `st7789` later?
