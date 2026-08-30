test("wifi_csi/promiscuous-hardware", function () {
  var cfg = test.requireConfig("wifiSsid", "wifiPassword");
  var caps = wifiCsi.capabilities();
  var capture = caps.configSchema === "wifi-csi-he/1"
    ? { schema: "wifi-csi-he/1", enableLegacy: true, ht20: true, heSu: true }
    : { schema: "wifi-csi-legacy/1", lltf: true, htLtf: true, scale: "auto" };
  var session = null;
  var frame = null;
  var before;

  test.equal(caps.supports.promiscuous, true,
    "the CSI hardware test Build Context should allow promiscuous capture");
  try {
    try { wifi.disconnect(); } catch (ignoredDisconnectError) {}
    wifi.connect(cfg.wifiSsid, {
      password: cfg.wifiPassword,
      timeoutMs: 15000
    });
    before = wifi.status().radio.channel;
    session = wifiCsi.open({
      source: "promiscuous",
      channel: "current",
      conflict: "fail",
      capture: capture,
      filter: { sampleEvery: 1, validOnly: true },
      queue: { capacity: 8, overflow: "drop-newest" }
    });
    wifi.scan({
      channel: session.status().effective.channel,
      showHidden: true,
      passive: false,
      dwellMs: 250,
      timeoutMs: 3000
    });
    frame = session.receive(8000);
    test.ok(frame !== null,
      "ambient traffic should produce a promiscuous CSI frame");
    test.equal(session.status().effective.source, "promiscuous",
      "effective source mode should remain explicit");
  } finally {
    if (frame !== null) frame.close();
    if (session !== null) session.close();
    try { wifi.disconnect(); } catch (ignoredFinalDisconnectError) {}
  }
  test.equal(wifi.status().radio.clients.wifiCsi, 0,
    "promiscuous close should release the radio lease");
  if (before !== null) {
    test.equal(wifi.status().radio.channel, before,
      "promiscuous capture should not move the current channel");
  }

  return { target: caps.target, source: "promiscuous" };
});
