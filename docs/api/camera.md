# `camera` Module

`camera` is registered only when `sys.info.features.camera` is true on a
supported ESP32-S3 build. It is independent of I2S and provides explicit still
capture only: no background video, codecs, MJPEG, RTSP, or upload policy.

- `camera.capabilities()`
  Return target, PSRAM status/size, compiled sensor drivers, pixel formats, and
  frame sizes, including the native square `128x128` mode. Version 1 probes
  OV2640, OV3660, or OV5640 after initialization; callers do not select a
  sensor model.
- `camera.open(options?)`
  Open the singleton camera. Options include `pixelFormat`, `frameSize`,
  `jpegQuality`, `frameBuffers`, `grabMode`, `bufferLocation`, `xclkFreqHz`,
  `psramDma`, `timeoutMs`, and `pins`. `psramDma: false` keeps DMA staging in
  internal memory before complete chunks are copied to PSRAM. Explicit pins
  override selected hardware constants;
  the resolved XCLK, SCCB, D0-D7, VSYNC, HREF, and PCLK map must be complete.

The safe defaults are JPEG, QVGA, quality 12, one PSRAM framebuffer, and
`whenEmpty`. Continuous acquisition is enabled only when the application
explicitly pairs two framebuffers with `latest`.

`Camera` methods:

- `cam.capture(timeoutMs?)`
  Return one `CameraFrame` or `null`. Only one capture Future may be pending and
  only one framebuffer may be leased. The native acquisition observes the
  timeout and returns `null` without closing the camera. Cancelling or
  interrupting an in-flight capture closes that camera instance after its
  worker exits, so the stale handle must not be reused.
- `cam.status()`
  Return configuration and ownership state. `status().sensor.model` reports
  the detected `ov2640`, `ov3660`, or `ov5640`.
- `cam.controls()` / `cam.setControl(name, value)`
  Read or update `frameSize`, `jpegQuality`, `brightness`, `contrast`,
  `saturation`, `horizontalMirror`, or `verticalFlip`.
- `cam.close()`
  Start a close, cancel active capture, and immediately revoke
  derived `CameraFrame` and unopened frame-source handles. Revoked frame data
  operations throw a closed-frame `ReferenceError`; `frame.close()` remains
  idempotent. The close waits only for active native bitmap/source readers,
  then releases the driver in a worker. Caller cancellation stops waiting but
  does not cancel cleanup. Only the current call stack is suspended; timers,
  transports, and other Futures continue to run. Use
  `Future.call(cam.close, cam, [])` when the caller does not need to wait.
  Driver deinitialization must succeed before the camera, SCCB, and LEDC leases
  are released or the JS handle is detached. A native deinitialization failure
  rejects the call, retains all resources, and leaves that handle usable only
  for another `close()` attempt; `camera.open()` and runtime initialization can
  also retry orphaned cleanup. Repeated calls are safe.

`CameraFrame` exposes read-only `width`, `height`, `format`, `byteLength`,
`timestampUs`, and `sequence` fields:

- `frame.source({ chunkBytes? })`
  Return one active, one-shot `ByteSpanSource`. Chunks are limited to 32768
  bytes. A consuming transport closes the source and returns the framebuffer on
  success, failure, cancellation, or timeout.
- `frame.read(offset?, limit?)`
  Copy at most 32768 bytes into an owned `ByteView` for diagnostics. Close the
  view after inspection.
- `frame.close()`
  Return the framebuffer. It rejects while a source is active and is idempotent
  after the source has released the frame.

```js
var cam = camera.open({
  pixelFormat: "jpeg",
  frameSize: "qvga",
  jpegQuality: 12,
  frameBuffers: 1,
  grabMode: "whenEmpty",
  bufferLocation: "psram"
});
var frame = null;
var source = null;
try {
  frame = cam.capture(5000);
  if (frame !== null) {
    source = frame.source({ chunkBytes: 8192 });
    var response = http.fetch("https://example.com/frame", {
      method: "POST",
      headers: { "content-type": "image/jpeg" },
      body: source,
      timeoutMs: 10000
    });
    print(response.status, cam.status().sensor.model);
  }
} finally {
  if (source !== null) source.close();
  if (frame !== null) frame.close();
  cam.close();
}
```
