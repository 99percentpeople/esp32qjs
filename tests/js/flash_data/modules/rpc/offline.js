test("rpc/offline", function () {
  var baseline = rpc.status();
  var codec;
  var decoder;
  var bytes;
  var frames;
  var messages = [];
  var decoderStatus;
  var orphanDecoder;
  var replacement;
  var closedError = "";
  var i;
  var originalObjectKeys;

  test.equal(rpc.PROTOCOL, "esp32qjs.rpc/1",
    "rpc.PROTOCOL should identify the v1 wire contract");
  test.equal(rpc.RESPONSE, 4, "rpc.RESPONSE should expose the response flag");
  test.equal(rpc.ERROR, 8, "rpc.ERROR should expose the error flag");
  test.equal(typeof rpc.FIRST, "undefined",
    "record segmentation flags should not be public message flags");
  test.equal(typeof rpc.createDecoder, "undefined",
    "decoder creation should belong to RPCCodec");
  test.equal(typeof rpc.encode, "undefined",
    "encoding should belong to RPCCodec");

  codec = rpc.createCodec({
    fields: ["ok", "data"],
    streamDirectory: "/littlefs"
  });
  test.ok(codec instanceof RPCCodec,
    "rpc.createCodec() should return an RPCCodec object");
  test.equal(rpc.status().activeCodecs, baseline.activeCodecs + 1,
    "codec status should count live object handles");

  decoder = codec.createDecoder();
  test.ok(decoder instanceof RPCDecoder,
    "RPCCodec.createDecoder() should return an RPCDecoder object");
  test.equal(rpc.status().activeDecoders, baseline.activeDecoders + 1,
    "RPC status should count live decoder objects");

  bytes = rpc.bytes([0, 1, 2, 255]);
  frames = codec.encode(1, 7, 0, { ok: true, data: bytes });
  bytes.close();
  test.ok(Object.prototype.toString.call(frames) === "[object Array]",
    "a bounded message should encode to an array of frames");
  for (i = 0; i < frames.length; i += 1) {
    try {
      messages = messages.concat(decoder.feed(frames[i]));
    } finally {
      frames[i].close();
    }
  }
  test.equal(messages.length, 1, "decoder should emit one logical message");
  test.equal(messages[0].opcode, 1, "decoded opcode should round-trip");
  test.equal(messages[0].requestId, 7, "decoded request id should round-trip");
  test.equal(messages[0].payload.ok, true, "decoded map values should round-trip");
  test.ok(messages[0].payload.data instanceof _ByteView,
    "decoded CBOR byte strings should use ByteView");
  try {
    test.equal(messages[0].payload.data.length, 4,
      "decoded byte strings should preserve length");
  } finally {
    messages[0].payload.data.close();
  }

  originalObjectKeys = Object.keys;
  Object.keys = function () { return []; };
  try {
    frames = codec.encode(1, 8, 0, { ok: true });
  } finally {
    Object.keys = originalObjectKeys;
  }
  messages = [];
  for (i = 0; i < frames.length; i += 1) {
    try {
      messages = messages.concat(decoder.feed(frames[i]));
    } finally {
      frames[i].close();
    }
  }
  test.equal(messages[0].payload.ok, true,
    "RPC map enumeration should ignore mutable Object.keys");

  decoderStatus = decoder.status();
  test.equal(decoderStatus.open, true, "decoder status should report an open handle");
  test.equal(decoderStatus.messages, 2, "decoder status should count messages");
  test.equal(decoderStatus.errors, 0, "decoder status should count errors");
  test.equal(decoder.reset(), true, "decoder reset should preserve its handle");
  test.equal(decoder.close(), true, "decoder close should release its slot");
  test.equal(decoder.close(), false, "decoder close should be idempotent");
  try {
    decoder.feed([]);
  } catch (error) {
    closedError = String(error);
  }
  test.ok(closedError.indexOf("RPCDecoder") >= 0,
    "closed decoder objects should reject further use");
  test.equal(codec.close(), true, "codec close should release its slot");
  test.equal(codec.close(), false, "codec close should be idempotent");
  test.equal(rpc.status().activeCodecs, baseline.activeCodecs,
    "explicit close should restore the codec count");
  test.equal(rpc.status().activeDecoders, baseline.activeDecoders,
    "explicit close should restore the decoder count");

  function createOrphanDecoder() {
    var orphanCodec = rpc.createCodec({ fields: ["ok"] });
    return orphanCodec.createDecoder();
  }

  orphanDecoder = createOrphanDecoder();
  gc();
  test.equal(orphanDecoder.status().open, true,
    "a decoder should remain usable after its codec object is finalized");
  test.equal(rpc.status().activeCodecs, baseline.activeCodecs + 1,
    "codec cleanup should remain pending while its decoder is live");
  orphanDecoder.close();
  test.equal(rpc.status().activeCodecs, baseline.activeCodecs,
    "closing the final decoder should release its finalized codec");

  replacement = rpc.createCodec({ fields: ["ok"] });
  test.equal(codec.close(), false,
    "a closed object must not close a newer codec that reused its slot");
  replacement.close();

  function abandonHandles() {
    var abandonedCodec = rpc.createCodec({ fields: ["ok"] });
    abandonedCodec.createDecoder();
  }

  abandonHandles();
  gc();
  test.equal(rpc.status().activeCodecs, baseline.activeCodecs,
    "codec and decoder finalizers should release an abandoned object graph");
  test.equal(rpc.status().activeDecoders, baseline.activeDecoders,
    "decoder finalization should restore the decoder count");

  return {
    protocol: rpc.PROTOCOL,
    messages: decoderStatus.messages,
    errors: decoderStatus.errors
  };
});
