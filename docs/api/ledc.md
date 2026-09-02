# `ledc` Module

This module exposes the ESP-IDF LEDC low-level timer/channel primitives. It does not implement higher-level drivers such as servos or `analogWrite(...)`.

- `ledc.AUTO_CLOCK`, `ledc.APB_CLOCK`, `ledc.XTAL_CLOCK`, `ledc.RC_FAST_CLOCK`
  Clock-source strings accepted by `ledc.timerConfig(...)`.
- `ledc.SLEEP_NO_ALIVE_NO_PD`, `ledc.SLEEP_NO_ALIVE_ALLOW_PD`, `ledc.SLEEP_KEEP_ALIVE`
  Sleep-mode strings accepted by `ledc.channelConfig(...)`.
- `ledc.CHANNEL_COUNT`
  Number of LEDC channels on the active target.
- `ledc.TIMER_COUNT`
  Number of LEDC timers on the active target.
- `ledc.MAX_DUTY_RESOLUTION_BITS`
  Maximum duty-resolution bits supported by the active target.
- `ledc.timerConfig(timer, options)`
  Configure or deconfigure one timer. `options` accepts `{ freqHz, dutyResolution, clock, deconfigure }`.
  Deconfiguration pauses the timer first; a pause or deconfigure failure keeps
  the tracked timer and peripheral lease so the same operation or the next
  runtime initialization can retry it.
- `ledc.channelConfig(channel, options)`
  Configure or deconfigure one channel. `options` accepts `{ pin, timer, duty, hpoint, outputInvert, sleepMode, deconfigure }`.
  Deconfiguration stops the channel first and only releases its peripheral
  lease after the driver confirms deconfiguration. A failed stage remains
  retryable and an already successful stop is not repeated.
- `ledc.timerStatus(timer)`
  Return the runtime's tracked timer state as
  `{ timer, configured, paused, freqHz, dutyResolution, maxDuty, clock }`
  without entering the live driver read path. Use `ledc.getFreq(timer)` when a
  live hardware frequency read is explicitly required.
- `ledc.channelStatus(channel)`
  Return `{ channel, configured, pin, timer, duty, hpoint, maxDuty, outputInvert, sleepMode }`.
- `ledc.setDuty(channel, duty)`
- `ledc.setDutyWithHpoint(channel, duty, hpoint)`
- `ledc.setDutyAndUpdate(channel, duty, hpoint?)`
  Update duty/hpoint state and return `ledc.channelStatus(channel)`.
- `ledc.updateDuty(channel)`
  Apply pending duty changes to hardware and return `ledc.channelStatus(channel)`.
- `ledc.getDuty(channel)`
- `ledc.getHpoint(channel)`
  Read live channel state from the driver.
- `ledc.setFreq(timer, freqHz)`
  Update timer frequency and return `ledc.timerStatus(timer)`.
- `ledc.getFreq(timer)`
  Read the live timer frequency.
- `ledc.bindChannelTimer(channel, timer)`
  Rebind a channel to another timer and return `ledc.channelStatus(channel)`.
- `ledc.stop(channel, idleLevel = false)`
  Stop PWM output on a channel.
- `ledc.timerPause(timer)` / `ledc.timerResume(timer)`
  Pause or resume a timer and return `ledc.timerStatus(timer)`.

Example:

```js
var pin = gpio.USER_LED_PIN >= 0 ? gpio.USER_LED_PIN : gpio.LED_BUILTIN;

ledc.timerConfig(0, { freqHz: 5000, dutyResolution: 8 });
ledc.channelConfig(0, {
  pin: pin,
  timer: 0,
  duty: 0,
  sleepMode: ledc.SLEEP_NO_ALIVE_NO_PD,
});

for (var duty = 0; duty <= 255; duty += 32) {
  ledc.setDutyAndUpdate(0, duty);
  sleep(40);
}

ledc.stop(0, false);
ledc.channelConfig(0, { deconfigure: true });
ledc.timerConfig(0, { deconfigure: true });
```
