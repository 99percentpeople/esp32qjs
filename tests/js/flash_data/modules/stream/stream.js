test("stream/stream", function () {
  var path = "stream-test.txt";
  var stream;
  var writeStream;
  var first;
  var second;
  var byteChunk;
  var byteTailView;
  var byteTail;
  var bytes;
  var binaryPath = "stream-binary-test.bin";
  var binaryStream;
  var queuedWriteA;
  var queuedWriteB;
  var queuedSeek;
  var queuedRead;
  var queuedResults;
  var spanPath = "stream-span-source.bin";
  var spanBitmap;
  var spanSource;
  var spanStream;
  var spanWrite;
  var streamChanges;
  var streamChange;

  streamChanges = fs.watch();
  writeStream = fs.open(path, "w+");
  test.equal(writeStream.kind, "file", "stream kind");
  test.equal(writeStream.mode, "w+", "stream mode");
  test.ok(writeStream.readable, "w+ stream should be readable");
  test.ok(writeStream.writable, "w+ stream should be writable");
  queuedWriteA = Future.call(writeStream.write, writeStream, ["abc"]);
  queuedWriteB = Future.call(writeStream.write, writeStream, ["def"]);
  queuedSeek = Future.call(writeStream.seek, writeStream,
    [0, Stream.SEEK_SET]);
  queuedRead = Future.call(writeStream.read, writeStream, [6]);
  queuedResults = Future.all([
    queuedWriteA,
    queuedWriteB,
    queuedSeek,
    queuedRead
  ]).wait(2000);
  test.equal(queuedResults[0], 3, "first queued write byte count");
  test.equal(queuedResults[1], 3, "second queued write byte count");
  test.equal(queuedResults[3], "abcdef",
    "same-stream Futures should execute in submission order");
  test.equal(streamChanges.receive(0), null,
    "stream writes should not publish before a successful commit");
  writeStream.seek(0, Stream.SEEK_END);
  writeStream.flush();
  streamChange = streamChanges.receive(0);
  test.equal(streamChange.type, "write",
    "stream flush should publish one committed write event");
  test.equal(streamChange.path, path,
    "stream flush event path");
  test.equal(writeStream.tell(), 6, "tell after write");
  writeStream.seek(0, Stream.SEEK_SET);
  test.equal(writeStream.read(2), "ab", "read after write");
  writeStream.close();
  test.equal(streamChanges.receive(0), null,
    "closing a clean stream should not duplicate the write event");
  streamChanges.close();

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
  byteTailView = stream.read(8);
  byteTail = byteTailView.toArray();
  test.equal(byteTail.length, 2, "binary read should return short tail chunk");
  test.equal(byteTail[0], 0x65, "binary read tail first byte");
  test.equal(byteTail[1], 0x66, "binary read tail second byte");

  binaryStream = fs.open(binaryPath, "wb");
  test.equal(binaryStream.write(byteChunk), 4,
    "binary stream should write a ByteView without text conversion");
  test.equal(binaryStream.write(byteTailView), 2,
    "binary stream should append a second ByteView exactly");
  binaryStream.close();

  test.equal(byteChunk.close(), true, "ByteView close should succeed");
  test.equal(byteChunk.close(), true, "ByteView close should be idempotent");
  test.equal(byteTailView.close(), true, "tail ByteView close should succeed");
  try {
    byteChunk.toArray();
  } catch (closedViewError) {
    bytes = String(closedViewError);
  }
  test.ok(bytes.indexOf("closed") >= 0,
    "closed ByteView should reject later reads");
  test.equal(stream.read(1), null, "binary read at eof should return null");
  stream.close();

  if (sys.info.features.bitmap && typeof bitmap === "object") {
    spanBitmap = bitmap.create({
      width: 4,
      height: 2,
      format: "gray8",
      chunkBytes: 2
    });
    spanBitmap.clear(0x2a);
    spanSource = spanBitmap.createSpanSource({ chunkBytes: 2 });
    spanStream = fs.open(spanPath, "wb");
    try {
      spanWrite = Future.call(spanStream.write, spanStream, [spanSource]);
      test.equal(spanWrite.wait(1000), 8,
        "binary Stream Future should consume all ByteSpanSource spans");
      spanStream.close();
      spanStream = null;
      test.equal(fs.stat(spanPath).size, 8,
        "ByteSpanSource stream output should have the exact byte length");
    } finally {
      if (spanStream !== null) {
        spanStream.close();
      }
      spanSource.close();
      spanBitmap.close();
    }
    fs.remove(spanPath);
  }
  fs.remove(path);
  fs.remove(binaryPath);

  return { first: first, second: second };
});
