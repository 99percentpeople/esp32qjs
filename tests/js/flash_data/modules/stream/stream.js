test("stream/stream", function () {
  var path = "stream-test.txt";
  var stream;
  var writeStream;
  var first;
  var second;
  var byteChunk;
  var byteTail;
  var bytes;

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
  test.equal(typeof first, "string", "text read should return a string");
  test.equal(first, "abc", "first read");
  test.equal(stream.tell(), 3, "tell after first read");

  stream.seek(1, Stream.SEEK_SET);
  second = stream.read(2);
  test.equal(second, "bc", "seek + read");

  stream.seek(0, Stream.SEEK_END);
  test.equal(stream.read(1), null, "read at eof should return null");
  test.ok(stream.eof(), "stream should report eof after reading at end");
  stream.close();

  stream = fs.open(path, "rb");
  test.equal(stream.mode, "rb", "binary stream mode");
  byteChunk = stream.read(4);
  test.equal(typeof byteChunk, "object", "binary read should return an object");
  test.ok(byteChunk, "binary read should return a ByteView before EOF");
  test.equal(byteChunk.length, 4, "binary read length");
  test.equal(byteChunk.byteLength, 4, "binary read byteLength");
  bytes = byteChunk.toArray();
  test.equal(bytes.length, 4, "binary read array length");
  test.equal(bytes[0], 0x61, "binary read first byte");
  test.equal(bytes[1], 0x62, "binary read second byte");
  test.equal(bytes[2], 0x63, "binary read third byte");
  test.equal(bytes[3], 0x64, "binary read fourth byte");
  byteTail = stream.read(8).toArray();
  test.equal(byteTail.length, 2, "binary read should return short tail chunk");
  test.equal(byteTail[0], 0x65, "binary read tail first byte");
  test.equal(byteTail[1], 0x66, "binary read tail second byte");
  test.equal(stream.read(1), null, "binary read at eof should return null");
  stream.close();
  fs.remove(path);

  return { first: first, second: second };
});
