test("wifi_csi/offline", function () {
  var caps = wifiCsi.capabilities();
  var capture;
  var wrongCapture;
  var options;
  var session;
  var replacement;
  var status;
  var stats;
  var errorText = "";

  test.equal(caps.apiVersion, "wifi-csi/1",
    "CSI should expose the sole v1 API identifier");
  test.ok(caps.configSchema === "wifi-csi-legacy/1" ||
    caps.configSchema === "wifi-csi-he/1",
    "CSI should expose one discriminated target schema");
  test.ok(caps.sources.length >= 1 && caps.sources[0] === "associated",
    "associated capture should always be available");
  test.ok(caps.limits.maxPoolCapacity >= 2,
    "the fixed pool capacity should be visible");
  test.ok(caps.limits.maxQueueCapacity <= caps.limits.maxPoolCapacity,
    "the queue must fit within the pool");

  if (caps.configSchema === "wifi-csi-he/1") {
    capture = {
      schema: "wifi-csi-he/1",
      enableLegacy: true,
      ht20: true,
      heSu: true,
      valueScale: 0
    };
    wrongCapture = { schema: "wifi-csi-legacy/1", lltf: true };
  } else {
    capture = {
      schema: "wifi-csi-legacy/1",
      lltf: true,
      htLtf: true,
      stbcHtLtf2: true,
      ltfMerge: true,
      adjacentSubcarrierFilter: false,
      scale: "auto"
    };
    wrongCapture = { schema: "wifi-csi-he/1", enableLegacy: true };
  }

  try {
    wifiCsi.open({ capture: wrongCapture });
  } catch (schemaError) {
    errorText = String(schemaError && schemaError.message
      ? schemaError.message : schemaError);
  }
  test.ok(errorText.indexOf("WIFI_CSI_CONFIG_SCHEMA_MISMATCH") >= 0,
    "the target adapter should reject the other schema");

  errorText = "";
  try {
    wifiCsi.open({ capture: capture, unknown: true });
  } catch (optionError) {
    errorText = String(optionError && optionError.message
      ? optionError.message : optionError);
  }
  test.ok(errorText.indexOf("unknown option") >= 0,
    "open should reject unknown options");

  options = {
    source: "associated",
    channel: "current",
    conflict: "fail",
    capture: capture,
    filter: {
      minimumRssi: -100,
      sampleEvery: 1,
      maximumRateHz: 1000,
      validOnly: true
    },
    queue: { capacity: 2, overflow: "drop-newest" },
    powerSavePolicy: "preserve"
  };
  try { wifi.disconnect(); } catch (ignoredDisconnectError) {}
  session = wifiCsi.open(options);
  try {
    status = session.status();
    test.equal(status.state, "running", "open should start capture");
    test.equal(status.effective.configSchema, caps.configSchema,
      "effective schema should match capabilities");
    test.equal(status.effective.queueCapacity, 2,
      "effective queue capacity should match the request");
    test.equal(session.receive(0), null,
      "an empty queue should return null without blocking");
    stats = session.stats();
    test.equal(stats.queue.capacity, 2,
      "CSI stats should expose EventQueue capacity");
    test.equal(session.stop().state, "stopped",
      "stop should wait for native callback quiescence");
    test.equal(session.configure(options).state, "stopped",
      "configure should preserve the stopped lifecycle");
    test.equal(session.start().state, "running",
      "start should resume a stopped session");
  } finally {
    session.close();
  }
  test.equal(session.close(), true, "close should be idempotent");
  test.equal(session.status().state, "closed",
    "closed-session status should remain observable until reopen");

  replacement = wifiCsi.open(options);
  try {
    errorText = "";
    try {
      session.status();
    } catch (staleSessionError) {
      errorText = String(staleSessionError && staleSessionError.message
        ? staleSessionError.message : staleSessionError);
    }
    test.ok(errorText.indexOf("WIFI_CSI_NOT_OPEN") >= 0,
      "reopen should invalidate an older generation handle");
  } finally {
    replacement.close();
  }

  return {
    target: caps.target,
    schema: caps.configSchema,
    maxFrameBytes: caps.limits.maxFrameBytes
  };
});
