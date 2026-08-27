test("spi/basic", function () {
  var info = sys.info;
  var bus;
  var busStatus;
  var device;
  var deviceStatus;
  var overrideBus;
  var overrideStatus;
  var emptyTransfer;
  var emptyRead;
  var staleBusError = "";
  var staleDeviceError = "";
  var bulkFuture;
  var bulkStats;

  function assertThrowsContains(fn, expected, message) {
    var thrown = "";

    try {
      fn();
    } catch (error) {
      thrown = String(error);
    }
    test.ok(thrown.indexOf(expected) >= 0, message + ": " + thrown);
  }

  test.ok(typeof spi.HOST_2 === "number", "spi.HOST_2 should be numeric");
  if (info.features.spi) {
    test.ok(typeof spi.DEFAULT_HOST === "number", "spi.DEFAULT_HOST should be numeric");
    test.ok(typeof spi.DEFAULT_SCLK === "number", "spi.DEFAULT_SCLK should be numeric");
    test.ok(typeof spi.DEFAULT_MOSI === "number", "spi.DEFAULT_MOSI should be numeric");
    test.ok(typeof spi.DEFAULT_MISO === "number", "spi.DEFAULT_MISO should be numeric");
    test.ok(typeof spi.DEFAULT_CS === "number", "spi.DEFAULT_CS should be numeric");
    test.ok(typeof spi.DEFAULT_FREQ_HZ === "number", "spi.DEFAULT_FREQ_HZ should be numeric");
    test.ok(typeof spi.DEFAULT_QUEUE_SIZE === "number", "spi.DEFAULT_QUEUE_SIZE should be numeric");
    test.ok(typeof spi.DEFAULT_MAX_TRANSFER_SIZE === "number", "spi.DEFAULT_MAX_TRANSFER_SIZE should be numeric");
    test.ok(typeof spi.openBus === "function", "spi.openBus should exist");
    test.ok(typeof spi.transfer === "undefined", "spi.transfer should not exist on the top-level module");
  }

  bus = spi.openBus();
  test.ok(bus && typeof bus === "object", "spi.openBus() should return an SPIBus object");
  test.ok(typeof bus.status === "function", "SPIBus.status should exist");
  test.ok(typeof bus.openDevice === "function", "SPIBus.openDevice should exist");

  busStatus = bus.status();
  test.ok(busStatus && typeof busStatus === "object", "SPIBus.status() should return an object");
  test.equal(busStatus.opened, true, "SPIBus.status() should report an open bus");
  test.ok(typeof busStatus.host === "number", "SPIBus.status().host should be numeric");
  test.ok(typeof busStatus.sclk === "number", "SPIBus.status().sclk should be numeric");
  test.equal(busStatus.sclk, spi.DEFAULT_SCLK, "SPIBus.status().sclk should use DEFAULT_SCLK");
  test.equal(busStatus.mosi, spi.DEFAULT_MOSI, "SPIBus.status().mosi should use DEFAULT_MOSI");
  test.equal(busStatus.miso, spi.DEFAULT_MISO, "SPIBus.status().miso should use DEFAULT_MISO");
  test.ok(typeof busStatus.maxTransferSize === "number", "SPIBus.status().maxTransferSize should be numeric");
  test.equal(busStatus.dmaStagingBytes,
    Math.min(8192, busStatus.maxTransferSize),
    "SPIBus.status() should report the fixed staging slot size");

  assertThrowsContains(function () {
    spi.openBus({ unknown: true });
  }, "unknown key", "spi.openBus should reject unknown fields");
  assertThrowsContains(function () {
    spi.openBus({ maxTransferSize: 8, dmaStagingBytes: 0 });
  }, "dmaStagingBytes", "spi.openBus should reject an empty staging slot");
  assertThrowsContains(function () {
    spi.openBus({ maxTransferSize: 8, dmaStagingBytes: 9 });
  }, "must not exceed", "spi.openBus should bound staging by maxTransferSize");

  device = bus.openDevice();
  test.ok(device && typeof device === "object", "SPIBus.openDevice() should return an SPIDevice object");
  test.ok(typeof device.status === "function", "SPIDevice.status should exist");
  test.ok(typeof device.transfer === "function", "SPIDevice.transfer should exist");
  test.ok(typeof device.write === "function", "SPIDevice.write should exist");
  test.ok(typeof device.writeChunks === "function", "SPIDevice.writeChunks should exist");
  test.ok(typeof device.writeSource === "function", "SPIDevice.writeSource should exist");
  test.ok(typeof device.read === "function", "SPIDevice.read should exist");

  deviceStatus = device.status();
  test.ok(deviceStatus && typeof deviceStatus === "object", "SPIDevice.status() should return an object");
  test.equal(deviceStatus.opened, true, "SPIDevice.status() should report an open device");
  test.equal(deviceStatus.cs, spi.DEFAULT_CS, "SPIDevice.status().cs should use DEFAULT_CS");
  test.equal(typeof deviceStatus.freqHz, "undefined", "SPIDevice.status() should not expose the superseded freqHz field");
  test.equal(deviceStatus.requestedFreqHz, spi.DEFAULT_FREQ_HZ, "SPIDevice.status() should report requestedFreqHz");
  test.ok(deviceStatus.actualFreqHz > 0, "SPIDevice.status() should report actualFreqHz");
  test.equal(deviceStatus.directExternalDma, false, "external DMA direct mode should default off");
  test.equal(deviceStatus.timeoutMs, 1000, "device timeout should default to 1000 ms");
  test.equal(deviceStatus.dmaStagingBytes, busStatus.dmaStagingBytes, "device status should report the bus staging slot size");
  test.equal(deviceStatus.faulted, false, "a new SPI device should not be faulted");
  test.equal(deviceStatus.lastErrorCode, null, "a new SPI device should not have an error code");

  assertThrowsContains(function () {
    bus.openDevice({ unknown: true });
  }, "unknown key", "SPIBus.openDevice should reject unknown fields");
  assertThrowsContains(function () {
    bus.openDevice({ timeoutMs: 0 });
  }, "1..60000", "SPIBus.openDevice should reject timeoutMs below range");
  assertThrowsContains(function () {
    bus.openDevice({ timeoutMs: 60001 });
  }, "1..60000", "SPIBus.openDevice should reject timeoutMs above range");

  test.equal(device.write([], { timeoutMs: 25 }), 0, "SPIDevice.write should accept an operation timeout");
  bulkStats = device.writeChunks([], { queueDepth: 2, timeoutMs: 25 });
  test.equal(bulkStats.bytes, 0, "empty writeChunks should report zero bytes");
  test.equal(bulkStats.sourceSpans, 0, "empty writeChunks should report zero source spans");
  test.equal(bulkStats.transactions, 0, "empty writeChunks should report zero transactions");
  test.equal(bulkStats.path, "direct-internal", "empty writeChunks should use the neutral direct path");
  test.equal(bulkStats.stagedBytes, 0, "empty writeChunks should report zero staged bytes");
  test.equal(bulkStats.queueDepth, 2, "writeChunks should report its effective queue depth");
  test.ok(typeof bulkStats.copyUs === "number", "writeChunks should report copyUs");
  test.ok(typeof bulkStats.queueUs === "number", "writeChunks should report queueUs");
  test.ok(typeof bulkStats.waitUs === "number", "writeChunks should report waitUs");
  test.ok(typeof bulkStats.transferUs === "number", "writeChunks should report transferUs");
  test.ok(typeof bulkStats.totalUs === "number", "writeChunks should report totalUs");
  bulkFuture = Future.call(device.writeChunks, device, [[], { timeoutMs: 25 }]);
  test.ok(bulkFuture instanceof Future,
    "Future.call(SPIDevice.writeChunks) should return immediately with a Future");
  bulkStats = bulkFuture.wait(500);
  test.equal(bulkStats.sourceSpans, 0,
    "native SPI bulk Future should settle empty writes");
  assertThrowsContains(function () {
    device.write([], { timeoutMs: 0 });
  }, "1..60000", "SPIDevice.write should reject timeoutMs below range");
  assertThrowsContains(function () {
    device.transfer([], { unknown: true });
  }, "unknown key", "SPIDevice.transfer should reject unknown fields");
  assertThrowsContains(function () {
    device.read(0, { fillByte: 256 });
  }, "0..255", "SPIDevice.read should bound fillByte");
  assertThrowsContains(function () {
    device.writeChunks([], { queueDepth: 0 });
  }, "queueDepth", "SPIDevice.writeChunks should bound queueDepth");
  var writeSourceRejected = false;
  try {
    device.writeSource([]);
  } catch (writeSourceError) {
    writeSourceRejected = String(writeSourceError).indexOf("ByteSpanSource") >= 0;
  }
  test.ok(writeSourceRejected, "SPIDevice.writeSource should reject non-source inputs");
  if (info.features.bitmap && typeof bitmap === "object") {
    var buffer = bitmap.create({ width: 1, height: 1, format: "rgb565" });
    try {
      var source = buffer.createSpanSource();
      var sourceStats = device.writeSource(source);

      test.ok(source instanceof _ByteSpanSource, "display span source should provide the generic ByteSpanSource capability");
      test.ok(sourceStats && typeof sourceStats === "object", "SPIDevice.writeSource should accept generic ByteSpanSource inputs");
      test.equal(sourceStats.bytes, 2, "SPIDevice.writeSource should report generic source bytes");
      test.equal(sourceStats.sourceSpans, 1, "SPIDevice.writeSource should consume one source span");
      test.ok(sourceStats.transactions >= 1, "SPIDevice.writeSource should report DMA transactions");
    } finally {
      buffer.close();
    }
  }
  emptyTransfer = device.transfer([], { timeoutMs: 25 });
  emptyRead = device.read(0, { fillByte: 0x3c, timeoutMs: 25 });
  try {
    test.ok(emptyTransfer instanceof _ByteView,
      "SPIDevice.transfer() should return a ByteView");
    test.ok(emptyRead instanceof _ByteView,
      "SPIDevice.read() should return a ByteView");
    test.equal(emptyTransfer.length, 0,
      "SPIDevice.transfer([]) should return an empty ByteView");
    test.equal(emptyRead.length, 0,
      "SPIDevice.read(0) should return an empty ByteView");
  } finally {
    emptyTransfer.close();
    emptyRead.close();
  }

  test.equal(bus.close(), true, "SPIBus.close() should succeed");

  try {
    bus.status();
  } catch (busError) {
    staleBusError = String(busError);
  }
  test.ok(staleBusError.indexOf("closed") >= 0, "closed SPIBus objects should reject further use");

  try {
    device.status();
  } catch (deviceError) {
    staleDeviceError = String(deviceError);
  }
  test.ok(staleDeviceError.indexOf("closed") >= 0, "devices from a closed bus should become stale");

  overrideBus = spi.openBus({ sclk: spi.DEFAULT_SCLK, mosi: -1, miso: -1 });
  overrideStatus = overrideBus.status();
  test.equal(overrideStatus.mosi, -1, "spi.openBus({ mosi }) should override DEFAULT_MOSI");
  test.equal(overrideStatus.miso, -1, "spi.openBus({ miso }) should override DEFAULT_MISO");
  test.equal(overrideBus.close(), true, "explicit override SPIBus.close() should succeed");

  return {
    host: busStatus.host,
    sclk: busStatus.sclk,
    mosi: busStatus.mosi,
    miso: busStatus.miso,
    cs: deviceStatus.cs,
    maxTransferSize: busStatus.maxTransferSize,
  };
});
