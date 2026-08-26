test("spi/loopback", function () {
  var overrides = test.config().spiLoopback || {};
  var cfg = {
    host: spi.DEFAULT_HOST,
    sclk: spi.DEFAULT_SCLK,
    mosi: spi.DEFAULT_MOSI,
    miso: spi.DEFAULT_MISO,
    cs: spi.DEFAULT_CS,
    freqHz: 500000,
    maxTransferSize: 64,
  };
  var bus;
  var device;
  var pattern = [0x00, 0xff, 0xa5, 0x5a, 0x11, 0x22, 0x7e, 0x81];
  var dummy = 0x3c;
  var rx;
  var readBack;
  var i;

  function assertBytesEqual(actual, expected, message) {
    var bytes;

    test.ok(actual instanceof _ByteView, message + " should return ByteView");
    try {
      bytes = actual.toArray();
      test.equal(bytes.length, expected.length, message + " length");
      for (i = 0; i < expected.length; i++) {
        test.equal(bytes[i], expected[i], message + " byte " + i);
      }
    } finally {
      actual.close();
    }
  }

  function overrideNumber(name) {
    if (typeof overrides[name] === "number") {
      cfg[name] = overrides[name];
    }
  }

  overrideNumber("host");
  overrideNumber("sclk");
  overrideNumber("mosi");
  overrideNumber("miso");
  overrideNumber("cs");
  overrideNumber("freqHz");
  overrideNumber("maxTransferSize");

  if (cfg.sclk < 0) {
    test.skip("SPI loopback requires DEFAULT_SCLK or testConfig.spiLoopback.sclk");
  }
  if (cfg.mosi < 0 || cfg.miso < 0) {
    test.skip("SPI loopback requires MOSI and MISO defaults or testConfig.spiLoopback overrides");
  }

  bus = spi.openBus({
    host: cfg.host,
    sclk: cfg.sclk,
    mosi: cfg.mosi,
    miso: cfg.miso,
    maxTransferSize: cfg.maxTransferSize,
  });

  try {
    device = bus.openDevice({
      cs: cfg.cs,
      mode: 0,
      freqHz: cfg.freqHz,
      queueSize: 1,
    });

    rx = device.transfer(pattern);
    assertBytesEqual(rx, pattern, "loopback transfer should echo MOSI on MISO");

    readBack = device.read(4, dummy);
    assertBytesEqual(readBack, [dummy, dummy, dummy, dummy], "loopback read should echo fill byte");

    test.equal(device.write(pattern), pattern.length, "loopback write should report byte count");
  } finally {
    if (device) {
      device.close();
    }
    if (bus) {
      bus.close();
    }
  }

  return {
    host: cfg.host,
    sclk: cfg.sclk,
    mosi: cfg.mosi,
    miso: cfg.miso,
    cs: cfg.cs,
    freqHz: cfg.freqHz,
    maxTransferSize: cfg.maxTransferSize,
  };
});
