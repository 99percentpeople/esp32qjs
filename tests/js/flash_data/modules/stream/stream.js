test("stream/stream", function () {
  var path = "stream-test.txt";
  var stream;
  var writeStream;
  var first;
  var second;

  writeStream = fs.open(path, "w+");
  test.equal(writeStream.kind, "file", "stream kind");
  test.equal(writeStream.mode, "w+", "stream mode");
  test.ok(writeStream.readable, "w+ stream should be readable");
  test.ok(writeStream.writable, "w+ stream should be writable");
  test.equal(writeStream.write("abcdef"), 6, "write byte count");
  writeStream.flush();
  test.equal(writeStream.tell(), 6, "tell after write");
  writeStream.seek(0, Stream.SEEK_SET);
  test.equal(writeStream.read(2), "ab", "read after write");
  writeStream.close();

  stream = fs.open(path, "r");
  test.ok(stream.readable, "read stream should be readable");
  test.ok(!stream.writable, "read stream should not be writable");
  first = stream.read(3);
  test.equal(first, "abc", "first read");
  test.equal(stream.tell(), 3, "tell after first read");

  stream.seek(1, Stream.SEEK_SET);
  second = stream.read(2);
  test.equal(second, "bc", "seek + read");

  stream.seek(0, Stream.SEEK_END);
  test.equal(stream.read(1), null, "read at eof should return null");
  test.ok(stream.eof(), "stream should report eof after reading at end");
  stream.close();
  fs.remove(path);

  return { first: first, second: second };
});
