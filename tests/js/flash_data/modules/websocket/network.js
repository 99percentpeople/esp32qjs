test("websocket/network", function () {
  var cfg = test.requireConfig("wifiSsid", "wifiPassword", "websocketUrl");
  var wifiStatus = wifi.status();
  var client;
  var event;
  var echoed = "";
  var payload = "esp32qjs-websocket-echo";

  if (!wifiStatus.connected) {
    wifiStatus = Future.call(wifi.connect, wifi,
      [cfg.wifiSsid, cfg.wifiPassword, 15000]).wait(20000);
  }
  test.ok(wifiStatus.connected,
    "Wi-Fi should be connected before WebSocket test");

  try {
    client = websocketClient.open({
      url: cfg.websocketUrl,
      autoReconnect: false,
      networkTimeoutMs: 10000,
      pingIntervalSec: 5,
      maxMessageBytes: 4096
    });
    event = client.recv(20000);
    test.equal(event.type, "open", "WebSocket client should emit open");
    test.ok(websocketClient.status().connected,
      "WebSocket client should connect");

    client.send(payload);
    event = client.recv(10000);
    if (event && event.type === "message") echoed = event.data;
    test.equal(echoed, payload,
      "WebSocket server should echo the complete text frame");
  } finally {
    if (client) client.close();
    websocketClient.close();
    wifi.disconnect();
  }

  return { echoed: echoed, url: cfg.websocketUrl };
});
