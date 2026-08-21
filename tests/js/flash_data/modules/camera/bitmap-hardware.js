test("camera/bitmap-hardware", function () {
  var cam = null;
  var frame = null;
  var preview = null;
  var screen = null;
  var presentedBytes = 0;
  var frameBusyError = "";
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

    return {
      model: cam.status().sensor.model,
      source: { width: frame.width, height: frame.height },
      preview: { width: preview.width, height: preview.height },
      presentedBytes: presentedBytes
    };
  } finally {
    if (screen !== null) screen.close();
    if (preview !== null) preview.close();
    if (frame !== null) frame.close();
    if (cam !== null) cam.close();
  }
});
