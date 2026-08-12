test("core/eval", function () {
  var value = 1 + 2 + 3;

  test.equal(value, 6, "basic eval");
  test.equal(fs.ROOT, "/littlefs", "filesystem root");
  gc();
  help();
  delay(2);

  return { value: value, scriptsDir: fs.ROOT };
});
