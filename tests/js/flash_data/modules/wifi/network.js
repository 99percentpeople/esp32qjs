test("wifi/network", function () {
  var cfg = test.requireConfig("wifiSsid", "wifiPassword");
  var status;
  var disconnected;

  try {
    wifi.disconnect();
  } catch (_) {}

  status = Future.call(wifi.connect, wifi,
    [cfg.wifiSsid, cfg.wifiPassword, 15000]).wait(20000);

  test.ok(status.connected, "wifi should connect");
  test.ok(typeof status.ip === "string" && status.ip.length > 0, "wifi ip should be present");

  disconnected = wifi.disconnect();
  test.ok(!disconnected.connected, "wifi should disconnect");

  return { ip: status.ip };
});
