test("espnow/wifi-radio-hardware", function () {
  var before = wifi.status();
  var session = null;
  var during;
  var after;
  var networks;

  test.equal(before.radio.clients.espNow, 0,
    "the E2E case should start without an ESP-NOW lease");
  try {
    session = espNow.open({
      channel: "current",
      receiveCapacity: 2,
      sendTimeoutMs: 250
    });
    during = wifi.status();
    test.equal(during.radio.started, true,
      "ESP-NOW should start the boot-scoped radio");
    test.ok(during.radio.clients.espNow >= 1,
      "ESP-NOW should hold a visible radio lease");
  } finally {
    if (session !== null) session.close();
  }

  after = wifi.status();
  test.equal(after.radio.clients.espNow, 0,
    "closing ESP-NOW should release its radio lease");
  test.equal(after.started, true,
    "the boot-scoped radio may remain started after lease release");

  networks = wifi.scan();
  test.ok(typeof networks.length === "number",
    "Wi-Fi scan should reuse a radio started before its event handler");
  test.equal(wifi.status().started, true,
    "Wi-Fi status should remain reconciled after scan");

  if (after.radio.maxTxPowerDbm !== null) {
    test.equal(wifi.setTxPower(after.radio.maxTxPowerDbm),
      after.radio.maxTxPowerDbm,
      "setting the current TX power should report the mapped actual value");
  }

  return {
    accessPoints: networks.length,
    mode: wifi.status().radio.mode
  };
});
