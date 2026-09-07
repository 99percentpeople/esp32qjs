test("rpc/memory", function () {
  var baseline;
  var active;
  var codec;
  var decoder;
  var frames;
  var messages;
  var index;

  function memory() {
    var allocations = sys.status.memory.manager.allocations;
    var total = { bytes: 0, blocks: 0, internal: 0 };
    var i;
    for (i = 0; i < allocations.length; i += 1) {
      if (allocations[i].owner === "rpc.decoder") {
        total.bytes += allocations[i].bytes;
        total.blocks += allocations[i].blocks;
        if (allocations[i].region === "internal") {
          total.internal += allocations[i].bytes;
        }
      }
    }
    return total;
  }

  function released() {
    test.equal(memory().bytes, baseline.bytes, "decoder bytes released");
    test.equal(memory().blocks, baseline.blocks, "decoder blocks released");
  }

  baseline = memory();
  codec = rpc.createCodec({ fields: ["ok"] });
  try {
    for (index = 0; index < 20; index += 1) {
      decoder = codec.createDecoder();
      try {
        active = memory();
        test.equal(active.blocks, baseline.blocks + 1, "one buffer per decoder");
        test.ok(active.bytes > baseline.bytes, "frame bytes accounted");
        if (sys.status.memory.psram.totalBytes > 0) {
          test.equal(active.internal, 0, "frame buffer belongs in PSRAM");
        }
        frames = codec.encode(1, index, 0, { ok: true });
        try {
          messages = decoder.feed(frames[0]);
          test.equal(messages.length, 1, "decode after slot reuse");
          test.equal(messages[0].requestId, index, "request ID preserved");
          test.equal(messages[0].payload.ok, true, "payload preserved");
        } finally {
          frames[0].close();
        }
        decoder.reset();
        test.equal(memory().bytes, active.bytes, "reset retains frame buffer");
      } finally {
        decoder.close();
      }
      test.equal(decoder.close(), false, "close is idempotent");
      released();
    }
  } finally {
    codec.close();
  }

  function orphan() {
    var owner = rpc.createCodec({ fields: ["ok"] });
    return owner.createDecoder();
  }

  decoder = orphan();
  try {
    gc();
    test.equal(decoder.status().open, true, "decoder survives codec GC");
    test.equal(memory().blocks, baseline.blocks + 1, "orphan retains buffer");
  } finally {
    decoder.close();
  }
  released();

  function abandon() {
    var owner = rpc.createCodec({ fields: ["ok"] });
    owner.createDecoder();
  }

  abandon();
  gc();
  released();
  return { cycles: 20, bytesPerDecoder: active.bytes - baseline.bytes };
});
