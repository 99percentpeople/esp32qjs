# `bitmap` Module

This module exposes native Bitmaps for heavy pixel work. It is registered only when `sys.info.features.bitmap` is enabled. Compressed JPEG decoding is a separate optional `bitmap_jpeg` subfeature reported by `sys.info.features.bitmapJpeg`; disabling it leaves the raw Bitmap formats and transforms available without installing `Bitmap.prototype.decode`. The JS `Surface` owns rendering, `PanelDriver` owns controller sequencing, and `DisplayTransport` owns SPI/I2C/GPIO operations; `bitmap` only owns pixels, transforms, and exported bytes.

- `bitmap.MONO1`
  Pixel format string `"mono1"`.
- `bitmap.GRAY4`
  Pixel format string `"gray4"`.
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
- `format: "gray4"`
  Two 4-bit luminance pixels per byte in linear layout. Even `x` uses the high
  nibble and odd `x` uses the low nibble; stride defaults to
  `ceil(width / 2)`. Colors are numeric values from `0` through `15`. Native
  conversion expands a level with `level * 17` and quantizes gray8 with
  `value >> 4`.
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
  byteOrder?, bitOrder? }`. Raw formats are `"mono1"`, `"gray4"`, `"gray8"`,
  `"rgb565"`, and `"rgb888"`. RGB565 descriptors accept
  `byteOrder: "be" | "le"`; mono1
  descriptors accept `layout: "linear" | "page-y8"` and
  `bitOrder: "lsb" | "msb"`.

  Options are `{ sourceRect?, destinationRect?, rotation?, flipX?, flipY?,
  filter?, normalize?, threshold?, dither? }`. Rotation is clockwise
  `0`, `90`, `180`, or `270`. Processing order is crop, rotate, flip, resize,
  and encode. `normalize` applies only to grayscale output; `threshold` and
  `dither: "bayer4x4"` apply only to mono1. The Bayer matrix is anchored to
  absolute destination coordinates so tiled blits have no seams.

  Bitmap, CameraFrame, and ByteView inputs are read-leased while the worker is
  active and the target is write-leased. Busy close or mutation attempts fail
  clearly. Array-like pixels are copied to native staging memory before work.
  In-place or aliased blits are rejected. Cancellation may preserve completed
  rows and conservatively marks the full clipped destination dirty.
- `blitBatch(operations)`
  Apply `1..16` ordered `{ source, options? }` blits through one native worker
  submission and return the same target Bitmap. Sources and the target retain
  the same checked leases as `blit()`. Later operations observe and may
  overwrite earlier results; dirty bounds include every operation that
  completed or wrote partial rows before cancellation. This API is intended
  for tiled or striped sources that should not pay one Future dispatch per
  region.
- `decode(source, { codec: "jpeg", destinationRect? })`
  Available only when the Build Context selects `bitmap_jpeg` and
  `sys.info.features.bitmapJpeg` is `true`.
  Decode baseline JPEG into this linear RGB565 Bitmap. `source` may be one
  `ByteSource` or `{ chunks, lengths, byteLength }`, where each length selects
  the useful prefix of its corresponding transport chunk. Input staging is
  bounded by `CONFIG_ESP32_MQUICKJS_BITMAP_JPEG_MAX_INPUT_BYTES`; temporary
  RGB565 output is bounded by
  `CONFIG_ESP32_MQUICKJS_BITMAP_JPEG_MAX_OUTPUT_BYTES`. The target is
  write-leased until the native Future worker publishes the complete image.
  The result reports `{ codec, engine, width, height, inputBytes,
  outputBytes }`; `engine` is `"rom-tjpgd"` on supported ROM targets and
  `"software-tjpgd"` otherwise. Scaling and progressive JPEG are not accepted.
- `drawText(x, y, text, options?)`
  Draw text with `options.color` and `options.font`, a `DisplayFont` returned by `bitmap.loadFont(...)`. `options.spacing` controls extra inter-character pixels. Text background is transparent by default; pass `options.background` to fill each glyph cell before drawing, or `null` to keep it transparent explicitly.
- `measureText(text, options?)`
  Return `{ width, height, lines }`.
- `getDirty()`
  Return `{ x, y, width, height }` for the current bounding dirty rectangle, or `null`.
- `clearDirty()`
- `markDirty(x, y, width, height)`
- `readRect(x, y, width, height, options?)`
  Return a stable owned `ByteView` snapshot for the clamped rectangle.
- `readRectChunks(x, y, width, height, options?)`
  Return an array of stable owned `ByteView` snapshots split by
  `options.chunkBytes` or the buffer's `chunkBytes`.
- `createSpanSource(options?)`
  Return a retained native `BitmapSpanSource` bound to the buffer. Its exact
  `byteLength` follows the current clamped rectangle, so bounded transports
  such as ESP-NOW can validate it before opening the source. Pass it to
  `SPIDevice.writeSource(source, options?)` to flush without allocating JS
  chunk arrays or ByteView wrappers in the loop.
- `createCommandBuffer(options?)`
  Return a retained native `DisplayCommandBuffer` for recording drawing commands and replaying them into a `Bitmap`. Options are `{ commandCapacity, textBytes }`.

`BitmapSpanSource` methods:

- `source.setRect(x, y, width, height)`
  Update the clamped export rectangle and `byteLength`, then return the same
  source for reuse in display or packet loops. This method belongs to
  Bitmap-created sources, not to the generic `ByteSpanSource` transport
  capability.

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
python3 scripts/font_to_eqf.py input.bdf /path/to/library/flash_data/_sys/display/fonts/my.eqf \
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
python3 scripts/font_to_eqf.py --format manifest /path/to/library/flash_data/_sys/fonts/droid-cjk.json
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
Native byte views expose `length`, `byteLength`, `getUint8(offset)`, and
`toArray()`. `getUint8(...)` requires an in-range non-negative integer and is
suited to inspecting a small protocol header without allocating an array. Every
`ByteView` owns a stable immutable snapshot until it is closed or collected.
They can be passed directly to `spi`, `i2c`, and `uart` writes without
converting to a JavaScript array.
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
