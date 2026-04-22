test("core/eval", function () {
  var value = 1 + 2 + 3;

  test.equal(value, 6, "basic eval");
  test.equal(SCRIPTS_DIR, "/littlefs", "scripts dir");
  gc();
  help();
  delay(2);

  return { value: value, scriptsDir: SCRIPTS_DIR };
});
