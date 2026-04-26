test("i2c/status", function () {
  var bus;
  var status;
  var staleError = "";

  test.ok(typeof i2c.DEFAULT_SDA === "number", "i2c.DEFAULT_SDA should be numeric");
  test.ok(typeof i2c.DEFAULT_SCL === "number", "i2c.DEFAULT_SCL should be numeric");
  test.ok(typeof i2c.DEFAULT_FREQ_HZ === "number", "i2c.DEFAULT_FREQ_HZ should be numeric");
  test.ok(typeof i2c.DEFAULT_TIMEOUT_MS === "number", "i2c.DEFAULT_TIMEOUT_MS should be numeric");
  test.ok(typeof i2c.open === "function", "i2c.open should exist");
  test.ok(typeof i2c.status === "undefined", "i2c.status should not exist on the top-level module");
  test.ok(typeof i2c.scan === "undefined", "i2c.scan should not exist on the top-level module");
  test.ok(typeof i2c.write === "undefined", "i2c.write should not exist on the top-level module");

  bus = i2c.open();
  test.ok(bus && typeof bus === "object", "i2c.open() should return an I2CBus object");
  test.ok(typeof bus.status === "function", "I2CBus.status should exist");
  test.ok(typeof bus.scan === "function", "I2CBus.scan should exist");
  test.ok(typeof bus.write === "function", "I2CBus.write should exist");
  test.ok(typeof bus.writeChunks === "function", "I2CBus.writeChunks should exist");
  test.ok(typeof bus.read === "function", "I2CBus.read should exist");
  test.ok(typeof bus.writeRead === "function", "I2CBus.writeRead should exist");

  status = bus.status();
  test.ok(status && typeof status === "object", "I2CBus.status() should return an object");
  test.ok(typeof status.opened === "boolean", "status.opened should be boolean");
  test.ok(typeof status.sda === "number", "status.sda should be numeric");
  test.ok(typeof status.scl === "number", "status.scl should be numeric");
  test.equal(status.opened, true, "I2CBus.status() should report an open bus");

  test.equal(bus.close(), true, "I2CBus.close() should succeed");

  try {
    bus.status();
  } catch (error) {
    staleError = String(error);
  }
  test.ok(staleError.indexOf("closed") >= 0, "closed I2CBus objects should reject further use");

  return { sda: status.sda, scl: status.scl };
});
