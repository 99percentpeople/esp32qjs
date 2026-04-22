test("wifi/offline", function () {
  var status = wifi.status();
  var disconnected;

  test.ok(status && typeof status === "object", "wifi.status() should return an object");
  test.ok(typeof wifi.DEFAULT_TIMEOUT_MS === "number", "wifi timeout constant");
  test.ok(typeof wifi.connect === "function", "wifi.connect should exist");
  test.ok(typeof wifi.disconnect === "function", "wifi.disconnect should exist");
  test.ok(typeof wifi.scan === "function", "wifi.scan should exist");
  test.ok(typeof status.initialized === "boolean", "initialized should be boolean");
  test.ok(typeof status.started === "boolean", "started should be boolean");
  test.ok(typeof status.connected === "boolean", "connected should be boolean");
  test.ok(typeof status.scanning === "boolean", "scanning should be boolean");
  test.ok(typeof status.lastDisconnectReason === "number", "disconnect reason should be numeric");
  test.ok(typeof status.lastDisconnectReasonName === "string", "disconnect reason name should be string");
  disconnected = wifi.disconnect();
  test.ok(disconnected && typeof disconnected === "object", "wifi.disconnect should return a status object");

  return { connected: status.connected, started: status.started };
});
