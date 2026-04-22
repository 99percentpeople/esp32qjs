test("ledc/basic", function () {
  function cleanup() {
    try {
      ledc.channelConfig(channel, { deconfigure: true });
    } catch (cleanupChannelError) {}
    try {
      ledc.timerConfig(timer, { deconfigure: true });
    } catch (cleanupTimerError) {}
    gpio.reset(pin);
  }

  var pin = gpio.USER_LED_PIN >= 0 ? gpio.USER_LED_PIN : gpio.LED_BUILTIN;
  var timer = 0;
  var channel = 0;
  var timerStatus;
  var channelStatus;

  test.ok(typeof ledc.AUTO_CLOCK === "string", "ledc.AUTO_CLOCK should be a string");
  test.ok(typeof ledc.APB_CLOCK === "string", "ledc.APB_CLOCK should be a string");
  test.ok(typeof ledc.XTAL_CLOCK === "string", "ledc.XTAL_CLOCK should be a string");
  test.ok(typeof ledc.RC_FAST_CLOCK === "string", "ledc.RC_FAST_CLOCK should be a string");
  test.ok(typeof ledc.SLEEP_NO_ALIVE_NO_PD === "string", "ledc.SLEEP_NO_ALIVE_NO_PD should be a string");
  test.ok(typeof ledc.SLEEP_NO_ALIVE_ALLOW_PD === "string", "ledc.SLEEP_NO_ALIVE_ALLOW_PD should be a string");
  test.ok(typeof ledc.SLEEP_KEEP_ALIVE === "string", "ledc.SLEEP_KEEP_ALIVE should be a string");
  test.ok(typeof ledc.CHANNEL_COUNT === "number" && ledc.CHANNEL_COUNT > 0, "ledc.CHANNEL_COUNT should be positive");
  test.ok(typeof ledc.TIMER_COUNT === "number" && ledc.TIMER_COUNT > 0, "ledc.TIMER_COUNT should be positive");
  test.ok(typeof ledc.MAX_DUTY_RESOLUTION_BITS === "number" && ledc.MAX_DUTY_RESOLUTION_BITS >= 8,
    "ledc.MAX_DUTY_RESOLUTION_BITS should be numeric");
  test.ok(typeof ledc.timerConfig === "function", "ledc.timerConfig should exist");
  test.ok(typeof ledc.channelConfig === "function", "ledc.channelConfig should exist");
  test.ok(typeof ledc.setDuty === "function", "ledc.setDuty should exist");
  test.ok(typeof ledc.setDutyWithHpoint === "function", "ledc.setDutyWithHpoint should exist");
  test.ok(typeof ledc.setDutyAndUpdate === "function", "ledc.setDutyAndUpdate should exist");
  test.ok(typeof ledc.getDuty === "function", "ledc.getDuty should exist");
  test.ok(typeof ledc.getHpoint === "function", "ledc.getHpoint should exist");
  test.ok(typeof ledc.updateDuty === "function", "ledc.updateDuty should exist");
  test.ok(typeof ledc.setFreq === "function", "ledc.setFreq should exist");
  test.ok(typeof ledc.getFreq === "function", "ledc.getFreq should exist");
  test.ok(typeof ledc.bindChannelTimer === "function", "ledc.bindChannelTimer should exist");
  test.ok(typeof ledc.stop === "function", "ledc.stop should exist");
  test.ok(typeof ledc.timerPause === "function", "ledc.timerPause should exist");
  test.ok(typeof ledc.timerResume === "function", "ledc.timerResume should exist");
  test.ok(typeof ledc.timerStatus === "function", "ledc.timerStatus should exist");
  test.ok(typeof ledc.channelStatus === "function", "ledc.channelStatus should exist");

  if (pin < 0 || !gpio.isValid(pin) || !gpio.isOutputCapable(pin)) {
    return { pin: pin, skippedHardwareCheck: true };
  }

  try {
    timerStatus = ledc.timerConfig(timer, {
      freqHz: 5000,
      dutyResolution: 8,
      clock: ledc.AUTO_CLOCK,
    });
    test.ok(timerStatus.configured, "timerConfig should configure the timer");
    test.equal(timerStatus.timer, timer, "timerStatus.timer should match");
    test.equal(timerStatus.freqHz, 5000, "timerConfig should apply freqHz");
    test.equal(timerStatus.dutyResolution, 8, "timerConfig should apply duty resolution");
    test.equal(timerStatus.maxDuty, 255, "8-bit timer max duty should be 255");
    test.equal(ledc.getFreq(timer), 5000, "getFreq should report timer frequency");

    channelStatus = ledc.channelConfig(channel, {
      pin: pin,
      timer: timer,
      duty: 0,
      hpoint: 0,
      outputInvert: false,
      sleepMode: ledc.SLEEP_NO_ALIVE_NO_PD,
    });
    test.ok(channelStatus.configured, "channelConfig should configure the channel");
    test.equal(channelStatus.channel, channel, "channelStatus.channel should match");
    test.equal(channelStatus.pin, pin, "channelStatus.pin should match");
    test.equal(channelStatus.timer, timer, "channelStatus.timer should match");
    test.equal(channelStatus.maxDuty, 255, "channel max duty should follow timer resolution");

    channelStatus = ledc.setDuty(channel, 128);
    test.equal(channelStatus.duty, 128, "setDuty should update tracked duty");
    channelStatus = ledc.updateDuty(channel);
    test.equal(ledc.getDuty(channel), 128, "getDuty should report updated duty");

    channelStatus = ledc.setDutyWithHpoint(channel, 64, 3);
    test.equal(channelStatus.hpoint, 3, "setDutyWithHpoint should track hpoint");
    ledc.updateDuty(channel);
    test.equal(ledc.getHpoint(channel), 3, "getHpoint should report updated hpoint");

    channelStatus = ledc.setDutyAndUpdate(channel, 32, 2);
    test.equal(channelStatus.duty, 32, "setDutyAndUpdate should update duty");
    test.equal(ledc.getDuty(channel), 32, "getDuty should reflect setDutyAndUpdate");

    timerStatus = ledc.setFreq(timer, 2000);
    test.equal(timerStatus.freqHz, 2000, "setFreq should update timer frequency");
    test.equal(ledc.getFreq(timer), 2000, "getFreq should reflect setFreq");

    channelStatus = ledc.bindChannelTimer(channel, timer);
    test.equal(channelStatus.timer, timer, "bindChannelTimer should keep timer binding");

    timerStatus = ledc.timerPause(timer);
    test.ok(timerStatus.paused, "timerPause should update paused state");
    timerStatus = ledc.timerResume(timer);
    test.ok(!timerStatus.paused, "timerResume should clear paused state");

    channelStatus = ledc.stop(channel, false);
    test.equal(channelStatus.channel, channel, "stop should return channel status");

    return { pin: pin, timer: timer, channel: channel };
  } finally {
    cleanup();
  }
});
