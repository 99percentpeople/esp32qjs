__esp32qjsTest.run("wifi/offline", function () {
  var status = wifi.status();
  var disconnected;

  __esp32qjsTest.ok(status && typeof status === "object", "wifi.status() should return an object");
  __esp32qjsTest.ok(typeof wifi.DEFAULT_TIMEOUT_MS === "number", "wifi timeout constant");
  __esp32qjsTest.ok(typeof wifi.connect === "function", "wifi.connect should exist");
  __esp32qjsTest.ok(typeof wifi.disconnect === "function", "wifi.disconnect should exist");
  __esp32qjsTest.ok(typeof wifi.scan === "function", "wifi.scan should exist");
  __esp32qjsTest.ok(typeof status.initialized === "boolean", "initialized should be boolean");
  __esp32qjsTest.ok(typeof status.started === "boolean", "started should be boolean");
  __esp32qjsTest.ok(typeof status.connected === "boolean", "connected should be boolean");
  __esp32qjsTest.ok(typeof status.scanning === "boolean", "scanning should be boolean");
  __esp32qjsTest.ok(typeof status.lastDisconnectReason === "number", "disconnect reason should be numeric");
  __esp32qjsTest.ok(typeof status.lastDisconnectReasonName === "string", "disconnect reason name should be string");
  disconnected = wifi.disconnect();
  __esp32qjsTest.ok(disconnected && typeof disconnected === "object", "wifi.disconnect should return a status object");

  return { connected: status.connected, started: status.started };
});
