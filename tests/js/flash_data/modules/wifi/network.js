test("wifi/network", function () {
  var cfg = test.requireConfig("wifiSsid", "wifiPassword");
  var status;
  var disconnected;

  try {
    wifi.disconnect();
  } catch (_) {}

  status = waitFor(function (resolve, reject) {
    wifi.connectAsync(cfg.wifiSsid, cfg.wifiPassword, 15000, function (nextStatus, error) {
      if (error) {
        reject(error);
        return;
      }
      resolve(nextStatus);
    });
  }, 20000);

  test.ok(status.connected, "wifi should connect");
  test.ok(typeof status.ip === "string" && status.ip.length > 0, "wifi ip should be present");

  disconnected = wifi.disconnect();
  test.ok(!disconnected.connected, "wifi should disconnect");

  return { ip: status.ip };
});
