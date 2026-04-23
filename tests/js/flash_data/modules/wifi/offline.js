test("wifi/offline", function () {
  var aps;
  var status = wifi.status();
  var disconnected;
  var scanError = "";
  var connectError = "";

  test.ok(status && typeof status === "object", "wifi.status() should return an object");
  test.ok(typeof wifi.DEFAULT_TIMEOUT_MS === "number", "wifi timeout constant");
  test.ok(typeof wifi.connect === "function", "wifi.connect should exist");
  test.ok(typeof wifi.connectAsync === "function", "wifi.connectAsync should exist");
  test.ok(typeof wifi.disconnect === "function", "wifi.disconnect should exist");
  test.ok(typeof wifi.scan === "function", "wifi.scan should exist");
  test.ok(typeof wifi.scanAsync === "function", "wifi.scanAsync should exist");
  test.ok(typeof wifi.async === "undefined", "wifi.async should not exist");
  test.ok(typeof status.initialized === "boolean", "initialized should be boolean");
  test.ok(typeof status.started === "boolean", "started should be boolean");
  test.ok(typeof status.connected === "boolean", "connected should be boolean");
  test.ok(typeof status.scanning === "boolean", "scanning should be boolean");
  test.ok(typeof status.lastDisconnectReason === "number", "disconnect reason should be numeric");
  test.ok(typeof status.lastDisconnectReasonName === "string", "disconnect reason name should be string");
  aps = wifi.scan();
  test.ok(typeof aps.length === "number", "wifi.scan should return an array-like result");

  try {
    wifi.scan(function () {});
  } catch (scanFailure) {
    scanError = scanFailure && scanFailure.message ? scanFailure.message : String(scanFailure);
  }
  test.ok(scanError.indexOf("wifi.scanAsync") >= 0, "wifi.scan(callback) should direct callers to wifi.scanAsync");

  try {
    wifi.connect("ssid", "password", function () {});
  } catch (connectFailure) {
    connectError = connectFailure && connectFailure.message ? connectFailure.message : String(connectFailure);
  }
  test.ok(connectError.indexOf("wifi.connectAsync") >= 0, "wifi.connect(callback) should direct callers to wifi.connectAsync");

  disconnected = wifi.disconnect();
  test.ok(disconnected && typeof disconnected === "object", "wifi.disconnect should return a status object");

  return { connected: status.connected, started: status.started, aps: aps.length };
});
