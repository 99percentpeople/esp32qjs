test("core/eval", function () {
  var value = 1 + 2 + 3;
  var dateBefore = Date.now();
  var fixedDate = new Date(1234);
  var currentDate = new Date();

  test.equal(value, 6, "basic eval");
  test.equal(fixedDate.valueOf(), 1234, "Date should preserve an explicit millisecond value");
  test.ok(currentDate.valueOf() >= dateBefore,
    "Date without arguments should use the runtime millisecond clock");
  test.equal(fs.ROOT, "/littlefs", "filesystem root");
  gc();
  help();
  sleep(2);

  return { value: value, scriptsDir: fs.ROOT };
});
