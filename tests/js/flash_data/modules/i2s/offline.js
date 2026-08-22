test("i2s/offline", function () {
  var capabilities = i2s.capabilities();
  var directionError = "";
  var txPinError = "";
  var duplexPinError = "";
  var missingPinsError = "";
  var widthError = "";
  var dmaError = "";
  var pdmPinError = "";
  var pdmLayoutError = "";
  var optionsError = "";
  var duplicatePinError = "";
  var pdmDirectionError = "";

  test.ok(capabilities && typeof capabilities === "object",
    "i2s.capabilities should return an object");
  test.ok(capabilities.ports.length > 0, "at least one I2S port should exist");
  test.equal(capabilities.standard, true, "standard RX should be supported");
  test.equal(capabilities.standardRx, true, "standard RX should be explicit");
  test.equal(capabilities.standardTx, true, "standard TX should be supported");
  test.equal(capabilities.standardDuplex, true,
    "standard duplex should be supported");
  test.ok(typeof capabilities.pdm === "boolean", "PDM support should be explicit");
  test.equal(capabilities.limits.maxDescriptorBytes, 4092,
    "DMA descriptor limit should match ESP-IDF");
  test.equal(capabilities.limits.maxReadBytes, 65536,
    "one read should be bounded to 64 KiB");
  test.equal(capabilities.limits.maxWriteBytes, 65536,
    "one write should be bounded to 64 KiB");

  try {
    i2s.open([]);
  } catch (optionsException) {
    optionsError = String(optionsException);
  }
  test.ok(optionsError.indexOf("options object") >= 0,
    "open should reject arrays as option records");

  try {
    i2s.open({
      direction: "sideways",
      mode: "standard",
      pins: { bclk: 0, ws: 1, din: 2, dout: 3 }
    });
  } catch (error) {
    directionError = String(error);
  }
  test.ok(directionError.indexOf("rx\", \"tx\", or \"duplex") >= 0,
    "direction should use the unified channel variants");

  try {
    i2s.open({
      direction: "tx",
      mode: "standard",
      pins: { bclk: 0, ws: 1, din: 2 }
    });
  } catch (txPinException) {
    txPinError = String(txPinException);
  }
  test.ok(txPinError.indexOf("pins.dout") >= 0,
    "TX should require an explicit output data pin");

  try {
    i2s.open({
      direction: "duplex",
      mode: "standard",
      pins: { bclk: 0, ws: 1, dout: 2 }
    });
  } catch (duplexPinException) {
    duplexPinError = String(duplexPinException);
  }
  test.ok(duplexPinError.indexOf("pins.din") >= 0,
    "duplex should require both input and output data pins");

  try {
    i2s.open({ direction: "rx", mode: "standard" });
  } catch (missingPinsException) {
    missingPinsError = String(missingPinsException);
  }
  test.ok(missingPinsError.indexOf("standard pins") >= 0,
    "standard mode should require its explicit pin group");

  try {
    i2s.open({
      direction: "rx",
      mode: "standard",
      dataBits: 24,
      slotBits: 16,
      pins: { bclk: 0, ws: 1, din: 2 }
    });
  } catch (widthException) {
    widthError = String(widthException);
  }
  test.ok(widthError.indexOf("dataBits/slotBits") >= 0,
    "standard mode should validate the discriminated PCM layout");

  try {
    i2s.open({
      direction: "rx",
      mode: "standard",
      pins: { bclk: 0, ws: 0, din: 0 }
    });
  } catch (duplicatePinException) {
    duplicatePinError = String(duplicatePinException);
  }
  test.ok(duplicatePinError.indexOf("distinct GPIOs") >= 0,
    "standard mode should reject overlapping signal pins");

  try {
    i2s.open({
      direction: "rx",
      mode: "standard",
      pins: { bclk: 0, ws: 1, din: 2 },
      dma: { descriptorCount: 2, framesPerDescriptor: 2047 }
    });
  } catch (dmaException) {
    dmaError = String(dmaException);
  }
  test.ok(dmaError.indexOf("4092-byte") >= 0,
    "DMA frames should be rejected above one-descriptor capacity");

  try {
    i2s.open({
      direction: "rx",
      mode: "pdm",
      pins: { clk: -2, din: -2 }
    });
  } catch (pdmPinException) {
    pdmPinError = String(pdmPinException);
  }
  test.ok(pdmPinError.indexOf("pins.clk") >= 0,
    "PDM should validate its distinct clock/data pin group");

  try {
    i2s.open({
      direction: "rx",
      mode: "pdm",
      dataBits: 32,
      pins: { clk: 0, din: 1 }
    });
  } catch (pdmLayoutException) {
    pdmLayoutError = String(pdmLayoutException);
  }
  test.ok(pdmLayoutError.indexOf("standard-I2S option dataBits") >= 0,
    "PDM should reject configurable PCM width instead of ignoring it");

  try {
    i2s.open({
      direction: "tx",
      mode: "pdm",
      pins: { clk: 0, din: 1 }
    });
  } catch (pdmDirectionException) {
    pdmDirectionError = String(pdmDirectionException);
  }
  test.ok(pdmDirectionError.indexOf("only accepts direction \"rx\"") >= 0,
    "PDM should remain receive-only");

  return {
    ports: capabilities.ports.length,
    pdm: capabilities.pdm,
    maxReadBytes: capabilities.limits.maxReadBytes,
    maxWriteBytes: capabilities.limits.maxWriteBytes
  };
});
