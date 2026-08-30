test("wifi_csi/espnow-conflict-hardware", function () {
  var caps = wifiCsi.capabilities();
  var capture = caps.configSchema === "wifi-csi-he/1"
    ? { schema: "wifi-csi-he/1", enableLegacy: true, ht20: true, heSu: true }
    : { schema: "wifi-csi-legacy/1", lltf: true, htLtf: true, scale: "auto" };
  var espnowSession = null;
  var csiSession = null;
  var conflictError = "";
  var during;

  test.equal(caps.supports.fixedChannel, true,
    "ESP-NOW conflict qualification requires the fixed-channel Build Context gate");
  try {
    try { wifi.disconnect(); } catch (ignoredDisconnectError) {}
    espnowSession = espNow.open({
      channel: 1,
      receiveCapacity: 2,
      sendTimeoutMs: 250
    });
    during = wifi.status();
    test.ok(during.radio.clients.espNow >= 1,
      "ESP-NOW should hold the first fixed-channel radio lease");
    try {
      csiSession = wifiCsi.open({
        source: "associated",
        channel: 6,
        conflict: "fail",
        capture: capture,
        queue: { capacity: 2, overflow: "drop-newest" }
      });
    } catch (error) {
      conflictError = String(error && error.message ? error.message : error);
    }
    test.ok(conflictError.indexOf("WIFI_CSI_RADIO_CONFLICT") >= 0,
      "CSI should reject a channel that conflicts with ESP-NOW");
    test.equal(csiSession, null,
      "a channel conflict must not publish a partial CSI session");
    test.equal(espnowSession.status().open, true,
      "a rejected CSI open must leave ESP-NOW running");
    test.equal(wifi.status().radio.channel, 1,
      "a rejected CSI open must not move the ESP-NOW channel");
    espnowSession.close();
    espnowSession = null;

    csiSession = wifiCsi.open({
      source: "associated",
      channel: 6,
      conflict: "fail",
      capture: capture,
      queue: { capacity: 2, overflow: "drop-newest" }
    });
    test.equal(csiSession.status().effective.channel, 6,
      "CSI may claim the requested channel after ESP-NOW releases it");
  } finally {
    if (csiSession !== null) csiSession.close();
    if (espnowSession !== null) espnowSession.close();
    try { wifi.disconnect(); } catch (ignoredFinalDisconnectError) {}
  }

  test.equal(wifi.status().radio.clients.wifiCsi, 0,
    "CSI conflict qualification should release its radio lease");
  test.equal(wifi.status().radio.clients.espNow, 0,
    "CSI conflict qualification should release the ESP-NOW radio lease");

  return {
    target: caps.target,
    espnowChannel: 1,
    csiChannel: 6,
    conflict: "WIFI_CSI_RADIO_CONFLICT"
  };
});
