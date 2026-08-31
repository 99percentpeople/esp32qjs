test("core/eval", function () {
  var value = 1 + 2 + 3;
  var dateBefore = Date.now();
  var fixedDate = new Date(1234);
  var currentDate = new Date();
  var futureError = null;

  test.equal(value, 6, "basic eval");
  test.equal(fixedDate.valueOf(), 1234, "Date should preserve an explicit millisecond value");
  test.ok(currentDate.valueOf() >= dateBefore,
    "Date without arguments should use the runtime millisecond clock");
  test.equal(fs.ROOT, "/littlefs", "filesystem root");
  try {
    Future.call(Object.keys, Object, [null]).wait(1000);
  } catch (error) {
    futureError = error;
  }
  test.ok(futureError !== null,
    "Future rejection should preserve a native Error object");
  test.equal(typeof futureError.message, "string",
    "Future rejection Error prototype should survive moving GC");
  test.equal(futureError.__testSkip, undefined,
    "Future rejection Error should keep prototype lookup valid");
  gc();
  help();
  sleep(2);

  return { value: value, scriptsDir: fs.ROOT };
});
