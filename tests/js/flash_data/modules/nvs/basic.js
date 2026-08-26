test("nvs/basic", function () {
  var namespace = "qjs_test";
  var status = nvs.status();
  var oversized = "";
  var firstSet;
  var secondSet;
  var orderedGet;
  var orderedResults;
  var i;

  function expectError(callback, fragment, label) {
    var message = "";

    try {
      callback();
    } catch (error) {
      message = String(error);
    }
    test.ok(message.indexOf(fragment) >= 0, label + ": " + message);
  }

  nvs.clear(namespace);
  test.equal(nvs.getString(namespace, "missing"), null,
    "missing NVS strings should return null");
  test.equal(nvs.setString(namespace, "greeting", "hello"), 5,
    "setString should report UTF-8 bytes");
  test.equal(nvs.getString(namespace, "greeting"), "hello",
    "getString should return persisted text");
  test.equal(nvs.setString(namespace, "greeting", "你好"), 6,
    "setString should count multibyte UTF-8");
  test.equal(nvs.getString(namespace, "greeting"), "你好",
    "getString should preserve UTF-8");

  firstSet = Future.call(nvs.setString, nvs,
    [namespace, "ordered", "first"]);
  secondSet = Future.call(nvs.setString, nvs,
    [namespace, "ordered", "second"]);
  orderedGet = Future.call(nvs.getString, nvs, [namespace, "ordered"]);
  orderedResults = Future.all([firstSet, secondSet, orderedGet]).wait(2000);
  test.equal(orderedResults[2], "second",
    "NVS Futures should execute in submission order");
  nvs.erase(namespace, "ordered");
  test.ok(nvs.erase(namespace, "greeting"), "erase should remove an existing key");
  test.ok(!nvs.erase(namespace, "greeting"), "erase should report a missing key");

  nvs.setString(namespace, "one", "1");
  nvs.setString(namespace, "two", "2");
  test.ok(nvs.clear(namespace), "clear should erase an existing namespace");
  test.equal(nvs.getString(namespace, "one"), null,
    "clear should remove all namespace keys");

  for (i = 0; i < nvs.MAX_VALUE_BYTES + 1; i += 1) {
    oversized += "x";
  }
  expectError(function () {
    nvs.setString(namespace, "too_big", oversized);
  }, "at most", "oversized values should be rejected");
  expectError(function () {
    nvs.getString("bad namespace", "key");
  }, "namespace", "invalid namespaces should be rejected");

  test.ok(status.initialized, "NVS should report initialized state");
  test.ok(typeof status.encrypted === "boolean", "NVS encryption state should be explicit");
  test.equal(status.maxValueBytes, 2048, "NVS status should expose the value limit");
  test.equal(nvs.MAX_VALUE_BYTES, 2048, "NVS constant should expose the value limit");

  nvs.clear(namespace);
  return { encrypted: status.encrypted, maxValueBytes: status.maxValueBytes };
});
