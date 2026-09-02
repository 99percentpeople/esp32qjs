# `dac` Module

This module exposes ESP-IDF DAC oneshot primitives and GPIO/channel mapping helpers. It is registered only on boards that compile with the `dac` feature enabled.

- `dac.CHANNEL_0`, `dac.CHANNEL_1`
  DAC channel identifiers accepted by `dac.open(...)`, `dac.close(...)`, `dac.status(...)`, `dac.write(...)`, and `dac.channelToIo(...)`.
- `dac.CHANNEL_COUNT`
  Number of DAC channels on the active target.
- `dac.RESOLUTION_BITS`
  DAC output resolution in raw digital bits.
- `dac.MAX_VALUE`
  Maximum raw value accepted by `dac.write(...)`.
- `dac.open(channel)`
  Open one DAC oneshot channel and return `dac.status(channel)`.
- `dac.close(channel)`
  Close one DAC channel and release its oneshot handle.
- `dac.status(channel?)`
  Return one channel status object or, when called with no arguments, an array of all channel statuses.
- `dac.write(channel, value)`
  Output one raw DAC value `0..dac.MAX_VALUE` and return `dac.status(channel)`.
- `dac.ioToChannel(pin)`
  Map a GPIO to `{ channel, pin }` or return `null` when the pad is not DAC-capable.
- `dac.channelToIo(channel)`
  Map a DAC channel back to its GPIO number.

Status objects look like:

```js
{
  channel: 0,
  opened: true,
  pin: 25,
  resolutionBits: 8,
  maxValue: 255,
  lastValue: 128
}
```

Example:

```js
var ref = dac.ioToChannel(25);

if (ref) {
  dac.open(ref.channel);
  dac.write(ref.channel, 128);
  print(JSON.stringify(dac.status(ref.channel)));
  dac.close(ref.channel);
}
```
