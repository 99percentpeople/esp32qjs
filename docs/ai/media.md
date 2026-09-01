# Media API

## Availability and hardware constants

Read `sys.info.features.i2s` and `sys.info.features.camera`. Camera is registered
only in supported ESP32-S3 builds. The framework never infers a board model.

If `APP_BOARD_MODEL` is present, read the matching `doc://board/...` resource
from the current Artifact. Board wiring belongs there rather than in this
generic API document. The firmware repository does not infer a board or store
board overlays; flash and PSRAM parameters still come from the probed hardware
profile.

## I2S PCM channels

- `i2s.capabilities()` returns ports, standard RX/TX/duplex support, PDM RX,
  data widths, and 64 KiB read/write limits.
- `i2s.open(options)` returns one `I2SChannel`. Standard mode accepts
  `direction: "rx"`, `"tx"`, or `"duplex"`; PDM accepts only `"rx"`.
- `channel.start()` and `channel.stop()` are explicit and idempotent.
  `stop()` disables DMA while retaining the channel and DMA ring for reuse.
- `channel.read(frameCount, timeoutMs?)` is RX/duplex only. It returns `null` at
  timeout or `{ data, frames, byteLength, timestampUs, sequence, overruns }`.
  `data` is an owned `ByteView`; close it after the last consumer or `toArray()`.
- `channel.write(data, timeoutMs?)` is TX/duplex only. It accepts a `ByteSource`
  or `ByteSpanSource` and returns `{ frames, byteLength, timestampUs }`. The
  total byte length must align to the configured PCM frame, even when a frame
  crosses a span boundary.
- `channel.status()` reports direction, port, running/read/write state, PCM
  layout, DMA configuration, receive queue overruns, and
  `sendQueueOverflows`. Read `dma.storage`, `dma.bufferBytes`, and
  `dma.totalBufferBytes` for the actual persistent driver ring layout.
- `channel.close()` is idempotent and non-waiting. It cancels pending I/O and
  releases native handles after Future leases finish. Runtime teardown uses the
  same path.

Standard-I2S requires `mode: "standard"`. Options include `sampleRateHz`,
`dataBits`, `slotBits`, `slotMode: "mono" | "stereo"`,
`slotMask: "left" | "right" | "both"`,
`format: "philips" | "msb" | "pcmShort" | "pcmLong"`,
`pins: { bclk, ws, din?, dout?, mclk? }`, and DMA descriptor settings. The
default format is `"philips"`. RX requires `din`, TX requires `dout`, and
duplex requires both. One DMA descriptor must fit within 4092 bytes. Duplex
permits one read and one write concurrently; the same direction cannot be
re-entered. Use `Future.call()` for non-blocking scheduling.

Create the channel once after Wi-Fi initialization, then reuse `start()` and
`stop()` for recording/playback cycles. Do not reopen it or attempt several DMA
configurations as a low-memory fallback. On PSRAM boards, transient PCM buffers
use PSRAM while I2S DMA rings remain in DMA-capable internal memory; a staging
allocation failure reports `I2S_NO_MEMORY` without consuming that internal
reserve. Ring admission and lifetime accounting are owned by the framework
memory manager even though ESP-IDF performs the physical allocations. Compare
`sys.status.memory.manager.driverPinnedBytes`,
`.pendingDmaReservationBytes`, plus `sys.status.memory.internal`, `.dma`, and
`.psram`
`largestFreeBlockBytes` / `minimumFreeBytes` when diagnosing fragmentation.
The manager's `allocations` list attributes live PCM operation buffers to I2S,
camera frame copies to camera, and bitmap/JPEG pixels and decode buffers to
their native owners. Sensor-driver framebuffers and opaque DMA descriptors are
reported only when the driver exposes an exact size; the framework does not
estimate them from heap deltas.

PDM always yields signed 16-bit little-endian mono PCM:

```js
(function () {
    var channel = i2s.open({
        direction: "rx",
        mode: "pdm",
        port: "auto",
        sampleRateHz: 16000,
        dma: { descriptorCount: 6, framesPerDescriptor: 240 },
        timeoutMs: 1000
    });
    try {
        channel.start();
        var chunk = channel.read(320, 1000);
        if (chunk === null) return null;
        try {
            return {
                frames: chunk.frames,
                bytes: chunk.byteLength,
                sequence: chunk.sequence,
                overruns: chunk.overruns
            };
        } finally {
            chunk.data.close();
        }
    } finally {
        channel.stop();
        channel.close();
    }
})()
```

## Camera capture

- `camera.capabilities()` reports target, PSRAM, and the compiled OV2640/OV3660/OV5640
  drivers, formats, and frame sizes. The generic frame-size vocabulary includes
  the sensor driver's native square `128x128` mode.
- `camera.open(options?)` uses explicit pins over selected hardware constants.
  The resolved map must contain every required signal. It probes the sensor;
  callers never choose a sensor model. `psramDma` controls direct DMA into
  PSRAM independently from `bufferLocation`; `false` keeps DMA staging in
  internal memory before complete chunks are copied to PSRAM.
- `cam.capture(timeoutMs?)` returns one `CameraFrame` or `null`. Only one capture
  may be pending and only one framebuffer may be leased. A normal timeout
  returns `null` and leaves the camera open. Cancellation or exec interruption
  closes that camera instance after the native worker exits; do not reuse its
  handle.
- `cam.status().sensor.model` is `"ov2640"`, `"ov3660"`, or `"ov5640"` after probing.
- `cam.controls()` and `cam.setControl(name, value)` cover `frameSize`,
  `jpegQuality`, `brightness`, `contrast`, `saturation`, `horizontalMirror`,
  and `verticalFlip`.
- `frame.source({ chunkBytes? })` creates one active, one-shot
  `ByteSpanSource`; chunks are limited to 32768 bytes.
- `frame.read(offset?, limit?)` copies at most 32768 bytes for diagnostics;
  close the returned owned `ByteView` after inspection.
- `bitmap.convert(frame, options?)` and `Bitmap.blit(frame, options?)` accept a
  live raw grayscale, RGB565, or RGB888 frame without copying it into a
  JavaScript array. They can crop, rotate, flip, resize with `"nearest"` or
  `"bilinear"`, normalize contrast, convert through packed high-nibble-first
  `gray4`, and apply `"bayer4x4"` dithering to a `mono1` target. Conversion
  does not consume or close the frame.
- `Bitmap.blitBatch(operations)` applies `1..16` ordered
  `{ source, options? }` transforms through one native worker submission. It
  retains every source until completion, write-leases the target once, and is
  the bounded path for efficiently composing striped or tiled inputs.
- `Bitmap.decode(source, { codec: "jpeg", destinationRect? })` is installed
  only by the optional `bitmap_jpeg` native feature, reported as
  `sys.info.features.bitmapJpeg`; it decodes a baseline JPEG into a linear
  RGB565 target on the native Future worker. On
  SoCs with ROM TJpgDec, including ESP32-S3/C3/C5, the framework uses that ROM
  implementation. `source` may be one byte source or
  `{ chunks, lengths, byteLength }`; `lengths` makes it possible to ignore
  trailing transport headers without constructing a JavaScript byte array.
  Input and temporary RGB565 output are bounded by Build Context settings.
- JPEG remains a compressed codec, not a raw Bitmap format. BMP is not decoded
  by the current framework.
- `frame.close()` returns the framebuffer and rejects while its source is
  active. `cam.close()` irreversibly cancels active capture and immediately
  revokes derived frame and unopened source handles. Revoked frame operations
  fail as closed while `frame.close()` remains safe. Camera close waits only
  for active native bitmap/source readers and driver teardown; cancelling the
  caller does not cancel that cleanup. Timers, transports, and other Futures
  continue.
  Use `Future.call(cam.close, cam, [])` when the caller does not need to wait.

The safe default is JPEG, QVGA, quality 12, one PSRAM framebuffer, and
`whenEmpty`. Continuous acquisition is enabled only by explicitly pairing two
framebuffers with `latest`.

```js
(function () {
    var cam = camera.open({
        pixelFormat: "jpeg",
        frameSize: "qvga",
        jpegQuality: 12,
        frameBuffers: 1,
        grabMode: "whenEmpty",
        bufferLocation: "psram"
    });
    var frame = null;
    try {
        frame = cam.capture(5000);
        if (frame === null) return null;
        var view = frame.read(0, 2);
        try {
            var edge = view.toArray();
            return {
                model: cam.status().sensor.model,
                width: frame.width,
                height: frame.height,
                bytes: frame.byteLength,
                jpegSoi: edge[0] === 0xff && edge[1] === 0xd8
            };
        } finally {
            view.close();
        }
    } finally {
        if (frame !== null) frame.close();
        cam.close();
    }
})()
```

For a bounded monochrome preview, keep both the camera frame and the display
buffer native:

```js
function drawPreviewFrame(screen, cam) {
    var frame = null;
    try {
        frame = cam.capture(1000);
        if (frame === null) return false;
        screen.blit(frame, {
            sourceRect: { x: 0, y: 0, width: frame.width, height: frame.height },
            destinationRect: { x: 0, y: 0, width: screen.width, height: screen.height },
            rotation: 90,
            filter: "nearest",
            normalize: true,
            dither: "bayer4x4"
        });
        screen.flush();
        return true;
    } finally {
        if (frame !== null) frame.close();
    }
}
```

Do not replace this with `frame.read().toArray()` plus nested JavaScript pixel
loops. Here `screen` is an already-open `display.Display`; its driver owns the
panel transfer while `blit()` owns the native pixel conversion.

## Binary transport

For public HTTPS/TLS, first connect Wi-Fi and complete the workspace-selected
`sys.time.sync({ servers, timeoutMs? })` round as described by
`doc://framework/networking`.

`Request`, `Response`, and `fetch` bodies accept a string, `Stream`, `ByteView`,
or `ByteSpanSource`. `request.bytes(maxBytes?)` and
`response.bytes(maxBytes?)` consume a body into an owned `ByteView`.
`Response.bytes(body, init?)` creates a binary response. Binary bodies preserve
embedded NUL bytes and do not acquire an implicit `Content-Type`.

Known lengths produce `Content-Length`. An explicit mismatched length is
rejected before dispatch. HTTP client bodies are bounded to 1 MiB by default;
the server streams response spans without first building a full copy. TCP send
also consumes `ByteSpanSource` span by span, including TLS partial writes.

```js
(function () {
    var cam = camera.open();
    var frame = null;
    var source = null;
    try {
        frame = cam.capture(5000);
        if (frame === null) return null;
        source = frame.source({ chunkBytes: 8192 });
        var response = fetch("https://example.com/frame", {
            method: "POST",
            headers: { "content-type": "image/jpeg" },
            body: source,
            timeoutMs: 10000
        });
        return { status: response.status, sent: frame.byteLength };
    } finally {
        if (source !== null) source.close();
        if (frame !== null) frame.close();
        cam.close();
    }
})()
```

For TCP, replace `fetch(...)` with
`tcpSocket.send(source, timeoutMs)`. The transport closes the source
on success, failure, cancellation, or timeout. Calling `source.close()` and
`frame.close()` afterward is safe and keeps cleanup explicit.
