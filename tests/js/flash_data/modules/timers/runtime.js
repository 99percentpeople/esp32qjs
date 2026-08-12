test("timers/runtime", function () {
  var invoked = false;
  var queued = Future.call(function (value) {
    invoked = true;
    return value + 1;
  }, null, [41]);
  var left;
  var right;
  var combined;
  var raced;
  var rejected;
  var cancelled;
  var timeoutInput;
  var timed;
  var rejectedCaught = false;
  var timeoutCaught = false;
  var fireAndForgetRan = false;
  var intervalTicks = 0;
  var intervalId;

  test.ok(typeof Future === "function", "Future factory should exist");
  test.ok(typeof EventQueue === "function", "EventQueue class should exist");
  test.ok(typeof defer === "undefined", "legacy defer should be removed");
  test.ok(typeof waitFor === "undefined", "legacy waitFor should be removed");
  test.equal(queued.status(), "queued", "Future.call should defer invocation");
  test.ok(!invoked, "Future.call must not invoke JavaScript before returning");
  test.equal(queued.wait(1000), 42, "queued JavaScript future result");
  test.ok(invoked, "wait should pump queued work");
  queued = null;
  gc();

  Future.call(function () {
    fireAndForgetRan = true;
  });
  delay(20);
  test.ok(fireAndForgetRan,
    "unretained Future should start and settle at a later scheduler safe point");
  gc();

  left = Future.sleep(20);
  right = Future.call(function () { return "right"; });
  combined = Future.all([left, right]);
  test.equal(combined.wait(1000)[1], "right", "Future.all should preserve input order");
  left = null;
  right = null;
  combined = null;
  gc();

  raced = Future.race([Future.sleep(10), Future.sleep(80)]);
  test.equal(raced.wait(1000).index, 0, "Future.race should report the first input");
  raced = null;
  gc();

  rejected = Future.call(function () { throw "future-fail"; });
  try {
    rejected.wait(1000);
  } catch (rejectionError) {
    rejectedCaught = String(rejectionError).indexOf("future-fail") >= 0;
  }
  test.ok(rejectedCaught, "Future rejection should surface from wait");
  rejected = null;
  gc();

  cancelled = Future.sleep(1000);
  test.ok(cancelled.cancel(), "pending Future should be cancellable");
  test.equal(cancelled.status(), "cancelled", "cancel should update status");
  cancelled = null;
  gc();

  timeoutInput = Future.sleep(500);
  timed = Future.timeout(timeoutInput, 10);
  try {
    timed.wait(1000);
  } catch (timeoutError) {
    timeoutCaught = String(timeoutError).indexOf("expired") >= 0;
  }
  test.ok(timeoutCaught, "Future.timeout should reject and cancel its input");
  timeoutInput = null;
  timed = null;
  gc();

  intervalId = setInterval(function () {
    intervalTicks++;
    if (intervalTicks >= 3) clearInterval(intervalId);
  }, 10);
  Future.sleep(80).wait(500);
  test.equal(intervalTicks, 3, "Future wait should keep timer callbacks progressing");

  test.equal(delay(5100), 5100, "long native delay should cooperate with the task watchdog");

  return { invoked: invoked, intervalTicks: intervalTicks };
});
