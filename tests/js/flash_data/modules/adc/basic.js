test("adc/basic", function () {
  function pickChannel() {
    var fallback = null;
    var pin;

    for (pin = 0; pin <= 63; pin++) {
      var ref;

      if (!gpio.isValid(pin)) {
        continue;
      }
      ref = adc.ioToChannel(pin);
      if (!ref) {
        continue;
      }
      if (ref.unit === adc.UNIT_1) {
        return { pin: pin, unit: ref.unit, channel: ref.channel };
      }
      if (!fallback) {
        fallback = { pin: pin, unit: ref.unit, channel: ref.channel };
      }
    }

    return fallback;
  }

  var ref = pickChannel();
  var status;
  var channelInfo;
  var raw;
  var millivolts = null;
  var calibrated = false;

  test.equal(adc.UNIT_1, 1, "adc.UNIT_1 should be 1");
  test.equal(adc.UNIT_2, 2, "adc.UNIT_2 should be 2");
  test.equal(adc.ATTEN_DB_0, 0, "adc.ATTEN_DB_0 should be 0");
  test.equal(adc.ATTEN_DB_12, 3, "adc.ATTEN_DB_12 should be 3");
  test.equal(adc.BITWIDTH_DEFAULT, 0, "adc.BITWIDTH_DEFAULT should be 0");
  test.equal(adc.BITWIDTH_12, 12, "adc.BITWIDTH_12 should be 12");
  test.ok(typeof adc.UNIT_COUNT === "number" && adc.UNIT_COUNT >= 1, "adc.UNIT_COUNT should be numeric");
  test.ok(typeof adc.MAX_CHANNEL_COUNT === "number" && adc.MAX_CHANNEL_COUNT >= 1,
    "adc.MAX_CHANNEL_COUNT should be numeric");
  test.ok(typeof adc.open === "function", "adc.open should exist");
  test.ok(typeof adc.close === "function", "adc.close should exist");
  test.ok(typeof adc.status === "function", "adc.status should exist");
  test.ok(typeof adc.configure === "function", "adc.configure should exist");
  test.ok(typeof adc.read === "function", "adc.read should exist");
  test.ok(typeof adc.readMilliVolts === "function", "adc.readMilliVolts should exist");
  test.ok(typeof adc.ioToChannel === "function", "adc.ioToChannel should exist");
  test.ok(typeof adc.channelToIo === "function", "adc.channelToIo should exist");
  test.ok(adc.ioToChannel(-1) === null, "invalid ADC pin should map to null");

  if (!ref) {
    return { skippedHardwareCheck: true };
  }

  try {
    status = adc.open(ref.unit);
    test.ok(status.opened, "adc.open should open the selected unit");
    test.equal(status.unit, ref.unit, "adc.open should return the selected unit");
    test.equal(adc.channelToIo(ref.unit, ref.channel), ref.pin, "channelToIo should invert ioToChannel");

    status = adc.configure(ref.unit, ref.channel, {
      atten: adc.ATTEN_DB_12,
      bitwidth: adc.BITWIDTH_12,
    });
    test.ok(status.opened, "adc.configure should preserve opened state");

    channelInfo = status.channels[ref.channel];
    test.ok(channelInfo.configured, "configured channel should be marked configured");
    test.equal(channelInfo.channel, ref.channel, "channel info index should match");
    test.equal(channelInfo.pin, ref.pin, "channel info pin should match mapping");
    test.equal(channelInfo.atten, adc.ATTEN_DB_12, "channel atten should match configuration");
    test.equal(channelInfo.bitwidth, adc.BITWIDTH_12, "channel bitwidth should match configuration");

    raw = adc.read(ref.unit, ref.channel);
    test.ok(typeof raw === "number", "adc.read should return a number");
    test.ok(raw >= 0, "adc.read should return a non-negative raw value");

    try {
      millivolts = adc.readMilliVolts(ref.unit, ref.channel);
      calibrated = true;
      test.ok(typeof millivolts === "number", "adc.readMilliVolts should return a number when calibration is available");
      test.ok(millivolts >= 0, "adc.readMilliVolts should be non-negative");
    } catch (error) {
      var message = (error && error.message) || String(error);
      test.ok(message.indexOf("calibration unavailable") >= 0,
        "readMilliVolts failure should explain calibration availability");
    }

    return {
      pin: ref.pin,
      unit: ref.unit,
      channel: ref.channel,
      raw: raw,
      calibrated: calibrated,
      millivolts: millivolts,
    };
  } finally {
    try {
      adc.close(ref.unit);
    } catch (_) {}
  }
});
