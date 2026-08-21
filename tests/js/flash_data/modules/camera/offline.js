test("camera/offline", function () {
  var capabilities = camera.capabilities();
  var modelOptionError = "";
  var qualityError = "";
  var bufferModeError = "";
  var pinError = "";
  var optionsError = "";

  test.ok(capabilities && typeof capabilities === "object",
    "camera.capabilities should return an object");
  test.equal(capabilities.target, "esp32s3", "camera should be S3-gated");
  test.ok(capabilities.sensorDrivers.indexOf("ov2640") >= 0,
    "OV2640 should be compiled");
  test.ok(capabilities.sensorDrivers.indexOf("ov3660") >= 0,
    "OV3660 should be compiled");
  test.ok(capabilities.pixelFormats.indexOf("jpeg") >= 0,
    "JPEG should be supported");
  test.ok(capabilities.frameSizes.indexOf("qvga") >= 0,
    "QVGA should be supported");

  try {
    camera.open([]);
  } catch (optionsException) {
    optionsError = String(optionsException);
  }
  test.ok(optionsError.indexOf("expects an object") >= 0,
    "camera options should be a record, not an array");

  try {
    camera.open({ sensor: "ov2640" });
  } catch (error) {
    modelOptionError = String(error);
  }
  test.ok(modelOptionError.indexOf("sensor") >= 0,
    "callers should not guess a camera sensor model");

  try {
    camera.open({ jpegQuality: 64 });
  } catch (qualityException) {
    qualityError = String(qualityException);
  }
  test.ok(qualityError.indexOf("JPEG") >= 0,
    "JPEG quality should be bounded before driver initialization");

  try {
    camera.open({ frameBuffers: 2, grabMode: "whenEmpty" });
  } catch (bufferModeException) {
    bufferModeError = String(bufferModeException);
  }
  test.ok(bufferModeError.indexOf("2/latest") >= 0,
    "continuous capture should require explicit 2/latest pairing");

  try {
    camera.open({
      bufferLocation: "dram",
      pins: { pclk: -2 }
    });
  } catch (pinException) {
    pinError = String(pinException);
  }
  test.ok(pinError.indexOf("complete explicit") >= 0,
    "camera should reject an incomplete or invalid resolved pin map");

  return {
    target: capabilities.target,
    psram: capabilities.psram,
    sensors: capabilities.sensorDrivers
  };
});
