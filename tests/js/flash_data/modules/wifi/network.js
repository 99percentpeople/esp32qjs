test("wifi/network", function () {
  var cfg = test.requireConfig("wifiSsid", "wifiPassword");
  var status;
  var disconnected;
  var failed = false;

  try {
    wifi.disconnect();
  } catch (ignoredDisconnectError) {}

  status = Future.call(wifi.connect, wifi,
    [cfg.wifiSsid, cfg.wifiPassword, 15000]).wait(20000);

  test.ok(status.connected, "wifi should connect");
  test.ok(typeof status.ip === "string" && status.ip.length > 0, "wifi ip should be present");

  try {
    Future.call(wifi.connect, wifi,
      ["esp32qjs-e2e-missing", "not-a-secret", 1000]).wait(5000);
  } catch (missingNetworkError) {
    failed = true;
  }
  test.ok(failed, "a missing network should fail or time out");

  status = Future.call(wifi.connect, wifi,
    [cfg.wifiSsid, cfg.wifiPassword, 15000]).wait(20000);
  test.ok(status.connected, "wifi should connect again after a failed attempt");

  disconnected = wifi.disconnect();
  test.ok(!disconnected.connected, "wifi should disconnect");

  return { ip: status.ip };
});
