test("camera/bitmap-hardware", function () {
  var cam = null;
  var frame = null;
  var preview = null;
  var screen = null;
  var reopened = null;
  var inactiveSource = null;
  var presentedBytes = 0;
  var frameBusyError = "";
  var revokedFrameError = "";
  var revokedFrameCloseResult = null;
  var cameraCloseResult = null;
  var cameraCloseElapsed = 0;
  var asyncClose = null;
  var sensorModel = "";
  var quickFrame = null;
  var quickStarted = 0;
  var quickElapsed = 0;
  var dirty;

  load("_sys/display.js");
  try {
    cam = camera.open({
      pixelFormat: "grayscale",
      frameSize: "qvga",
      frameBuffers: 1,
      grabMode: "whenEmpty",
      bufferLocation: "psram"
    });
    frame = cam.capture(5000);
    test.ok(frame !== null, "camera should capture one grayscale frame");
    test.equal(frame.format, "grayscale", "captured frame should stay raw grayscale");
    frame.close();
    frame = null;
    quickStarted = sys.millis();
    quickFrame = cam.capture(1);
    quickElapsed = sys.millis() - quickStarted;
    test.ok(quickElapsed < 250,
      "camera capture timeout should bound the native worker wait");
    if (quickFrame !== null) {
      quickFrame.close();
      quickFrame = null;
    }
    frame = cam.capture(5000);
    test.ok(frame !== null, "camera should remain open after a capture timeout");

    setTimeout(function () {
      try {
        frame.close();
      } catch (frameCloseError) {
        frameBusyError = String(frameCloseError);
      }
    }, 0);
    preview = bitmap.convert(frame, {
      format: "mono1",
      width: 128,
      height: 64,
      rotation: 90,
      filter: "bilinear",
      normalize: true,
      dither: "bayer4x4"
    });
    test.ok(frameBusyError.indexOf("CameraFrame is busy") >= 0,
      "camera frame should remain read-leased during conversion");
    test.equal(preview.width, 128, "converted preview should use the requested width");
    test.equal(preview.height, 64, "converted preview should use the requested height");
    dirty = preview.getDirty();
    test.equal(dirty.width, 128, "atomic conversion should publish a fully dirty Bitmap");
    test.equal(dirty.height, 64, "atomic conversion should publish every converted row");

    screen = display.create({
      name: "camera-preview-sink",
      width: 128,
      height: 64,
      pixelFormat: "mono1",
      layout: "page-y8",
      capabilities: { partialPresent: true },
      open: function () { return this; },
      present: function (source, regions) {
        var region = regions[0];
        var view = source.readRect(
          region.x, region.y, region.width, region.height
        );
        var byteLength = view.byteLength;
        try {
          presentedBytes += byteLength;
        } finally {
          view.close();
        }
        return {
          regions: regions.length,
          pixels: region.width * region.height,
          bytes: byteLength,
          chunks: 1
        };
      },
      close: function () { return true; }
    });
    screen.blit(frame, {
      destinationRect: { x: 0, y: 0, width: 128, height: 64 },
      rotation: 270,
      filter: "nearest",
      normalize: true,
      dither: "bayer4x4"
    });
    screen.open();
    test.ok(presentedBytes > 0,
      "Display presentation should consume native Bitmap bytes");

    quickStarted = sys.millis();
    cameraCloseResult = cam.close();
    cameraCloseElapsed = sys.millis() - quickStarted;
    test.equal(cameraCloseResult, true,
      "camera close should return after the worker releases the driver");
    test.ok(cameraCloseElapsed < 1000,
      "camera close should revoke a suspended JavaScript frame without waiting");
    try {
      frame.read(0, 1);
    } catch (revokedError) {
      revokedFrameError = String(revokedError);
    }
    test.ok(revokedFrameError.indexOf("CameraFrame is closed") >= 0,
      "camera close should immediately invalidate its derived frame");
    revokedFrameCloseResult = frame.close();
    test.equal(revokedFrameCloseResult, true,
      "closing a frame revoked by its camera should remain idempotent");
    frame = null;
    cam = null;
    reopened = camera.open({
      pixelFormat: "grayscale",
      frameSize: "qvga",
      frameBuffers: 1,
      grabMode: "whenEmpty",
      bufferLocation: "psram"
    });
    test.ok(reopened.status().opened,
      "camera should reopen after close revokes its JavaScript frame lease");
    sensorModel = reopened.status().sensor.model;
    frame = reopened.capture(5000);
    test.ok(frame !== null,
      "camera should capture a frame for unopened source revocation");
    inactiveSource = frame.source({ chunkBytes: 4096 });
    quickStarted = sys.millis();
    test.equal(reopened.close(), true,
      "camera close should revoke an unopened frame source");
    cameraCloseElapsed = sys.millis() - quickStarted;
    test.ok(cameraCloseElapsed < 1000,
      "an unopened frame source should not delay camera close");
    test.equal(inactiveSource.close(), true,
      "a source revoked by camera close should remain idempotently closeable");
    inactiveSource = null;
    test.equal(frame.close(), true,
      "the source owner frame should remain idempotently closeable");
    frame = null;
    reopened = camera.open({
      pixelFormat: "grayscale",
      frameSize: "qvga",
      frameBuffers: 1,
      grabMode: "whenEmpty",
      bufferLocation: "psram"
    });
    asyncClose = Future.call(reopened.close, reopened, []);
    test.equal(asyncClose.status(), "queued",
      "Future.call should allow camera close without waiting at the call site");
    Future.sleep(1).wait(1000);
    test.equal(asyncClose.wait(5000), true,
      "queued camera close should finish through the native driver");
    reopened = null;

    return {
      model: sensorModel,
      preview: { width: preview.width, height: preview.height },
      presentedBytes: presentedBytes
    };
  } finally {
    if (inactiveSource !== null) inactiveSource.close();
    if (reopened !== null) reopened.close();
    if (screen !== null) screen.close();
    if (preview !== null) preview.close();
    if (quickFrame !== null) quickFrame.close();
    if (frame !== null) frame.close();
    if (cam !== null) cam.close();
  }
});
