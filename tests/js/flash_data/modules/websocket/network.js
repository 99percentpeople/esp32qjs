test("websocket/network", function () {
  var cfg = test.requireConfig("wifiSsid", "wifiPassword", "websocketUrl");
  var wifiStatus = wifi.status();
  var client;
  var event;
  var echoed = "";
  var payload = "esp32qjs-websocket-echo";

  if (!wifiStatus.connected) {
    wifiStatus = Future.call(wifi.connect, wifi,
      [cfg.wifiSsid, {
        password: cfg.wifiPassword,
        timeoutMs: 15000
      }]).wait(20000);
  }
  test.ok(wifiStatus.connected,
    "Wi-Fi should be connected before WebSocket test");

  try {
    client = websocketClient.open({
      url: cfg.websocketUrl,
      networkTimeoutMs: 10000,
      pingIntervalSec: 5,
      maxMessageBytes: 4096
    });
    event = client.receive(20000);
    test.equal(event.type, "open", "WebSocket client should emit open");
    test.ok(client.status().connected,
      "WebSocket client should connect");

    client.send(payload);
    event = client.receive(10000);
    if (event && event.type === "message") echoed = event.data;
    test.equal(echoed, payload,
      "WebSocket server should echo the complete text frame");
    test.ok(client.close(), "first WebSocket handle close should succeed");
    test.ok(!client.close(), "WebSocket handle close should be idempotent");
  } finally {
    if (client) client.close();
    wifi.disconnect();
  }

  return { echoed: echoed, url: cfg.websocketUrl };
});
