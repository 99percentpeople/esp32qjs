test("gpio/basic", function () {
  var pin = gpio.USER_LED_PIN >= 0 ? gpio.USER_LED_PIN : gpio.LED_BUILTIN;
  var initialDriveStrength;
  var status;

  test.ok(typeof gpio.DISABLED === "string", "gpio.DISABLED should be a string");
  test.ok(typeof gpio.INPUT === "string", "gpio.INPUT should be a string");
  test.ok(typeof gpio.OUTPUT === "string", "gpio.OUTPUT should be a string");
  test.ok(typeof gpio.INPUT_OUTPUT === "string", "gpio.INPUT_OUTPUT should be a string");
  test.ok(typeof gpio.OUTPUT_OPEN_DRAIN === "string", "gpio.OUTPUT_OPEN_DRAIN should be a string");
  test.ok(typeof gpio.INPUT_OUTPUT_OPEN_DRAIN === "string", "gpio.INPUT_OUTPUT_OPEN_DRAIN should be a string");
  test.ok(typeof gpio.FLOATING === "string", "gpio.FLOATING should be a string");
  test.ok(typeof gpio.PULLUP === "string", "gpio.PULLUP should be a string");
  test.ok(typeof gpio.PULLDOWN === "string", "gpio.PULLDOWN should be a string");
  test.ok(typeof gpio.PULLUP_PULLDOWN === "string", "gpio.PULLUP_PULLDOWN should be a string");
  test.ok(typeof gpio.CHANGE === "string", "gpio.CHANGE should be a string");
  test.ok(typeof gpio.RISING === "string", "gpio.RISING should be a string");
  test.ok(typeof gpio.FALLING === "string", "gpio.FALLING should be a string");
  test.equal(gpio.LOW, 0, "gpio.LOW should be 0");
  test.equal(gpio.HIGH, 1, "gpio.HIGH should be 1");
  test.equal(gpio.DRIVE_0, 0, "gpio.DRIVE_0 should be 0");
  test.equal(gpio.DRIVE_3, 3, "gpio.DRIVE_3 should be 3");
  test.ok(typeof gpio.USER_LED_ACTIVE_LOW === "boolean", "gpio.USER_LED_ACTIVE_LOW should be boolean");
  test.ok(typeof gpio.isValid === "function", "gpio.isValid should exist");
  test.ok(typeof gpio.isOutputCapable === "function", "gpio.isOutputCapable should exist");
  test.ok(typeof gpio.pinMode === "function", "gpio.pinMode should exist");
  test.ok(typeof gpio.setPull === "function", "gpio.setPull should exist");
  test.ok(typeof gpio.status === "function", "gpio.status should exist");
  test.ok(typeof gpio.configure === "function", "gpio.configure should exist");
  test.ok(typeof gpio.digitalWrite === "function", "gpio.digitalWrite should exist");
  test.ok(typeof gpio.digitalRead === "function", "gpio.digitalRead should exist");
  test.ok(typeof gpio.toggle === "function", "gpio.toggle should exist");
  test.ok(typeof gpio.getDriveStrength === "function", "gpio.getDriveStrength should exist");
  test.ok(typeof gpio.setDriveStrength === "function", "gpio.setDriveStrength should exist");
  test.ok(typeof gpio.hold === "function", "gpio.hold should exist");
  test.ok(typeof gpio.watch === "function", "gpio.watch should exist");
  test.ok(typeof gpio.attachInterrupt === "undefined", "legacy interrupt callbacks should be removed");
  test.ok(typeof gpio.reset === "function", "gpio.reset should exist");
  test.ok(typeof gpio.led === "function", "gpio.led should exist");
  test.ok(!gpio.isValid(-1), "negative pin should be invalid");
  test.ok(!gpio.isOutputCapable(-1), "negative pin should not be output capable");
  test.ok(!gpio.isValid([0]), "array pins must not be coerced to GPIO 0");
  test.ok(!gpio.isValid("0"), "string pins must not be coerced to GPIO 0");
  var invalidWatchCaught = false;
  try {
    gpio.watch([0]);
  } catch (invalidWatchError) {
    invalidWatchCaught = String(invalidWatchError).indexOf("expects a valid GPIO") >= 0;
  }
  test.ok(invalidWatchCaught, "gpio.watch should reject array pins");

  if (pin < 0) {
    return { pin: pin, skippedHardwareCheck: true };
  }

  test.ok(gpio.isValid(pin), "board LED pin should be valid");
  test.ok(gpio.isOutputCapable(pin), "board LED pin should be output capable");

  gpio.reset(pin);
  status = gpio.status(pin);
  test.equal(status.pin, pin, "status pin");
  test.ok(status.valid, "status should report valid pin");
  test.ok(status.outputCapable, "status should report output capable pin");
  test.equal(status.mode, gpio.DISABLED, "reset should disable input/output");
  test.ok(typeof status.level === "boolean", "status.level should be boolean");
  test.ok(typeof status.driveStrength === "number", "status.driveStrength should be numeric");
  test.ok(typeof status.functionSelect === "number", "status.functionSelect should be numeric");
  test.ok(typeof status.signalOut === "number", "status.signalOut should be numeric");
  test.ok(typeof status.outputControlledByPeripheral === "boolean", "status.outputControlledByPeripheral should be boolean");
  test.ok(typeof status.outputEnableInverted === "boolean", "status.outputEnableInverted should be boolean");
  test.ok(typeof status.sleepEnabled === "boolean", "status.sleepEnabled should be boolean");
  test.ok(typeof status.interruptAttached === "boolean", "status.interruptAttached should be boolean");
  test.equal(status.interruptMode, null, "reset should clear interrupt mode");
  test.ok(typeof status.interruptDropped === "number", "status.interruptDropped should be numeric");

  gpio.pinMode(pin, gpio.INPUT_OUTPUT);
  status = gpio.status(pin);
  test.equal(status.mode, gpio.INPUT_OUTPUT, "pinMode should support INPUT_OUTPUT");
  test.ok(status.inputEnabled, "INPUT_OUTPUT should enable input");
  test.ok(status.outputEnabled, "INPUT_OUTPUT should enable output");

  gpio.setPull(pin, gpio.PULLDOWN);
  status = gpio.status(pin);
  test.equal(status.pull, gpio.PULLDOWN, "setPull should update pull mode");
  test.ok(status.pulldown, "setPull should enable pulldown");

  initialDriveStrength = gpio.getDriveStrength(pin);
  test.ok(initialDriveStrength >= gpio.DRIVE_0 && initialDriveStrength <= gpio.DRIVE_3, "initial drive strength range");
  gpio.setDriveStrength(pin, gpio.DRIVE_1);
  test.equal(gpio.getDriveStrength(pin), gpio.DRIVE_1, "setDriveStrength should update drive strength");

  gpio.pinMode(pin, gpio.OUTPUT);
  gpio.digitalWrite(pin, true);
  test.ok(typeof gpio.digitalRead(pin) === "boolean", "digitalRead should return a boolean");
  var toggledLevel = gpio.toggle(pin);
  test.ok(typeof toggledLevel === "boolean", "toggle should return a boolean");
  test.ok(typeof gpio.digitalRead(pin) === "boolean", "digitalRead should stay boolean after toggle");
  gpio.digitalWrite(pin, false);
  test.ok(typeof gpio.digitalRead(pin) === "boolean", "digitalRead should still return a boolean");
  gpio.configure(pin, {
    mode: gpio.INPUT_OUTPUT,
    pull: gpio.PULLDOWN,
    level: gpio.LOW,
  });
  var interrupts = gpio.watch(pin, gpio.CHANGE);
  var queueStats = interrupts.stats();
  test.ok(queueStats.open, "interrupt EventQueue should start open");
  test.equal(queueStats.queued, 0, "new interrupt EventQueue should be empty");
  test.ok(queueStats.capacity > 0, "interrupt EventQueue should report capacity");
  test.equal(queueStats.dropped, 0, "new interrupt EventQueue should not report drops");
  test.ok(!queueStats.receiverPending, "new interrupt EventQueue should not have a receiver");
  status = gpio.status(pin);
  test.ok(status.interruptAttached, "watch should update status");
  test.equal(status.interruptMode, gpio.CHANGE, "watch should report change mode");
  test.equal(status.interruptDropped, 0, "watch should reset dropped count");
  gpio.digitalWrite(pin, true);
  var interruptEvent = interrupts.receive(1000);
  test.equal(interruptEvent.pin, pin, "interrupt event pin");
  test.equal(interruptEvent.mode, gpio.CHANGE, "interrupt event mode");
  test.equal(interruptEvent.level, true,
    "interrupt event should retain the level captured by the ISR");
  test.equal(interruptEvent.sequence, 1,
    "first interrupt event should start a watcher-local sequence");
  test.ok(interruptEvent.timestampUs > 0,
    "interrupt event should include a monotonic timestamp");
  gpio.digitalWrite(pin, false);
  var secondInterruptEvent = interrupts.receive(1000);
  test.equal(secondInterruptEvent.level, false,
    "queued interrupt events should retain their own captured level");
  test.equal(secondInterruptEvent.sequence, interruptEvent.sequence + 1,
    "interrupt sequence should advance for every ISR event");
  test.ok(secondInterruptEvent.timestampUs >= interruptEvent.timestampUs,
    "interrupt timestamps should be monotonic");
  test.ok(interrupts.close(), "interrupt EventQueue should close");
  queueStats = interrupts.stats();
  test.ok(!queueStats.open, "closed interrupt EventQueue should report closed");
  test.equal(queueStats.queued, 0, "consumed interrupt EventQueue should be empty");
  status = gpio.status(pin);
  test.ok(!status.interruptAttached, "queue close should detach the interrupt");
  test.equal(status.interruptMode, null, "queue close should clear mode");
  gpio.digitalWrite(pin, false);
  test.equal(interrupts.receive(80), null, "closed interrupt queue should not receive new events");
  status = gpio.configure(pin, {
    mode: gpio.OUTPUT,
    pull: gpio.FLOATING,
    driveStrength: initialDriveStrength,
    level: gpio.HIGH,
  });
  test.equal(status.mode, gpio.OUTPUT, "configure should keep output mode");
  test.equal(status.pull, gpio.FLOATING, "configure should update pull mode");
  test.equal(status.driveStrength, initialDriveStrength, "configure should restore drive strength");
  test.ok(typeof status.level === "boolean", "configure should report a boolean level");
  gpio.hold(pin, true);
  test.ok(gpio.status(pin).held, "hold(true) should be reflected in status");
  gpio.hold(pin, false);
  test.ok(!gpio.status(pin).held, "hold(false) should clear held state");
  gpio.led(true);
  gpio.led(false);
  gpio.reset(pin);

  return { pin: pin };
});
