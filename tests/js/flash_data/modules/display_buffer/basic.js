test("display_buffer/basic", function () {
  var mono = displayBuffer.create({ width: 8, height: 8, format: "mono1" });
  var dirty;
  var bytes;
  var rgb;
  var chunks;
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
  mono.clear(0).drawLine(0, 0, 7, 5, 1);
  bytes = mono.readRect(0, 0, 8, 8).toArray();
  test.equal(bytes[0], 0x01, "diagonal drawLine should set the first point");
  test.equal(bytes[3], 0x04, "diagonal drawLine should advance both axes");
  test.equal(bytes[7], 0x20, "diagonal drawLine should reach the last point");

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

  if (esp32.info().features.spi && typeof spi === "object") {
    var bus = spi.openBus();
    var device = bus.openDevice();

    test.equal(device.write(rgb.readRect(0, 0, 1, 1)), 2, "spi.write should accept native ByteView values");
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
  mono.close();

  return {
    monoBytes: 8,
    rgbBytes: 8,
  };
});
