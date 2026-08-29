test("ble/offline", function () {
  function assertThrowsContains(fn, expected, message) {
    var detail = "";
    try {
      fn();
    } catch (error) {
      detail = String(error && error.message ? error.message : error);
    }
    test.ok(detail.indexOf(expected) >= 0, message + ": " + detail);
  }

  var capabilities = ble.capabilities();
  var invalidOpen;
  var adapterFuture;
  var adapter;
  var status;
  var server;
  var serverStatus;
  var local;
  var value;
  var scanner;
  var scannerStats;
  var report;
  var advertiser;
  var reopened;
  var originalObjectKeys;

  test.equal(capabilities.classic, false, "BLE should exclude Bluetooth Classic");
  test.equal(capabilities.central, true, "BLE central role should be compiled");
  test.equal(capabilities.peripheral, true, "BLE peripheral role should be compiled");
  test.equal(capabilities.legacyAdvertising, true,
    "BLE v1 should expose legacy advertising");
  test.equal(capabilities.extendedAdvertising, false,
    "BLE v1 should exclude extended advertising");
  test.equal(capabilities.concurrentScanAdvertising, false,
    "BLE v1 should serialize scan and advertising");
  test.ok(capabilities.maxConnections >= 1, "BLE should expose connection capacity");
  test.ok(capabilities.maxMtu >= 23, "BLE should expose the ATT MTU limit");

  assertThrowsContains(function () { new BLEAdapter(); }, "cannot be constructed",
    "BLEAdapter construction should be native-only");
  assertThrowsContains(function () { new BLEScanner(); }, "cannot be constructed",
    "BLEScanner construction should be native-only");
  assertThrowsContains(function () { new BLEConnection(); }, "cannot be constructed",
    "BLEConnection construction should be native-only");
  assertThrowsContains(function () { new BLEGattServer(); }, "cannot be constructed",
    "BLEGattServer construction should be native-only");

  invalidOpen = Future.call(ble.open, ble, [{ unknown: true }]);
  test.equal(invalidOpen.status(), "rejected",
    "native open capture should reject unknown options synchronously");
  assertThrowsContains(function () { invalidOpen.wait(100); }, "unknown option",
    "ble.open should report unknown option names");
  originalObjectKeys = Object.keys;
  Object.keys = function () { return []; };
  try {
    invalidOpen = Future.call(ble.open, ble, [{ unknown: true }]);
    test.equal(invalidOpen.status(), "rejected",
      "native BLE option capture should ignore mutable Object.keys");
    assertThrowsContains(function () { invalidOpen.wait(100); }, "unknown option",
      "ble.open should retain strict options after Object.keys replacement");
  } finally {
    Object.keys = originalObjectKeys;
  }
  assertThrowsContains(function () { ble.open({ preferredMtu: 22 }); },
    "preferredMtu", "ble.open should reject an undersized ATT MTU");
  assertThrowsContains(function () {
    ble.open({ security: { pairingTimeoutMs: 999 } });
  }, "1000..60000", "ble.open should bound pairing timeout");
  assertThrowsContains(function () {
    ble.open({
      server: {
        services: [{
          id: "bad",
          uuid: "not-a-uuid",
          characteristics: [{
            id: "value",
            uuid: "2a19",
            properties: ["read"],
            maxLength: 1
          }]
        }]
      }
    });
  }, "uuid", "ble.open should reject malformed local GATT UUIDs");

  adapterFuture = Future.call(ble.open, ble, [{
    roles: ["central", "peripheral"],
    deviceName: "ESP32QJS-Test",
    preferredMtu: 128,
    maxConnections: 1,
    security: {
      bonding: false,
      secureConnections: true,
      mitm: false,
      ioCapability: "none",
      pairingTimeoutMs: 2000
    },
    server: {
      services: [{
        id: "battery",
        uuid: "180f",
        characteristics: [{
          id: "level",
          uuid: "2a19",
          properties: ["read", "write", "notify"],
          maxLength: 8,
          initialValue: [50],
          storeWrites: true
        }]
      }]
    }
  }]);
  test.ok(adapterFuture instanceof Future,
    "Future.call(ble.open) should return a native Future");
  adapter = adapterFuture.wait(5000);
  try {
    status = adapter.status();
    test.equal(status.open, true, "a synchronized BLE adapter should be open");
    test.equal(status.synchronized, true, "ble.open should wait for NimBLE sync");
    test.equal(status.deviceName, "ESP32QJS-Test", "deviceName should be applied");
    test.equal(status.maxConnections, 1, "maxConnections should be applied");
    test.equal(status.connections, 0, "a new adapter should have no connections");
    test.equal(status.scanning, false, "a new adapter should not be scanning");
    test.equal(status.advertising, false,
      "a new adapter should not be advertising");
    test.ok(typeof status.address === "string" && status.address.length === 17,
      "adapter status should expose a normalized address");

    server = adapter.server();
    test.ok(server !== null, "a captured GATT definition should create a server");
    serverStatus = server.status();
    test.equal(serverStatus.open, true, "local GATT server should be open");
    test.equal(serverStatus.services, 1, "local GATT service count should match");
    test.equal(serverStatus.characteristics, 1,
      "local GATT characteristic count should match");
    local = server.characteristic("level");
    test.equal(local.id, "level", "local characteristic id should be stable");
    test.equal(local.maxLength, 8, "local characteristic bound should be stable");
    value = local.value();
    try {
      test.equal(value.length, 1, "initial GATT value length should be retained");
      test.equal(value.toArray()[0], 50, "initial GATT value should be retained");
    } finally {
      value.close();
    }
    test.equal(local.setValue([51, 52]), 2,
      "local characteristic setValue should update native cache");
    test.equal(server.watch().receive(0), null,
      "an idle GATT server event queue should return null");

    assertThrowsContains(function () {
      adapter.scan({ durationMs: 0 });
    }, "durationMs", "scan should reject a zero duration");
    scanner = Future.call(adapter.scan, adapter, [{
      active: false,
      durationMs: 100,
      capacity: 2
    }]).wait(2000);
    scannerStats = scanner.stats();
    test.ok(typeof scannerStats.queued === "number",
      "scanner should expose EventQueue statistics");
    report = scanner.receive(150);
    if (report !== null) report.data.close();
    test.equal(scanner.close(), true, "scanner close should wait for GAP stop");
    assertThrowsContains(function () { scanner.status(); }, "STALE",
      "a closed scanner handle should be stale");

    assertThrowsContains(function () {
      adapter.advertise({ data: [2, 1, 6], durationMs: 0 });
    }, "durationMs", "advertise should reject a zero duration");
    advertiser = adapter.advertise({
      connectable: false,
      scannable: false,
      durationMs: 100,
      data: [2, 1, 6],
      capacity: 1
    });
    test.equal(advertiser.status().open, true,
      "advertise should return only after GAP start");
    test.equal(advertiser.close(), true,
      "advertiser close should wait for GAP stop");

    assertThrowsContains(function () {
      Future.call(adapter.connect, adapter, [{
        address: "invalid",
        type: "public"
      }, { timeoutMs: 0 }]).wait(100);
    }, "address", "connect should reject malformed peer addresses");
  } finally {
    if (adapter) Future.call(adapter.close, adapter, []).wait(12000);
  }

  reopened = ble.open({ roles: ["central"] });
  test.equal(reopened.status().open, true,
    "synchronous ble.open should match Future.call behavior");
  test.equal(reopened.close(), true,
    "synchronous adapter.close should match Future.call behavior");

  return {
    maxConnections: capabilities.maxConnections,
    maxMtu: capabilities.maxMtu
  };
});
