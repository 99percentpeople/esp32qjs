test("display_buffer/basic", function () {
  var mono = displayBuffer.create({ width: 8, height: 8, format: "mono1" });
  var dirty;
  var bytes;
  var rgb;
  var chunks;
  var source;
  var wide;
  var closeError = "";
  var font = displayBuffer.loadFont("_sys/display/fonts/mono5x7.eqf");

  test.ok(typeof displayBuffer.create === "function", "displayBuffer.create should exist");
  test.equal(mono.width, 8, "mono width should be exposed");
  test.equal(mono.height, 8, "mono height should be exposed");
  test.equal(mono.format, "mono1", "mono format should be exposed");
  test.equal(mono.layout, "page-y8", "mono default layout should be page-y8");
  test.equal(mono.stride, 8, "mono page-y8 stride should be width");
  test.equal(mono.pageHeight, 8, "mono page height should be 8");
  test.equal(mono.byteLength, 8, "mono byteLength should match one 8-pixel page");

  mono.clear(0).clearDirty().setPixel(0, 0, 1).setPixel(1, 7, 1);
  test.equal(mono.getPixel(0, 0), 1, "mono getPixel should read set pixels");
  test.equal(mono.getPixel(1, 7), 1, "mono getPixel should read page-y8 high bits");
  test.equal(mono.getPixel(2, 0), 0, "mono getPixel should read cleared pixels");
  bytes = mono.readRect(0, 0, 8, 8).toArray();
  test.equal(bytes.length, 8, "mono readRect should return a ByteView with array conversion");
  test.equal(bytes[0], 0x01, "mono readRect should pack y=0 in bit 0");
  test.equal(bytes[1], 0x80, "mono readRect should pack y=7 in bit 7");

  dirty = mono.getDirty();
  test.equal(dirty.x, 0, "dirty x should include set pixels");
  test.equal(dirty.y, 0, "dirty y should include set pixels");
  test.equal(dirty.width, 2, "dirty width should include both set pixels");
  test.equal(dirty.height, 8, "dirty height should include y=7");
  mono.clearDirty();
  test.equal(mono.getDirty(), null, "clearDirty should reset dirty bounds");
  mono.fillRect(2, 1, 2, 2, 1);
  dirty = mono.getDirty();
  test.equal(dirty.x, 2, "fillRect should mark dirty x");
  test.equal(dirty.y, 1, "fillRect should mark dirty y");
  test.equal(dirty.width, 2, "fillRect should mark dirty width");
  test.equal(dirty.height, 2, "fillRect should mark dirty height");
  mono.clear(0).clearDirty().fillCircle(3, 3, 2, 1);
  test.equal(mono.getPixel(3, 3), 1, "fillCircle should fill the center pixel");
  test.equal(mono.getPixel(3, 1), 1, "fillCircle should fill the top pixel");
  test.equal(mono.getPixel(1, 1), 0, "fillCircle should leave outside pixels clear");
  dirty = mono.getDirty();
  test.equal(dirty.x, 1, "fillCircle should mark dirty x");
  test.equal(dirty.y, 1, "fillCircle should mark dirty y");
  test.equal(dirty.width, 5, "fillCircle should mark dirty width");
  test.equal(dirty.height, 5, "fillCircle should mark dirty height");
  mono.clear(0).clearDirty().drawCircle(3, 3, 2, 1);
  test.equal(mono.getPixel(3, 3), 0, "drawCircle should leave the center pixel clear");
  test.equal(mono.getPixel(3, 1), 1, "drawCircle should draw the top outline pixel");
  test.equal(mono.getPixel(5, 3), 1, "drawCircle should draw the right outline pixel");
  mono.clear(0).clearDirty().fillEllipse(4, 4, 3, 1, 1);
  test.equal(mono.getPixel(1, 4), 1, "fillEllipse should fill the major axis");
  test.equal(mono.getPixel(4, 3), 1, "fillEllipse should fill the minor axis endpoint");
  test.equal(mono.getPixel(1, 3), 0, "fillEllipse should leave outside pixels clear");
  mono.clear(0).clearDirty().drawEllipse(4, 4, 3, 1, 1);
  test.equal(mono.getPixel(1, 4), 1, "drawEllipse should draw the left endpoint");
  test.equal(mono.getPixel(7, 4), 1, "drawEllipse should draw the right endpoint");
  test.equal(mono.getPixel(4, 4), 0, "drawEllipse should leave the center clear");
  wide = displayBuffer.create({ width: 21, height: 9, format: "mono1" });
  wide.clear(0).drawEllipse(10, 4, 6, 2, 1);
  test.equal(wide.getPixel(8, 3), 1, "drawEllipse should connect a shallow top arc");
  test.equal(wide.getPixel(12, 5), 1, "drawEllipse should connect a shallow bottom arc");
  test.equal(wide.getPixel(10, 4), 0, "drawEllipse should keep the shallow ellipse center clear");
  mono.clear(0).clearDirty().drawRoundRect(1, 1, 6, 5, 2, 1);
  test.equal(mono.getPixel(3, 1), 1, "drawRoundRect should draw the top edge");
  test.equal(mono.getPixel(1, 3), 1, "drawRoundRect should draw the left edge");
  test.equal(mono.getPixel(3, 3), 0, "drawRoundRect should leave the interior clear");
  mono.clear(0).clearDirty().fillRoundRect(1, 1, 6, 5, 2, 1);
  test.equal(mono.getPixel(3, 3), 1, "fillRoundRect should fill the interior");
  test.equal(mono.getPixel(1, 1), 0, "fillRoundRect should keep clipped rounded corners clear");
  mono.clear(0).drawLine(0, 0, 7, 5, 1);
  bytes = mono.readRect(0, 0, 8, 8).toArray();
  test.equal(bytes[0], 0x01, "diagonal drawLine should set the first point");
  test.equal(bytes[3], 0x04, "diagonal drawLine should advance both axes");
  test.equal(bytes[7], 0x20, "diagonal drawLine should reach the last point");
  mono.clear(0).drawPolyline([0, 6, 2, 6, 2, 4], 1);
  test.equal(mono.getPixel(1, 6), 1, "drawPolyline should draw native line segments");
  test.equal(mono.getPixel(2, 5), 1, "drawPolyline should connect segment endpoints");
  mono.clear(0).drawPolygon([{ x: 1, y: 1 }, { x: 4, y: 1 }, { x: 4, y: 3 }], 1);
  test.equal(mono.getPixel(2, 1), 1, "drawPolygon should draw the top edge");
  test.equal(mono.getPixel(2, 2), 1, "drawPolygon should close the final edge");
  mono.clear(0).clearDirty().fillTriangle(1, 1, 5, 1, 3, 5, 1);
  test.equal(mono.getPixel(3, 3), 1, "fillTriangle should fill the triangle interior");
  test.equal(mono.getPixel(1, 1), 0, "fillTriangle should use scanline fill rules for edge pixels");
  dirty = mono.getDirty();
  test.equal(dirty.x, 2, "fillTriangle should mark clipped dirty x");
  test.equal(dirty.y, 1, "fillTriangle should mark clipped dirty y");
  test.equal(dirty.width, 3, "fillTriangle should mark clipped dirty width");
  test.equal(dirty.height, 4, "fillTriangle should mark clipped dirty height");
  mono.clear(0).drawTriangle(1, 1, 5, 1, 3, 5, 1);
  test.equal(mono.getPixel(3, 1), 1, "drawTriangle should draw the top edge");
  test.equal(mono.getPixel(2, 3), 1, "drawTriangle should draw a side edge");
  mono.clear(0).fillPolygon([{ x: 0, y: 0 }, { x: 2, y: 0 }, { x: 2, y: 2 }, { x: 0, y: 2 }], 1);
  test.equal(mono.getPixel(0, 0), 1, "fillPolygon should accept object points");
  test.equal(mono.getPixel(2, 1), 1, "fillPolygon should fill polygon rows");
  test.equal(mono.getPixel(0, 2), 0, "fillPolygon should follow half-pixel scanline bounds");
  mono.clear(0).drawQuadraticBezier(0, 0, 3, 6, 7, 0, 1, { segments: 4 });
  test.equal(mono.getPixel(0, 0), 1, "drawQuadraticBezier should draw the first point");
  test.equal(mono.getPixel(7, 0), 1, "drawQuadraticBezier should draw the last point");
  mono.clear(0).drawCubicBezier(0, 7, 0, 0, 7, 0, 7, 7, 1, { segments: 4 });
  test.equal(mono.getPixel(0, 7), 1, "drawCubicBezier should draw the first point");
  test.equal(mono.getPixel(7, 7), 1, "drawCubicBezier should draw the last point");

  test.equal(mono.measureText("A", { font: font }).width, 6, "measureText should use default EQF glyph advance");
  test.equal(mono.measureText("A", { font: font }).height, 8, "measureText should report line height");
  try {
    mono.drawText(0, 0, "A", 1);
  } catch (textOptionsError) {
    closeError = String(textOptionsError);
  }
  test.ok(closeError.indexOf("options must be an object") >= 0, "drawText should reject positional colors");
  closeError = "";
  mono.clear(0).drawText(0, 0, "A", { color: 1, font: font });
  bytes = mono.readRect(0, 0, 8, 8).toArray();
  test.equal(bytes[0], 0x7e, "drawText should render the default EQF font");
  mono.clear(1).drawText(0, 0, "A", { color: 0, font: font });
  bytes = mono.readRect(0, 0, 8, 8).toArray();
  test.equal(bytes[0], 0x81, "drawText should keep non-glyph pixels transparent by default");
  test.equal(bytes[5], 0xff, "drawText should keep the advance gap transparent by default");
  mono.clear(1).drawText(0, 0, "A", { color: 1, font: font, background: 0 });
  bytes = mono.readRect(0, 0, 8, 8).toArray();
  test.equal(bytes[0], 0x7e, "drawText should fill an explicit background before glyph pixels");
  test.equal(bytes[5], 0x00, "drawText explicit background should cover the advance gap");
  mono.clear(1).drawBitmap(0, 0, { width: 2, height: 1, pixels: [1, 0] }, { color: 0 });
  bytes = mono.readRect(0, 0, 8, 8).toArray();
  test.equal(bytes[0], 0xfe, "drawBitmap should draw mask pixels with an options color");
  test.equal(bytes[1], 0xff, "drawBitmap should keep off pixels transparent by default");
  mono.clear(1).drawBitmap(0, 0, { width: 2, height: 1, pixels: [1, 0] }, { color: 1, background: 0 });
  bytes = mono.readRect(0, 0, 8, 8).toArray();
  test.equal(bytes[0], 0xff, "drawBitmap should draw on pixels with an options color");
  test.equal(bytes[1], 0xfe, "drawBitmap should fill explicit background pixels");
  try {
    mono.setPixel(0, 0, true);
  } catch (colorError) {
    closeError = String(colorError);
  }
  test.ok(closeError.indexOf("valid color") >= 0, "mono colors should reject boolean values");
  closeError = "";

  rgb = displayBuffer.create({ width: 2, height: 2, format: "rgb565", chunkBytes: 4 });
  test.equal(rgb.layout, "linear", "rgb565 default layout should be linear");
  test.equal(rgb.stride, 4, "rgb565 stride should be width * 2");
  test.equal(rgb.byteLength, 8, "rgb565 byteLength should be width * height * 2");
  rgb.clear(0x1234).setPixel(1, 0, 0xabcd);
  test.equal(rgb.getPixel(1, 0), 0xabcd, "rgb565 getPixel should return the 16-bit color");
  bytes = rgb.readRect(0, 0, 2, 2, { byteOrder: "be" }).toArray();
  test.equal(bytes[0], 0x12, "rgb565 be export should start with high byte");
  test.equal(bytes[1], 0x34, "rgb565 be export should include low byte");
  test.equal(bytes[2], 0xab, "rgb565 be export should include set high byte");
  test.equal(bytes[3], 0xcd, "rgb565 be export should include set low byte");
  bytes = rgb.readRect(0, 0, 1, 1, { byteOrder: "le" }).toArray();
  test.equal(bytes[0], 0x34, "rgb565 le export should start with low byte");
  test.equal(bytes[1], 0x12, "rgb565 le export should include high byte");
  chunks = rgb.readRectChunks(0, 0, 2, 2);
  test.equal(chunks.length, 2, "readRectChunks should split by configured chunkBytes");
  test.equal(chunks[0].length, 4, "readRectChunks should return ByteView chunks");
  source = rgb.createSpanSource({ byteOrder: "be", chunkBytes: 4 });
  test.ok(source && typeof source.setRect === "function", "createSpanSource should return a reusable span source");
  test.equal(source.setRect(-1, -1, 2, 2), source, "span source setRect should return the source");
  test.equal(source.setRect(0, 0, 2, 2), source, "span source setRect should allow repeated updates");
  var reusedChunks = rgb.readRectChunks(0, 0, 2, 2, { byteOrder: "be", reuse: true });
  var reusedFirst = reusedChunks[0];

  rgb.setPixel(0, 0, 0x0001);
  var reusedAgain = rgb.readRectChunks(0, 0, 2, 2, { byteOrder: "be", reuse: true });
  test.equal(reusedAgain, reusedChunks, "readRectChunks reuse should keep the chunk array stable");
  test.equal(reusedAgain[0], reusedFirst, "readRectChunks reuse should keep ByteView wrappers stable");
  test.equal(reusedAgain[0].toArray()[1], 0x01, "reused ByteView should point at updated pixel data");

  if (esp32.info().features.spi && typeof spi === "object") {
    var bus = spi.openBus();
    var device = bus.openDevice();

    test.equal(device.write(rgb.readRect(0, 0, 1, 1)), 2, "spi.write should accept native ByteView values");
    if (typeof device.writeChunks === "function") {
      var stats = device.writeChunks(chunks, { queueDepth: 2 });

      test.equal(stats.bytes, 8, "spi.writeChunks should report transmitted bytes");
      test.equal(stats.chunks, 2, "spi.writeChunks should transmit byte-source chunks");
      test.equal(typeof stats.direct, "boolean", "spi.writeChunks should report whether direct DMA was used");
    }
    if (typeof device.writeSource === "function") {
      var sourceStats = device.writeSource(source, { queueDepth: 2 });

      test.equal(sourceStats.bytes, 8, "spi.writeSource should report transmitted bytes");
      test.equal(sourceStats.chunks, 2, "spi.writeSource should transmit source spans");
      test.equal(typeof sourceStats.direct, "boolean", "spi.writeSource should report whether direct DMA was used");

      source.setRect(-1, -1, 2, 2);
      sourceStats = device.writeSource(source, { queueDepth: 2 });
      test.equal(sourceStats.bytes, 2, "spi.writeSource should use the clamped source rectangle");
      test.equal(sourceStats.chunks, 1, "spi.writeSource should emit one clamped span");
    }
    device.close();
    bus.close();
  }

  test.equal(rgb.close(), true, "close should release the native buffer");
  try {
    rgb.clear(0);
  } catch (closedError) {
    closeError = String(closedError);
  }
  test.ok(closeError.indexOf("closed") >= 0, "methods should fail clearly after close");
  wide.close();
  mono.close();

  return {
    monoBytes: 8,
    rgbBytes: 8,
  };
});
