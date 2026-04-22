__esp32qjsTest.run("wifi/network", function () {
  var cfg = __esp32qjsTest.requireConfig("wifiSsid", "wifiPassword");
  var status;
  var disconnected;

  try {
    wifi.disconnect();
  } catch (_) {}

  status = waitFor(function (resolve, reject) {
    wifi.connect(cfg.wifiSsid, cfg.wifiPassword, 15000, function (nextStatus, error) {
      if (error) {
        reject(error);
        return;
      }
      resolve(nextStatus);
    });
  }, 20000);

  __esp32qjsTest.ok(status.connected, "wifi should connect");
  __esp32qjsTest.ok(typeof status.ip === "string" && status.ip.length > 0, "wifi ip should be present");

  disconnected = wifi.disconnect();
  __esp32qjsTest.ok(!disconnected.connected, "wifi should disconnect");

  return { ip: status.ip };
});
