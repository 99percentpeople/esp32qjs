test("wifi/offline", function () {
  var aps;
  var status = wifi.status();
  var disconnected;
  var disconnectFuture;
  var scanError = "";
  var scanUnknownOptionError = "";
  var connectError = "";
  var connectUnknownOptionError = "";
  var connectTypeError = "";
  var connectSsidRangeError = "";
  var syncOptionsError = "";
  var syncServersError = "";
  var invalidSync;
  var syncZeroTimeoutError = "";
  var syncLargeTimeoutError = "";
  var syncFractionalTimeoutError = "";
  var txPowerRangeError = "";
  var powerSaveError = "";
  var originalObjectKeys;
  var intrinsicOptionError = "";

  test.ok(status && typeof status === "object", "wifi.status() should return an object");
  test.ok(typeof wifi.DEFAULT_TIMEOUT_MS === "number", "wifi timeout constant");
  var rawTxCaps = wifi.rawTx.capabilities();
  var rawTxTypes = ["beacon", "probe-request", "probe-response", "action", "data"];
  test.equal(rawTxCaps.supports.extendedManagement, true, "reviewed management extension is enabled");
  rawTxTypes = rawTxTypes.concat(["association-request","association-response","reassociation-request","reassociation-response","timing-advertisement","atim","disassociation","authentication","deauthentication","action-no-ack"]);
  test.ok(Array.isArray(rawTxCaps.frameTypes), "Raw TX frame types are an array");
  test.equal(rawTxCaps.frameTypes.length, rawTxTypes.length, "Raw TX allowlist size");
  for (var rawTxTypeIndex = 0; rawTxTypeIndex < rawTxTypes.length; rawTxTypeIndex++) {
    test.ok(rawTxCaps.frameTypes.some(function (frame) { return frame.name === rawTxTypes[rawTxTypeIndex]; }),
      "Raw TX type uses the send result identifier: " + rawTxTypes[rawTxTypeIndex]);
  }
  rawTxCaps.frameTypes.length = 0;
  test.equal(wifi.rawTx.capabilities().frameTypes.length, rawTxTypes.length,
    "mutating a capability snapshot does not change the native allowlist");
  test.ok(typeof wifi.connect === "function", "wifi.connect should exist");
  test.ok(typeof wifi.connectAsync === "undefined", "wifi.connectAsync should not exist");
  test.ok(typeof wifi.disconnect === "function", "wifi.disconnect should exist");
  test.ok(typeof wifi.scan === "function", "wifi.scan should exist");
  test.ok(typeof wifi.setPowerSave === "function",
    "wifi.setPowerSave should exist");
  test.ok(typeof wifi.setTxPower === "function", "wifi.setTxPower should exist");
  test.ok(typeof wifi.syncTime === "undefined", "wifi.syncTime should be removed");
  test.ok(typeof sys.time.sync === "function", "sys.time.sync should exist");
  test.ok(typeof wifi.scanAsync === "undefined", "wifi.scanAsync should not exist");
  test.ok(typeof wifi.async === "undefined", "wifi.async should be removed");
  test.ok(typeof status.initialized === "boolean", "initialized should be boolean");
  test.ok(typeof status.started === "boolean", "started should be boolean");
  test.ok(status.radio && typeof status.radio === "object",
    "status should include the shared radio snapshot");
  test.equal(status.started, status.radio.started,
    "top-level started should reflect the boot-scoped radio");
  test.ok(typeof status.radio.mode === "string", "radio mode should be observable");
  test.ok(status.radio.channel === null || typeof status.radio.channel === "number",
    "radio channel should be null or numeric");
  test.ok(status.radio.clients && typeof status.radio.clients.total === "number",
    "radio client leases should be observable");
  test.ok(status.radio.maxTxPowerDbm === null ||
    typeof status.radio.maxTxPowerDbm === "number",
    "actual maximum TX power should be observable when the radio is started");
  test.ok(status.radio.powerSave === null ||
    status.radio.powerSave === "none" ||
    status.radio.powerSave === "minimum" ||
    status.radio.powerSave === "maximum",
    "power-save status should be null or a normalized control value");
  test.equal(typeof status.hostname, "undefined",
    "network hostname should belong to net instead of wifi");
  test.equal(typeof status.ip, "undefined",
    "IP state should belong to net instead of wifi");
  try {
    wifi.setTxPower(1);
  } catch (txPowerFailure) {
    txPowerRangeError = String(txPowerFailure && txPowerFailure.message
      ? txPowerFailure.message : txPowerFailure);
  }
  test.ok(txPowerRangeError.indexOf("2..20") >= 0,
    "TX power should reject values outside the supported dBm range");
  try {
    wifi.setPowerSave("invalid");
  } catch (powerSaveFailure) {
    powerSaveError = String(powerSaveFailure && powerSaveFailure.message
      ? powerSaveFailure.message : powerSaveFailure);
  }
  test.ok(powerSaveError.indexOf("none, minimum, or maximum") >= 0,
    "power-save mode should be strictly validated");
  try {
    wifi.scan({ callback: function () {} });
  } catch (scanUnknownOptionFailure) {
    scanUnknownOptionError = String(scanUnknownOptionFailure &&
      scanUnknownOptionFailure.message
      ? scanUnknownOptionFailure.message : scanUnknownOptionFailure);
  }
  test.ok(scanUnknownOptionError.indexOf("unknown option 'callback'") >= 0,
    "wifi.scan should reject unknown control fields");
  try {
    wifi.connect("ssid", { automatic: true });
  } catch (connectUnknownOptionFailure) {
    connectUnknownOptionError = String(connectUnknownOptionFailure &&
      connectUnknownOptionFailure.message
      ? connectUnknownOptionFailure.message : connectUnknownOptionFailure);
  }
  test.ok(connectUnknownOptionError.indexOf("unknown option 'automatic'") >= 0,
    "wifi.connect should reject unknown control fields");
  originalObjectKeys = Object.keys;
  Object.keys = function () { return []; };
  try {
    wifi.scan({ callback: function () {} });
  } catch (intrinsicOptionFailure) {
    intrinsicOptionError = String(intrinsicOptionFailure &&
      intrinsicOptionFailure.message
      ? intrinsicOptionFailure.message : intrinsicOptionFailure);
  } finally {
    Object.keys = originalObjectKeys;
  }
  test.ok(intrinsicOptionError.indexOf("unknown option 'callback'") >= 0,
    "wifi options should ignore mutable Object.keys");
  try {
    wifi.connect(1234);
  } catch (connectTypeFailure) {
    test.ok(connectTypeFailure instanceof TypeError,
      "a numeric SSID should raise TypeError");
    connectTypeError = String(connectTypeFailure && connectTypeFailure.message
      ? connectTypeFailure.message : connectTypeFailure);
  }
  test.ok(connectTypeError.indexOf("SSID") >= 0,
    "wifi.connect should not coerce a numeric SSID");
  test.equal(wifi.status().radio.generation, status.radio.generation,
    "invalid connect input should preserve Radio identity");
  test.equal(wifi.status().radio.clients.total, status.radio.clients.total,
    "invalid connect input should not acquire a Radio owner");
  test.equal(wifi.status().started, status.started,
    "invalid connect input should not start Wi-Fi");
  try {
    wifi.connect("");
  } catch (connectSsidRangeFailure) {
    connectSsidRangeError = String(connectSsidRangeFailure &&
      connectSsidRangeFailure.message
      ? connectSsidRangeFailure.message : connectSsidRangeFailure);
  }
  test.ok(connectSsidRangeError.indexOf("1..32") >= 0,
    "wifi.connect should require a non-empty SSID");
  test.ok(typeof status.connected === "boolean", "connected should be boolean");
  test.ok(typeof status.scanning === "boolean", "scanning should be boolean");
  test.ok(typeof status.lastDisconnectReason === "number", "disconnect reason should be numeric");
  test.ok(typeof status.lastDisconnectReasonName === "string", "disconnect reason name should be string");
  aps = wifi.scan({ showHidden: true });
  test.ok(Array.isArray(aps.records), "wifi.scan returns a records array");
  test.ok(typeof aps.complete === "boolean" && typeof aps.timedOut === "boolean",
    "wifi.scan reports completion metadata");
  test.ok(aps.complete !== aps.timedOut, "scan completes normally or at its deadline");

  try {
    wifi.scan(function () {});
  } catch (scanFailure) {
    scanError = scanFailure && scanFailure.message ? scanFailure.message : String(scanFailure);
  }
  test.ok(scanError.indexOf("options object") >= 0,
    "wifi.scan should reject callback overloads");

  try {
    wifi.connect({ ssid: "ssid" });
  } catch (connectFailure) {
    test.ok(connectFailure instanceof TypeError,
      "an options object in place of SSID should raise TypeError");
    connectError = connectFailure && connectFailure.message ? connectFailure.message : String(connectFailure);
  }
  test.ok(connectError.indexOf("SSID") >= 0,
    "wifi.connect should reject the superseded options-only form");

  try {
    sys.time.sync();
  } catch (syncOptionsFailure) {
    syncOptionsError = String(syncOptionsFailure && syncOptionsFailure.message
      ? syncOptionsFailure.message : syncOptionsFailure);
  }
  test.ok(syncOptionsError.indexOf("options object") >= 0,
    "sys.time.sync should require an options object");

  invalidSync = Future.call(sys.time.sync, sys.time, [{ servers: [] }]);
  test.equal(invalidSync.status(), "rejected",
    "native Future arguments should be captured before Future.call returns");
  try {
    invalidSync.wait(1000);
  } catch (syncServersFailure) {
    syncServersError = String(syncServersFailure && syncServersFailure.message
      ? syncServersFailure.message : syncServersFailure);
  }
  test.ok(syncServersError.indexOf("1..") >= 0,
    "sys.time.sync should require at least one server");

  try {
    Future.call(sys.time.sync, sys.time, [{
      servers: ["pool.ntp.org"],
      timeoutMs: 0
    }]).wait(1000);
  } catch (syncZeroTimeoutFailure) {
    syncZeroTimeoutError = String(syncZeroTimeoutFailure && syncZeroTimeoutFailure.message
      ? syncZeroTimeoutFailure.message : syncZeroTimeoutFailure);
  }
  test.ok(syncZeroTimeoutError.indexOf("between 1 and 60000") >= 0,
    "sys.time.sync should reject timeoutMs=0");

  try {
    Future.call(sys.time.sync, sys.time, [{
      servers: ["pool.ntp.org"],
      timeoutMs: 60001
    }]).wait(1000);
  } catch (syncLargeTimeoutFailure) {
    syncLargeTimeoutError = String(syncLargeTimeoutFailure && syncLargeTimeoutFailure.message
      ? syncLargeTimeoutFailure.message : syncLargeTimeoutFailure);
  }
  test.ok(syncLargeTimeoutError.indexOf("between 1 and 60000") >= 0,
    "sys.time.sync should reject timeoutMs above 60000");

  try {
    Future.call(sys.time.sync, sys.time, [{
      servers: ["pool.ntp.org"],
      timeoutMs: 1.5
    }]).wait(1000);
  } catch (syncFractionalTimeoutFailure) {
    syncFractionalTimeoutError = String(syncFractionalTimeoutFailure &&
      syncFractionalTimeoutFailure.message
      ? syncFractionalTimeoutFailure.message : syncFractionalTimeoutFailure);
  }
  test.ok(syncFractionalTimeoutError.indexOf("between 1 and 60000") >= 0,
    "sys.time.sync should reject fractional timeoutMs");

  disconnected = wifi.disconnect({ timeoutMs: 1000 });
  test.ok(disconnected && typeof disconnected === "object", "wifi.disconnect should return a status object");
  disconnectFuture = Future.call(wifi.disconnect, wifi, [{ timeoutMs: 100 }]);
  test.ok(disconnectFuture instanceof Future,
    "Future.call(wifi.disconnect) should use the native driver");
  disconnected = disconnectFuture.wait(500);
  test.equal(disconnected.connected, false,
    "native disconnect Future should settle at disconnected state");

  return { connected: status.connected, started: status.started, aps: aps.length };
});
