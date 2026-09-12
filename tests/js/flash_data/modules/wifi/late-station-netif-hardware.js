test("wifi/late-station-netif-hardware", function () {
  var cfg = test.requireConfig("wifiSsid", "wifiPassword");
  var monitor = null;
  var before;
  var connected;
  var boot = sys.status.boot.bootId;
  var stage = "stop";
  try {
    wifi.disconnect();
    wifi.stop({ timeoutMs: 5000 });
    test.equal(wifi.status().initialized, false, "ordinary Station helper is retired");
    stage = "monitor-start";
    monitor = wifi.monitor.open({
      filter: { types: ["management"] },
      capture: { snapLength: 64 },
      buffering: { poolCapacity: 2, queueCapacity: 1 }
    });
    monitor.close();
    monitor = null;
    before = wifi.status();
    test.equal(before.radio.started, true, "Monitor started the physical Radio");
    test.equal(before.initialized, false, "Radio start did not create the Station IP helper");
    stage = "late-connect";
    wifi.connect(cfg.wifiSsid, { password: cfg.wifiPassword, timeoutMs: 15000 });
    connected = wifi.status();
    test.equal(connected.connected, true, "late Station attaches and associates");
    test.equal(net.status().ready, true, "late netif receives DHCP and becomes ready");
    test.equal(connected.radio.generation, before.radio.generation,
      "late attachment does not reconstruct the shared Radio");
    test.equal(sys.status.boot.bootId, boot, "receiving traffic does not reboot");
    return { generation: connected.radio.generation, bootId: boot, ready: true };
  } catch (error) {
    error.testStage = stage;
    throw error;
  } finally {
    if (monitor !== null) monitor.close();
    wifi.disconnect();
    wifi.stop({ timeoutMs: 5000 });
  }
});
