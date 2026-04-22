__esp32qjsTest.run("gpio/basic", function () {
  var pin = gpio.USER_LED_PIN >= 0 ? gpio.USER_LED_PIN : gpio.LED_BUILTIN;

  __esp32qjsTest.ok(typeof gpio.INPUT === "string", "gpio.INPUT should be a string");
  __esp32qjsTest.ok(typeof gpio.OUTPUT === "string", "gpio.OUTPUT should be a string");
  __esp32qjsTest.ok(typeof gpio.USER_LED_ACTIVE_LOW === "boolean", "gpio.USER_LED_ACTIVE_LOW should be boolean");
  __esp32qjsTest.ok(typeof gpio.pinMode === "function", "gpio.pinMode should exist");
  __esp32qjsTest.ok(typeof gpio.digitalWrite === "function", "gpio.digitalWrite should exist");
  __esp32qjsTest.ok(typeof gpio.digitalRead === "function", "gpio.digitalRead should exist");
  __esp32qjsTest.ok(typeof gpio.led === "function", "gpio.led should exist");

  if (pin < 0) {
    return { pin: pin, skippedHardwareCheck: true };
  }

  gpio.pinMode(pin, gpio.OUTPUT);
  gpio.digitalWrite(pin, true);
  __esp32qjsTest.ok(typeof gpio.digitalRead(pin) === "boolean", "digitalRead should return a boolean");
  gpio.digitalWrite(pin, false);
  __esp32qjsTest.ok(typeof gpio.digitalRead(pin) === "boolean", "digitalRead should still return a boolean");
  gpio.led(true);
  gpio.led(false);

  return { pin: pin };
});
