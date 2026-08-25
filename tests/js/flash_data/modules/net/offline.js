test("net/offline", function () {
  var status = net.status();
  var changes = net.watch();
  var initial;
  var argumentError = "";

  test.ok(status && typeof status === "object", "net.status should return an object");
  test.equal(typeof status.ready, "boolean", "net readiness should be boolean");
  test.equal(typeof status.truncated, "boolean", "net truncation should be boolean");
  test.ok(status.interfaces && typeof status.interfaces.length === "number",
    "net interfaces should be array-like");
  test.ok(status.primaryInterface === null || typeof status.primaryInterface === "string",
    "primary interface should be a key or null");

  initial = changes.receive(0);
  test.ok(initial && initial.type === "status", "watch should emit an initial status event");
  test.equal(typeof initial.status.ready, "boolean", "watch status should be convergent");
  test.equal(changes.receive(0), null, "watch should be empty after the initial event");

  try {
    net.status(1);
  } catch (failure) {
    argumentError = failure && failure.message ? failure.message : String(failure);
  }
  test.ok(argumentError.indexOf("expects no arguments") >= 0,
    "net.status should reject arguments");
  test.ok(changes.close(), "net watcher should close once");
  test.ok(!changes.close(), "net watcher close should be idempotent");

  return {
    ready: status.ready,
    interfaces: status.interfaces.length,
    primaryInterface: status.primaryInterface,
  };
});
