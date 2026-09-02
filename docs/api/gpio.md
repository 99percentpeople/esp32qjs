# `gpio` Module

- `gpio.DISABLED`
  Disabled mode string for `gpio.pinMode()` or `gpio.configure()`.
- `gpio.INPUT`
  Input mode string for `gpio.pinMode()`.
- `gpio.OUTPUT`
  Output mode string for `gpio.pinMode()`.
- `gpio.INPUT_OUTPUT`
  Input + output mode string for bidirectional pins.
- `gpio.OUTPUT_OPEN_DRAIN`
  Open-drain output mode string.
- `gpio.INPUT_OUTPUT_OPEN_DRAIN`
  Open-drain input + output mode string.
- `gpio.FLOATING`
  Disable internal pulls.
- `gpio.PULLUP`
  Enable the internal pull-up.
- `gpio.PULLDOWN`
  Enable the internal pull-down.
- `gpio.PULLUP_PULLDOWN`
  Enable both internal pulls when supported by the pad.
- `gpio.CHANGE`, `gpio.RISING`, `gpio.FALLING`
  Edge-trigger constants accepted by `gpio.watch(...)`.
- `gpio.LOW`, `gpio.HIGH`
  Numeric helpers (`0` / `1`) used both for output levels and level-trigger
  interrupt modes in `gpio.watch(...)`.
- `gpio.DRIVE_0` .. `gpio.DRIVE_3`
  Drive-strength levels accepted by `gpio.setDriveStrength()` and `gpio.configure()`.
- `gpio.LED_BUILTIN`
  LED pin from the current wiring profile, or `-1` when unset.
- `gpio.USER_LED_PIN`
  User LED pin from the current wiring profile, or `-1` when unset.
- `gpio.USER_LED_ACTIVE_LOW`
  Whether the configured LED is active-low.
- `gpio.isValid(pin)`
  Return `true` when `pin` is a usable digital GPIO on the current target.
- `gpio.isOutputCapable(pin)`
  Return `true` when the pad supports output mode, drive strength, and hold.
- `gpio.pinMode(pin, mode)`
  Configure a GPIO as `gpio.DISABLED`, `gpio.INPUT`, `gpio.OUTPUT`, `gpio.INPUT_OUTPUT`, `gpio.OUTPUT_OPEN_DRAIN`, or `gpio.INPUT_OUTPUT_OPEN_DRAIN`.
- `gpio.setPull(pin, mode)`
  Set internal pull resistors with `gpio.FLOATING`, `gpio.PULLUP`, `gpio.PULLDOWN`, or `gpio.PULLUP_PULLDOWN`.
- `gpio.status(pin)`
  Return the live pad configuration object:
  `{ pin, valid, outputCapable, mode, pull, level, inputEnabled, outputEnabled, openDrain, pullup, pulldown, driveStrength, held, functionSelect, signalOut, outputControlledByPeripheral, outputEnableInverted, sleepEnabled, interruptAttached, interruptMode, interruptDropped }`.
- `gpio.configure(pin, options)`
  Apply `{ mode, pull, level, driveStrength, hold }` in one call and return `gpio.status(pin)`.
- `gpio.digitalWrite(pin, value)`
  Set a GPIO output level. If the pin is not already output-enabled, the helper promotes it to `gpio.OUTPUT`.
- `gpio.digitalRead(pin)`
  Read a GPIO level and return `true` or `false`.
- `gpio.toggle(pin)`
  Flip the current output level and return the new boolean level.
- `gpio.getDriveStrength(pin)`
  Return the current numeric drive strength (`0..3`).
- `gpio.setDriveStrength(pin, strength)`
  Update the pad drive strength and return the applied numeric value.
- `gpio.hold(pin, enabled)`
  Enable or disable pad hold on output-capable GPIOs.
- `gpio.watch(pin, mode = gpio.CHANGE)`
  Return a bounded `EventQueue` of
  `{ sequence, timestampUs, pin, level, mode }` interrupt events. The ISR
  captures the level with the sequence and timestamp before enqueueing, so a
  delayed consumer does not observe a newer pin level for an older event. The
  ESP-IDF ISR never invokes JavaScript. Only one watcher may own a pin; creating
  another closes and replaces the old watcher. Closing the queue detaches the
  interrupt.
- `gpio.reset(pin)`
  Reset the pad back to the ESP-IDF default GPIO state.
- `gpio.led(value)`
  Control the configured user LED. `true` turns it on. Throws when no LED pin
  is defined by the wiring profile.

Examples:

Configure an output and toggle it:

```js
var pin = gpio.USER_LED_PIN >= 0 ? gpio.USER_LED_PIN : gpio.LED_BUILTIN;

// Make the LED pin a push-pull output with a known initial level.
print(gpio.isValid(pin), gpio.isOutputCapable(pin));
print(JSON.stringify(gpio.configure(pin, {
  mode: gpio.OUTPUT,
  pull: gpio.FLOATING,
  driveStrength: gpio.DRIVE_1,
  level: gpio.HIGH,
})));

// Flip the output and read back the new boolean level.
sleep(100);
print(gpio.toggle(pin)); // false
gpio.led(false);
```

Read an input with pull-up enabled:

```js
var buttonPin = 9;

// Typical button wiring uses INPUT + pull-up and reads LOW when pressed.
gpio.pinMode(buttonPin, gpio.INPUT);
gpio.setPull(buttonPin, gpio.PULLUP);

print("button level:", gpio.digitalRead(buttonPin));
print(JSON.stringify(gpio.status(buttonPin)));
```

Watch an interrupt without blocking the startup call stack:

```js
var buttonPin = 9;
var interruptCount = 0;
var watching = true;

gpio.pinMode(buttonPin, gpio.INPUT);
gpio.setPull(buttonPin, gpio.PULLUP);
var interrupts = gpio.watch(buttonPin, gpio.FALLING);

function armInterrupt() {
  if (!watching) return;
  Future.call(interrupts.receive, interrupts, []).map(function (event) {
    if (!watching || event === null) return;
    interruptCount++;
    print("interrupt", interruptCount, event.pin, event.mode, event.level);
    armInterrupt();
  });
}

armInterrupt();

// Later, when the pin is no longer needed:
watching = false;
interrupts.close();
```

Use level-triggered interrupts explicitly:

```js
var pin = 9;

gpio.pinMode(pin, gpio.INPUT);
gpio.setPull(pin, gpio.PULLDOWN);

// LOW/HIGH reuse the same 0/1 constants as digital levels. Level-triggered
// sources can fill the bounded queue quickly, so consume or close it promptly.
var levels = gpio.watch(pin, gpio.HIGH);
// Consume it with Future.call(levels.receive, levels, []) as above, then close.
levels.close();
```
