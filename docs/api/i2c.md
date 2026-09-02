# `i2c` Module

- `i2c.DEFAULT_SDA`
  Default SDA pin from Kconfig.
- `i2c.DEFAULT_SCL`
  Default SCL pin from Kconfig.
- `i2c.DEFAULT_FREQ_HZ`
  Default bus speed, `400000`.
- `i2c.DEFAULT_TIMEOUT_MS`
  Default transfer timeout in milliseconds.
- `i2c.openBus(options?)`
  Open an I2C master controller and return an `I2CBus`. `options` can include `{ sda, scl, freqHz, timeoutMs, internalPullup }`. The legacy `i2c.open()` entry point is not part of the v1 contract.

`I2CBus` methods:

- `bus.status()`
  Return `{ opened, controller, sda, scl, freqHz, timeoutMs, internalPullup, deviceCount }`.
- `bus.close()`
  Close the bus handle. After closing, the `I2CBus` instance becomes stale and its other methods throw. If a caller forgets to close it, GC finalization will also release the native handle eventually, but explicit `close()` remains the intended lifecycle boundary.
- `bus.scan()`
  Probe `0x03..0x77` and return an array of 7-bit device addresses.
- `bus.openDevice({ address, freqHz?, timeoutMs? })`
  Create an `I2CDevice` with a persistent ESP-IDF device handle. The device inherits the bus frequency and timeout unless overridden.

`I2CDevice` methods:

- `device.status()`
  Return `{ opened, controller, address, freqHz, timeoutMs }`.
- `device.close()`
  Close the device handle. Close devices before their owning bus.
- `device.write(data)`
  Execute one transaction containing one `ByteSource` and return its byte count.
- `device.writeSegments(segments)`
  Send all native segments inside one START/STOP transaction without joining them into a JavaScript array. It returns the total byte count.
- `device.writeBatch(chunks)`
  Execute chunks as independent ordered transactions and return `{ chunks, bytes, totalUs }`.
- `device.read(length)`
  Read `length` bytes into an owned `ByteView`. Close the view after use.
- `device.writeRead(writeData, readLength)`
  Write bytes, then read bytes in one transaction and return an owned
  `ByteView`.

`scan()`, `write()`, `writeSegments()`, `writeBatch()`, `read()`, and
`writeRead()` are native Future operations. Operations for the same controller
use one bounded FIFO resource lane; separate controllers may progress
independently. I2C master readiness still uses `gpio.watch()` rather than a
bus-specific watcher.

Example:

```js
var bus = i2c.openBus({ sda: 5, scl: 6, freqHz: 400000 });
var device;
print(JSON.stringify(bus.status()));
print(JSON.stringify(bus.scan())); // [60] for an SSD1306 at 0x3c
device = bus.openDevice({ address: 0x3c });
print(device.write([0x00, 0xAF])); // SSD1306 display on
device.close();
bus.close();
```
