# REPL API Reference

This project exposes a small JavaScript REPL on the XIAO ESP32-S3. Run `help()` on the device to point back to this document.

## Global Helpers

- `help()`
  Print the location of this document.
- `print(...values)`
  Write values to the serial console.
- `gc()`
  Run the JavaScript garbage collector.
- `load(path)`
  Evaluate a script from LittleFS. Relative paths resolve under `/littlefs`, and paths cannot escape that root.
- `sleep(ms)` / `delay(ms)`
  Block the REPL task for `ms` milliseconds.

Examples:

```js
print("hello");
gc();
sleep(50);
load("demo.js");
```

## Timers

- `setTimeout(fn, ms)`
  Run `fn` once after `ms` milliseconds.
- `clearTimeout(id)`
  Cancel a timeout timer.
- `setInterval(fn, ms)`
  Run `fn` repeatedly every `ms` milliseconds.
- `clearInterval(id)`
  Cancel an interval timer.

Example:

```js
const id = setInterval(function () { print("tick"); }, 500);
clearInterval(id);
```

## Constants

- `LED_BUILTIN`
  Built-in LED pin number, `21`.
- `SCRIPTS_DIR`
  Script base directory, `"/littlefs"`.

## `fs` Module

All `fs` operations are restricted to `/littlefs`.

- `fs.ROOT`
  File-system root, `"/littlefs"`.
- `fs.list(path = ".")`
  Return an array of entries for a directory.
- `fs.stat(path)`
  Return `{ name, path, isDir, size }` for a path.
- `fs.exists(path)`
  Return `true` if the path exists.
- `fs.readText(path)`
  Read a UTF-8 text file.
- `fs.writeText(path, text)`
  Overwrite a text file and return the number of bytes written.
- `fs.appendText(path, text)`
  Append text and return the number of bytes written.
- `fs.mkdir(path)`
  Create one directory level.
- `fs.rename(fromPath, toPath)`
  Rename a file or directory.
- `fs.remove(path)`
  Remove a file or an empty directory.

Example:

```js
fs.writeText("notes.txt", "hello\n");
print(fs.readText("notes.txt"));
print(fs.stat("notes.txt"));
print(JSON.stringify(fs.list(".")));
fs.rename("notes.txt", "notes-old.txt");
fs.remove("notes-old.txt");
```

## `gpio` Module

- `gpio.INPUT`
  Input mode string for `gpio.pinMode()`.
- `gpio.OUTPUT`
  Output mode string for `gpio.pinMode()`.
- `gpio.LED_BUILTIN`
  Built-in LED pin number, `21`.
- `gpio.USER_LED_PIN`
  XIAO ESP32-S3 user LED pin number.
- `gpio.USER_LED_ACTIVE_LOW`
  `true` because the board LED is active-low.
- `gpio.pinMode(pin, mode)`
  Configure a GPIO as `gpio.INPUT` or `gpio.OUTPUT`.
- `gpio.digitalWrite(pin, value)`
  Set a GPIO output level.
- `gpio.digitalRead(pin)`
  Read a GPIO level and return `true` or `false`.
- `gpio.led(value)`
  Control the XIAO ESP32-S3 user LED. `true` turns it on.

Example:

```js
gpio.pinMode(gpio.LED_BUILTIN, gpio.OUTPUT);
gpio.led(true);
sleep(100);
gpio.led(false);
```

## `esp32` Module

- `esp32.info()`
  Return board, chip, LED, script directory, free heap, and current JS time.
- `esp32.millis()`
  Return monotonic milliseconds from `esp_timer`.
- `esp32.micros()`
  Return monotonic microseconds from `esp_timer`.
- `esp32.freeHeap()`
  Return current free heap in bytes.

Example:

```js
print(esp32.info());
print(esp32.millis());
print(esp32.freeHeap());
```
