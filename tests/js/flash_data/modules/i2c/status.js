test("i2c/status", function () {
  var bus;
  var device;
  var status;
  var deviceStatus;
  var staleError = "";
  var staleDeviceError = "";

  test.ok(typeof i2c.DEFAULT_SDA === "number", "i2c.DEFAULT_SDA should be numeric");
  test.ok(typeof i2c.DEFAULT_SCL === "number", "i2c.DEFAULT_SCL should be numeric");
  test.ok(typeof i2c.DEFAULT_FREQ_HZ === "number", "i2c.DEFAULT_FREQ_HZ should be numeric");
  test.ok(typeof i2c.DEFAULT_TIMEOUT_MS === "number", "i2c.DEFAULT_TIMEOUT_MS should be numeric");
  test.ok(typeof i2c.openBus === "function", "i2c.openBus should exist");
  test.ok(typeof i2c.open === "undefined", "legacy i2c.open should not exist");
  test.ok(typeof i2c.status === "undefined", "i2c.status should not exist on the top-level module");
  test.ok(typeof i2c.scan === "undefined", "i2c.scan should not exist on the top-level module");
  test.ok(typeof i2c.write === "undefined", "i2c.write should not exist on the top-level module");

  bus = i2c.openBus();
  test.ok(bus && typeof bus === "object", "i2c.openBus() should return an I2CBus object");
  test.ok(typeof bus.status === "function", "I2CBus.status should exist");
  test.ok(typeof bus.scan === "function", "I2CBus.scan should exist");
  test.ok(typeof bus.openDevice === "function", "I2CBus.openDevice should exist");
  test.ok(typeof bus.write === "undefined", "I2CBus.write should not exist");

  status = bus.status();
  test.ok(status && typeof status === "object", "I2CBus.status() should return an object");
  test.ok(typeof status.opened === "boolean", "status.opened should be boolean");
  test.ok(typeof status.sda === "number", "status.sda should be numeric");
  test.ok(typeof status.scl === "number", "status.scl should be numeric");
  test.ok(typeof status.controller === "number", "status.controller should be numeric");
  test.equal(status.deviceCount, 0, "new I2CBus should not own devices");
  test.equal(status.opened, true, "I2CBus.status() should report an open bus");

  device = bus.openDevice({ address: 0x3c });
  test.ok(device && typeof device === "object", "openDevice() should return an I2CDevice");
  test.ok(typeof device.status === "function", "I2CDevice.status should exist");
  test.ok(typeof device.write === "function", "I2CDevice.write should exist");
  test.ok(typeof device.writeSegments === "function", "I2CDevice.writeSegments should exist");
  test.ok(typeof device.writeBatch === "function", "I2CDevice.writeBatch should exist");
  test.ok(typeof device.writeChunks === "undefined", "legacy I2CDevice.writeChunks should not exist");
  test.ok(typeof device.read === "function", "I2CDevice.read should exist");
  test.ok(typeof device.writeRead === "function", "I2CDevice.writeRead should exist");
  deviceStatus = device.status();
  test.equal(deviceStatus.address, 0x3c, "I2CDevice.status() should report address");
  test.equal(bus.status().deviceCount, 1, "I2CBus should count its device handle");
  test.equal(device.close(), true, "I2CDevice.close() should succeed");
  try {
    device.status();
  } catch (deviceError) {
    staleDeviceError = String(deviceError);
  }
  test.ok(staleDeviceError.indexOf("closed") >= 0,
    "closed I2CDevice objects should reject further use");

  test.equal(bus.close(), true, "I2CBus.close() should succeed");

  try {
    bus.status();
  } catch (error) {
    staleError = String(error);
  }
  test.ok(staleError.indexOf("closed") >= 0, "closed I2CBus objects should reject further use");

  return { sda: status.sda, scl: status.scl };
});
