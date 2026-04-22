__esp32qjsTest.run("stream/stream", function () {
  var path = "stream-test.txt";
  var stream;
  var writeStream;
  var first;
  var second;

  writeStream = fs.open(path, "w+");
  __esp32qjsTest.equal(writeStream.kind, "file", "stream kind");
  __esp32qjsTest.equal(writeStream.mode, "w+", "stream mode");
  __esp32qjsTest.ok(writeStream.readable, "w+ stream should be readable");
  __esp32qjsTest.ok(writeStream.writable, "w+ stream should be writable");
  __esp32qjsTest.equal(writeStream.write("abcdef"), 6, "write byte count");
  writeStream.flush();
  __esp32qjsTest.equal(writeStream.tell(), 6, "tell after write");
  writeStream.seek(0, Stream.SEEK_SET);
  __esp32qjsTest.equal(writeStream.read(2), "ab", "read after write");
  writeStream.close();

  stream = fs.open(path, "r");
  __esp32qjsTest.ok(stream.readable, "read stream should be readable");
  __esp32qjsTest.ok(!stream.writable, "read stream should not be writable");
  first = stream.read(3);
  __esp32qjsTest.equal(first, "abc", "first read");
  __esp32qjsTest.equal(stream.tell(), 3, "tell after first read");

  stream.seek(1, Stream.SEEK_SET);
  second = stream.read(2);
  __esp32qjsTest.equal(second, "bc", "seek + read");

  stream.seek(0, Stream.SEEK_END);
  __esp32qjsTest.equal(stream.read(1), null, "read at eof should return null");
  __esp32qjsTest.ok(stream.eof(), "stream should report eof after reading at end");
  stream.close();
  fs.remove(path);

  return { first: first, second: second };
});
