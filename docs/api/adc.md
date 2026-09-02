# `adc` Module

This module exposes ESP-IDF ADC oneshot primitives and GPIO/channel mapping helpers. It does not implement development-board-specific sensor drivers.

- `adc.UNIT_1`, `adc.UNIT_2`
  ADC unit identifiers accepted by `adc.open(...)`, `adc.status(...)`, and the read/configure helpers.
- `adc.ATTEN_DB_0`, `adc.ATTEN_DB_2_5`, `adc.ATTEN_DB_6`, `adc.ATTEN_DB_12`
  Attenuation constants accepted by `adc.configure(...)`.
- `adc.BITWIDTH_DEFAULT`, `adc.BITWIDTH_9` .. `adc.BITWIDTH_13`
  Bit-width constants accepted by `adc.configure(...)`.
- `adc.UNIT_COUNT`
  Number of ADC units on the active target.
- `adc.MAX_CHANNEL_COUNT`
  Maximum channels available on any ADC unit for the active target.
- `adc.open(unit)`
  Open one ADC unit for oneshot reads and return `adc.status(unit)`. Reopening
  first closes the existing unit; if calibration or unit teardown fails, the
  surviving resources remain owned and the operation throws so a later
  `adc.open(...)` or `adc.close(...)` can retry cleanup.
- `adc.close(unit)`
  Close one ADC unit and release all per-channel calibration state before the
  unit handle. A native deletion failure throws and retains the exact remaining
  resource suffix for retry; success resets the unit state.
- `adc.status(unit)`
  Return `{ unit, opened, channelCount, channels }`, where `channels` contains `{ channel, configured, atten, bitwidth, pin, calibrated }`.
- `adc.configure(unit, channel, options)`
  Configure a channel with `{ atten, bitwidth }` and return `adc.status(unit)`.
  Existing calibration is deleted before changing the hardware configuration;
  if deletion fails, the old calibration and configuration remain owned and no
  replacement is attempted. Creating new calibration remains best effort, so a
  successfully configured channel can report `calibrated: false` when the
  target or current conditions cannot provide a calibration scheme.
- `adc.read(unit, channel)`
  Perform one raw oneshot read and return the integer ADC result.
- `adc.readMilliVolts(unit, channel)`
  Return a calibrated result in mV when calibration is available; otherwise it throws a clear calibration-availability error.
- `adc.ioToChannel(pin)`
  Map a GPIO to `{ unit, channel }` or return `null` when the pad is not ADC-capable.
- `adc.channelToIo(unit, channel)`
  Map a unit/channel pair back to its GPIO number or return `null`.

Example:

```js
var ref = adc.ioToChannel(0);

if (ref) {
  adc.open(ref.unit);
  adc.configure(ref.unit, ref.channel, {
    atten: adc.ATTEN_DB_12,
    bitwidth: adc.BITWIDTH_12,
  });

  print(adc.read(ref.unit, ref.channel));
  try {
    print(adc.readMilliVolts(ref.unit, ref.channel));
  } catch (error) {
    print(error.message || error);
  }

  adc.close(ref.unit);
}
```
