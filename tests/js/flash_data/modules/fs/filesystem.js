test("fs/filesystem", function () {
  var dirPath = "tmp-test-dir";
  var filePath = dirPath + "/tmp-test.txt";
  var renamedPath = dirPath + "/tmp-test-renamed.txt";
  var bytes;
  var appended;
  var stat;
  var entries;
  var info;
  var changes;
  var mirrorChanges;
  var pendingChange;
  var change;

  test.equal(fs.ROOT, "/littlefs", "fs root");
  info = fs.info();
  test.equal(info.root, fs.ROOT, "fs info root");
  test.ok(info.totalBytes > 0, "fs info total bytes");
  test.ok(info.usedBytes >= 0, "fs info used bytes");
  test.equal(info.freeBytes, info.totalBytes - info.usedBytes, "fs info free bytes");
  changes = fs.watch();
  mirrorChanges = fs.watch();
  pendingChange = Future.call(changes.receive, changes, [1000]);
  fs.mkdir(dirPath);
  change = pendingChange.wait(1000);
  test.equal(change.type, "mkdir", "watch mkdir type");
  test.equal(change.path, dirPath, "watch mkdir path");
  test.equal(mirrorChanges.receive(0).path, dirPath, "independent watcher path");
  test.ok(mirrorChanges.close(), "independent watcher should close");
  test.ok(fs.stat(dirPath).isDir, "mkdir should create a directory");

  bytes = fs.writeText(filePath, "hello");
  test.equal(bytes, 5, "writeText byte count");
  change = changes.receive(0);
  test.equal(change.type, "write", "watch write type");
  test.equal(change.path, filePath, "watch write path");
  test.equal(fs.readText(filePath), "hello", "readText round-trip");
  appended = fs.appendText(filePath, "!");
  test.equal(appended, 1, "appendText byte count");
  test.equal(changes.receive(0).type, "write", "watch append type");
  test.equal(fs.readText(filePath), "hello!", "appendText round-trip");

  stat = fs.stat(filePath);
  test.equal(stat.name, "tmp-test.txt", "stat name");
  test.equal(stat.size, 6, "stat size");

  fs.rename(filePath, renamedPath);
  change = changes.receive(0);
  test.equal(change.type, "rename", "watch rename type");
  test.equal(change.path, filePath, "watch rename source");
  test.equal(change.toPath, renamedPath, "watch rename target");
  test.ok(fs.exists(renamedPath), "renamed file should exist");

  entries = fs.list(dirPath);
  test.ok(entries.length >= 1, "directory listing should not be empty");

  fs.remove(renamedPath);
  test.equal(changes.receive(0).type, "remove", "watch file removal");
  test.ok(!fs.exists(renamedPath), "removed file should not exist");
  fs.remove(dirPath);
  test.equal(changes.receive(0).type, "remove", "watch directory removal");
  test.ok(!fs.exists(dirPath), "removed directory should not exist");
  test.ok(changes.close(), "watch queue should close");

  return { entries: entries.length };
});
