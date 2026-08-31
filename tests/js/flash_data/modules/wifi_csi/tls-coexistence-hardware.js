test("wifi_csi/tls-coexistence-hardware", function () {
  var cfg = test.requireConfig("wifiSsid", "wifiPassword");
  var caps = wifiCsi.capabilities();
  var capture = caps.configSchema === "wifi-csi-he/1"
    ? { schema: "wifi-csi-he/1", enableLegacy: true, ht20: true, heSu: true }
    : { schema: "wifi-csi-legacy/1", lltf: true, htLtf: true, scale: "auto" };
  var session = null;
  var batch = null;
  var client = null;
  var before;
  var after;
  var stats;
  var wifiStatus;
  var frames = 0;
  var tlsConnections = 0;
  var i;
  var batchFrames = caps.limits.maxBatchFrames < 16
    ? caps.limits.maxBatchFrames : 16;

  wifiStatus = wifi.status();
  if (!wifiStatus.connected) {
    wifiStatus = Future.call(wifi.connect, wifi, [cfg.wifiSsid, {
      password: cfg.wifiPassword,
      timeoutMs: 15000
    }]).wait(20000);
  }
  test.ok(wifiStatus.connected,
    "Wi-Fi must be associated before CSI and TLS coexistence");
  test.equal(sys.time.sync({
    servers: ["pool.ntp.org", "time.cloudflare.com"],
    timeoutMs: 15000
  }).synchronized, true, "TLS qualification requires synchronized time");

  gc();
  before = sys.status.memory;
  try {
    session = wifiCsi.open({
      source: "associated",
      channel: "current",
      conflict: "fail",
      capture: capture,
      queue: { capacity: 16, overflow: "drop-newest" }
    });
    for (i = 0; i < 5; i += 1) {
      client = socket.openTCP({ tls: true });
      try {
        test.ok(client.connect("example.com", 443, { timeoutMs: 15000 }),
          "TLS should connect while associated CSI remains active");
        tlsConnections += 1;
      } finally {
        client.close();
        client = null;
      }
      batch = session.receiveBatch({ maximumFrames: batchFrames, timeoutMs: 2000 });
      if (batch !== null) {
        frames += batch.frameCount;
        batch.close();
        batch = null;
      }
    }
    stats = session.stats();
    test.equal(tlsConnections, 5,
      "every TLS connection should complete while CSI is enabled");
    test.ok(stats.callbacks > 0 && frames > 0,
      "TLS traffic should produce associated CSI callbacks and frames");
  } finally {
    if (client !== null) client.close();
    if (batch !== null) batch.close();
    if (session !== null) session.close();
    try { wifi.disconnect(); } catch (ignoredFinalDisconnectError) {}
  }

  gc();
  after = sys.status.memory;
  test.equal(wifi.status().radio.clients.wifiCsi, 0,
    "TLS coexistence close should release the CSI radio lease");
  test.ok(after.internal.largestFreeBlockBytes + 131072 >=
    before.internal.largestFreeBlockBytes,
    "TLS and CSI close should preserve a usable internal heap block");
  if (before.psram !== null && after.psram !== null) {
    test.ok(after.psram.largestFreeBlockBytes + 262144 >=
      before.psram.largestFreeBlockBytes,
      "TLS and CSI close should return PSRAM near its baseline");
  }

  return {
    target: caps.target,
    tlsConnections: tlsConnections,
    csiFrames: frames,
    callbacks: stats.callbacks,
    accepted: stats.accepted,
    droppedPoolFull: stats.droppedPoolFull,
    droppedQueueFull: stats.droppedQueueFull,
    internalLargestDelta: after.internal.largestFreeBlockBytes -
      before.internal.largestFreeBlockBytes,
    dmaLargestDelta: after.dma.largestFreeBlockBytes -
      before.dma.largestFreeBlockBytes,
    psramLargestDelta: before.psram === null || after.psram === null
      ? null
      : after.psram.largestFreeBlockBytes - before.psram.largestFreeBlockBytes
  };
});
