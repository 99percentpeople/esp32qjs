test("websocket/network", function () {
  var cfg = test.requireConfig("wifiSsid", "wifiPassword", "websocketUrl");
  var wifiStatus = wifi.status();
  var phaseResolve = null;
  var phaseReject = null;
  var opened = false;
  var echoed = "";
  var payload = "esp32qjs-websocket-echo";

  if (!wifiStatus.connected) {
    wifiStatus = waitFor(function (resolve, reject) {
      wifi.async.connect(cfg.wifiSsid, cfg.wifiPassword, 15000,
        function (nextStatus, error) {
          if (error) {
            reject(error);
            return;
          }
          resolve(nextStatus);
        });
    }, 20000);
  }
  test.ok(wifiStatus.connected,
    "Wi-Fi should be connected before WebSocket test");

  try {
    opened = waitFor(function (resolve, reject) {
      phaseResolve = resolve;
      phaseReject = reject;
      websocketClient.open({
        url: cfg.websocketUrl,
        autoReconnect: false,
        networkTimeoutMs: 10000,
        pingIntervalSec: 5,
        maxMessageBytes: 4096
      }, function (event) {
        var callback;

        if (event.type === "open" && phaseResolve) {
          callback = phaseResolve;
          phaseResolve = null;
          phaseReject = null;
          callback(true);
        } else if (event.type === "message" && phaseResolve) {
          callback = phaseResolve;
          phaseResolve = null;
          phaseReject = null;
          callback(event.data);
        } else if ((event.type === "error" || event.type === "close") && phaseReject) {
          callback = phaseReject;
          phaseResolve = null;
          phaseReject = null;
          callback(event.message || event.type);
        }
      });
    }, 20000);
    test.ok(opened && websocketClient.status().connected,
      "WebSocket client should connect");

    echoed = waitFor(function (resolve, reject) {
      phaseResolve = resolve;
      phaseReject = reject;
      websocketClient.send(payload);
    }, 10000);
    test.equal(echoed, payload,
      "WebSocket server should echo the complete text frame");
  } finally {
    phaseResolve = null;
    phaseReject = null;
    websocketClient.close();
    wifi.disconnect();
  }

  return { echoed: echoed, url: cfg.websocketUrl };
});
