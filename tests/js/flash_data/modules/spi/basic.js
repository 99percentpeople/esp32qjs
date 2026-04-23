test("spi/basic", function () {
  var info = esp32.info();
  var bus;
  var busStatus;
  var device;
  var deviceStatus;
  var overrideBus;
  var overrideStatus;
  var staleBusError = "";
  var staleDeviceError = "";

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

  device = bus.openDevice();
  test.ok(device && typeof device === "object", "SPIBus.openDevice() should return an SPIDevice object");
  test.ok(typeof device.status === "function", "SPIDevice.status should exist");
  test.ok(typeof device.transfer === "function", "SPIDevice.transfer should exist");
  test.ok(typeof device.write === "function", "SPIDevice.write should exist");
  test.ok(typeof device.read === "function", "SPIDevice.read should exist");

  deviceStatus = device.status();
  test.ok(deviceStatus && typeof deviceStatus === "object", "SPIDevice.status() should return an object");
  test.equal(deviceStatus.opened, true, "SPIDevice.status() should report an open device");
  test.equal(deviceStatus.cs, spi.DEFAULT_CS, "SPIDevice.status().cs should use DEFAULT_CS");
  test.equal(device.write([]), 0, "SPIDevice.write([]) should succeed");
  test.equal(device.transfer([]).length, 0, "SPIDevice.transfer([]) should return an empty array");
  test.equal(device.read(0).length, 0, "SPIDevice.read(0) should return an empty array");

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
