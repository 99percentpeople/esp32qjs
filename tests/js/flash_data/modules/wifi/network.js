test("wifi/network", function () {
  var cfg = test.requireConfig("wifiSsid", "wifiPassword");
  var status;
  var network;
  var primary;
  var interfaceIndex;
  var disconnected;
  var timeOptions = {
    servers: ["pool.ntp.org", "time.cloudflare.com"],
    timeoutMs: 15000
  };
  var firstClock;
  var secondClock;
  var busyCode = "";
  var immediateClock;
  var failed = false;

  try {
    wifi.disconnect();
  } catch (ignoredDisconnectError) {}

  status = Future.call(wifi.connect, wifi, [cfg.wifiSsid, {
    password: cfg.wifiPassword,
    timeoutMs: 15000,
    scanMethod: "all",
    sortMethod: "signal",
    pmf: "capable"
  }]).wait(20000);

  test.ok(status.connected, "wifi should connect");
  network = net.status();
  test.equal(network.ready, true, "net should report network readiness");
  primary = null;
  for (interfaceIndex = 0;
       interfaceIndex < network.interfaces.length;
       interfaceIndex += 1) {
    if (network.interfaces[interfaceIndex].defaultRoute) {
      primary = network.interfaces[interfaceIndex];
    }
  }
  test.ok(primary && primary.ipv4 && primary.ipv4.address.length > 0,
    "net should own the connected interface IPv4 state");

  if (!sys.time.status().synchronized) {
    firstClock = Future.call(sys.time.sync, sys.time, [timeOptions]);
    secondClock = Future.call(sys.time.sync, sys.time, [timeOptions]);
    try {
      secondClock.wait(1000);
    } catch (busyError) {
      busyCode = busyError && busyError.code ? busyError.code : "";
    }
    test.equal(busyCode, "TIME_SYNC_BUSY",
      "a concurrent time sync should fail instead of ignoring its options");
    firstClock = firstClock.wait(20000);
  } else {
    firstClock = sys.time.sync(timeOptions);
  }
  test.equal(firstClock.synchronized, true,
    "the active time operation should synchronize");
  test.ok(firstClock.unixTimeMs > 1577836800000,
    "synchronized time should be recent enough for certificate validation");
  immediateClock = sys.time.sync(timeOptions);
  test.equal(immediateClock.synchronized, true,
    "subsequent time sync should return immediately for a valid clock");
  test.equal(sys.time.status().synchronized, true,
    "time status should report a valid synchronized wall clock");
  test.ok(Math.abs(Date.now() - immediateClock.unixTimeMs) < 5000,
    "Date.now should use the synchronized wall clock");

  try {
    Future.call(wifi.connect, wifi,
      ["esp32qjs-e2e-missing", {
        password: "not-a-secret",
        timeoutMs: 1000,
        minimumRssi: -90
      }]).wait(5000);
  } catch (missingNetworkError) {
    failed = true;
  }
  test.ok(failed, "a missing network should fail or time out");

  status = Future.call(wifi.connect, wifi,
    [cfg.wifiSsid, {
      password: cfg.wifiPassword,
      timeoutMs: 15000
    }]).wait(20000);
  test.ok(status.connected, "wifi should connect again after a failed attempt");

  disconnected = wifi.disconnect();
  test.ok(!disconnected.connected, "wifi should disconnect");

  return {
    ip: primary.ipv4.address,
    unixTimeMs: immediateClock.unixTimeMs
  };
});
