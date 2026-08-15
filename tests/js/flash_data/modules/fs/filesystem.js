test("fs/filesystem", function () {
  var dirPath = "tmp-test-dir";
  var filePath = dirPath + "/tmp-test.txt";
  var renamedPath = dirPath + "/tmp-test-renamed.txt";
  var bytes;
  var appended;
  var stat;
  var entries;
  var info;

  test.equal(fs.ROOT, "/littlefs", "fs root");
  info = fs.info();
  test.equal(info.root, fs.ROOT, "fs info root");
  test.ok(info.totalBytes > 0, "fs info total bytes");
  test.ok(info.usedBytes >= 0, "fs info used bytes");
  test.equal(info.freeBytes, info.totalBytes - info.usedBytes, "fs info free bytes");
  fs.mkdir(dirPath);
  test.ok(fs.stat(dirPath).isDir, "mkdir should create a directory");

  bytes = fs.writeText(filePath, "hello");
  test.equal(bytes, 5, "writeText byte count");
  test.equal(fs.readText(filePath), "hello", "readText round-trip");
  appended = fs.appendText(filePath, "!");
  test.equal(appended, 1, "appendText byte count");
  test.equal(fs.readText(filePath), "hello!", "appendText round-trip");

  stat = fs.stat(filePath);
  test.equal(stat.name, "tmp-test.txt", "stat name");
  test.equal(stat.size, 6, "stat size");

  fs.rename(filePath, renamedPath);
  test.ok(fs.exists(renamedPath), "renamed file should exist");

  entries = fs.list(dirPath);
  test.ok(entries.length >= 1, "directory listing should not be empty");

  fs.remove(renamedPath);
  test.ok(!fs.exists(renamedPath), "removed file should not exist");
  fs.remove(dirPath);
  test.ok(!fs.exists(dirPath), "removed directory should not exist");

  return { entries: entries.length };
});
