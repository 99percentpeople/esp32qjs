test("display_buffer/font", function () {
  var info = esp32.info();
  var path = "display-font-test.eqf";
  var fontData;
  var font;
  var mono;
  var bytes;
  var metrics;

  if (!info.features.fs) {
    test.skip("displayBuffer.loadFont requires fs");
  }

  fontData = String.fromCharCode(
    0x45, 0x51, 0x46, 0x31,
    0x01, 0x00,
    65, 65,
    1, 7,
    2, 9,
    1, 0, 0, 0,
    0x7e
  );
  fs.writeText(path, fontData);

  font = displayBuffer.loadFont(path);
  test.equal(font.width, 1, "dynamic font width should match file");
  test.equal(font.height, 7, "dynamic font height should match file");
  test.equal(font.advance, 2, "dynamic font advance should match file");
  test.equal(font.lineHeight, 9, "dynamic font line height should match file");

  mono = displayBuffer.create({ width: 8, height: 16, format: displayBuffer.MONO1 });
  metrics = mono.measureText("A", { font: font, spacing: 1 });
  test.equal(metrics.width, 2, "measureText should use dynamic font advance");
  test.equal(metrics.height, 9, "measureText should use dynamic font line height");

  mono.clear(0).drawText(0, 0, "A", { color: 1, font: font });
  bytes = mono.readRect(0, 0, 1, 8).toArray();
  test.equal(bytes[0], 0x7e, "drawText should render dynamic font glyph bytes");
});
