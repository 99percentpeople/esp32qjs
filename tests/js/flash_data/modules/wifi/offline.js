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
    connectTypeError = String(connectTypeFailure && connectTypeFailure.message
      ? connectTypeFailure.message : connectTypeFailure);
  }
  test.ok(connectTypeError.indexOf("string SSID") >= 0,
    "wifi.connect should not coerce a non-string SSID");
  try {
    wifi.connect("");
  } catch (connectSsidRangeFailure) {
    connectSsidRangeError = String(connectSsidRangeFailure &&
      connectSsidRangeFailure.message
      ? connectSsidRangeFailure.message : connectSsidRangeFailure);
  }
  test.ok(connectSsidRangeError.indexOf("expects 1..32 bytes") >= 0,
    "wifi.connect should require a non-empty SSID");
  test.ok(typeof status.connected === "boolean", "connected should be boolean");
  test.ok(typeof status.scanning === "boolean", "scanning should be boolean");
  test.ok(typeof status.lastDisconnectReason === "number", "disconnect reason should be numeric");
  test.ok(typeof status.lastDisconnectReasonName === "string", "disconnect reason name should be string");
  aps = wifi.scan({ showHidden: true });
  test.ok(typeof aps.length === "number", "wifi.scan should return an array-like result");

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
    connectError = connectFailure && connectFailure.message ? connectFailure.message : String(connectFailure);
  }
  test.ok(connectError.indexOf("string SSID") >= 0,
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

  disconnected = wifi.disconnect();
  test.ok(disconnected && typeof disconnected === "object", "wifi.disconnect should return a status object");
  disconnectFuture = Future.call(wifi.disconnect, wifi, [100]);
  test.ok(disconnectFuture instanceof Future,
    "Future.call(wifi.disconnect) should use the native driver");
  disconnected = disconnectFuture.wait(500);
  test.equal(disconnected.connected, false,
    "native disconnect Future should settle at disconnected state");

  return { connected: status.connected, started: status.started, aps: aps.length };
});
