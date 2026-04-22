test("dac/basic", function () {
  var channels;
  var ref = null;
  var status;
  var channelStatus;
  var testValue;
  var pin;

  test.equal(dac.CHANNEL_0, 0, "dac.CHANNEL_0 should be 0");
  test.equal(dac.CHANNEL_1, 1, "dac.CHANNEL_1 should be 1");
  test.ok(typeof dac.CHANNEL_COUNT === "number" && dac.CHANNEL_COUNT >= 1,
    "dac.CHANNEL_COUNT should be numeric");
  test.ok(typeof dac.RESOLUTION_BITS === "number" && dac.RESOLUTION_BITS >= 1,
    "dac.RESOLUTION_BITS should be numeric");
  test.equal(dac.MAX_VALUE, Math.pow(2, dac.RESOLUTION_BITS) - 1,
    "dac.MAX_VALUE should match the advertised resolution");
  test.ok(typeof dac.open === "function", "dac.open should exist");
  test.ok(typeof dac.close === "function", "dac.close should exist");
  test.ok(typeof dac.status === "function", "dac.status should exist");
  test.ok(typeof dac.write === "function", "dac.write should exist");
  test.ok(typeof dac.ioToChannel === "function", "dac.ioToChannel should exist");
  test.ok(typeof dac.channelToIo === "function", "dac.channelToIo should exist");
  test.ok(dac.ioToChannel(-1) === null, "invalid DAC pin should map to null");

  channels = dac.status();
  test.ok(Array.isArray(channels), "dac.status() should return an array");
  test.equal(channels.length, dac.CHANNEL_COUNT, "status array length should match channel count");

  for (pin = 0; pin <= 63; pin++) {
    ref = dac.ioToChannel(pin);
    if (ref) {
      break;
    }
  }

  test.ok(!!ref, "the board should expose at least one DAC-capable pin when the module is enabled");

  status = dac.open(ref.channel);
  test.ok(status.opened, "dac.open should open the selected channel");
  test.equal(status.channel, ref.channel, "dac.open should return the selected channel");
  test.equal(status.pin, ref.pin, "dac.open should report the mapped pin");
  test.equal(dac.channelToIo(ref.channel), ref.pin, "channelToIo should invert ioToChannel");

  testValue = Math.floor(dac.MAX_VALUE / 2);
  channelStatus = dac.write(ref.channel, testValue);
  test.ok(channelStatus.opened, "dac.write should keep the channel open");
  test.equal(channelStatus.lastValue, testValue, "dac.write should update lastValue");

  channelStatus = dac.status(ref.channel);
  test.equal(channelStatus.lastValue, testValue, "dac.status(channel) should reflect the last write");

  try {
    return {
      channel: ref.channel,
      pin: ref.pin,
      resolutionBits: dac.RESOLUTION_BITS,
      value: testValue,
    };
  } finally {
    try {
      dac.close(ref.channel);
    } catch (_) {}
  }
});
