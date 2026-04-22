__esp32qjsTest.run("i2c/status", function () {
  var status = i2c.status();
  var opened;
  var closed;

  __esp32qjsTest.ok(status && typeof status === "object", "i2c.status() should return an object");
  __esp32qjsTest.ok(typeof i2c.DEFAULT_SDA === "number", "i2c.DEFAULT_SDA should be numeric");
  __esp32qjsTest.ok(typeof i2c.DEFAULT_SCL === "number", "i2c.DEFAULT_SCL should be numeric");
  __esp32qjsTest.ok(typeof i2c.DEFAULT_FREQ_HZ === "number", "i2c.DEFAULT_FREQ_HZ should be numeric");
  __esp32qjsTest.ok(typeof i2c.DEFAULT_TIMEOUT_MS === "number", "i2c.DEFAULT_TIMEOUT_MS should be numeric");
  __esp32qjsTest.ok(typeof status.opened === "boolean", "status.opened should be boolean");
  __esp32qjsTest.ok(typeof status.sda === "number", "status.sda should be numeric");
  __esp32qjsTest.ok(typeof status.scl === "number", "status.scl should be numeric");

  opened = i2c.open();
  __esp32qjsTest.ok(opened.opened, "i2c.open() should mark the bus open");
  __esp32qjsTest.equal(i2c.status().opened, true, "i2c.status() should report an open bus");
  i2c.close();
  closed = i2c.status();
  __esp32qjsTest.equal(closed.opened, false, "i2c.close() should close the bus");

  return { sda: closed.sda, scl: closed.scl };
});
