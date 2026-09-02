# `rpc` Module

`rpc` is exposed only when `sys.info.features.rpc` is enabled. It is a generic
`esp32qjs.rpc/1` connection codec: COBS record framing, CRC-32 corruption
detection, deterministic CBOR, incremental reassembly, and transparent
`ByteSpanSource` streaming. The framework does not define opcodes, request
dispatch, authentication, authorization, retries, queues, workspace paths, or
an application schema.

- `rpc.createCodec(options)`
  Create and return an `RPCCodec` object. `options.fields` is the
  non-empty ordered list that maps application field names to CBOR integer
  keys. `dynamicFields` optionally names fields whose nested JSON-like maps use
  CBOR text keys exclusively, including numeric-looking JavaScript property
  names. `allowStringKeys` applies the same text-key rule at the root when true.
  `streamDirectory` optionally selects where incoming transparent streams are
  spooled; without it, that codec rejects streamed input. `close()` releases
  the codec after all of its decoders have been closed.
- `codec.createDecoder()`
  Create an `RPCDecoder` object for incremental connection state. Keep one
  decoder per physical connection; each decoder accepts arbitrary input chunk
  boundaries. `close()` releases it deterministically; its finalizer provides
  the same cleanup if application code drops the object.
- `decoder.feed(data)`
  Feed one raw transport chunk and return zero or more complete
  `{ opcode, requestId, flags, logicalLength, payload }` messages. The caller
  never parses or reassembles segments.
- `decoder.reset()` / `decoder.status()`
  Discard an incomplete message and any temporary streamed input while keeping
  the decoder object, or inspect its message/error and active-stream counters.
- `codec.encode(opcode, requestId, flags, payload)`
  Encode one logical message. A regular payload returns an array of owning
  `ByteView` frames. If the final CBOR value is a `ByteSpanSource`, it returns a
  one-shot `ByteSpanSource` that produces already framed bytes lazily. The
  caller writes either result to a binary transport without adding delimiters
  or performing its own segmentation.
- `rpc.bytes(value)`
  Copy byte data into an owning `ByteView` suitable for a CBOR byte string.
- `rpc.fileSource(path)` / `rpc.sourceInfo(source)`
  Create a one-shot file-backed `RPCFileSource` for any readable regular file
  within the RPC stream limit, and inspect its `{ size, crc32 }` metadata.
  Locally created sources report a null CRC until transferred. `sourceInfo()`
  accepts only an RPC file source; use the generic `source.byteLength` property
  for Bitmap, camera, CSI, codec-output, and other `ByteSpanSource` producers.
- `rpc.adoptFile(source, path)`
  Atomically rename an unused inbound temporary stream to an
  application-selected destination. It does not impose a workspace policy.
- `rpc.status()`
  Return the wire protocol name and aggregate codec, decoder, message, and
  error counters.

The codec accepts only the deterministic, definite-length CBOR subset. It
rejects tags, indefinite values, duplicate/non-canonical map keys, invalid
UTF-8, non-finite floats, excessive nesting, trailing data, interleaved logical
messages, and invalid frame CRCs. CRC-32 detects accidental transport or
storage corruption; it is not authentication.

```js
var codec = rpc.createCodec({
  fields: ["ok", "result", "data"],
  dynamicFields: ["result"],
  streamDirectory: "/data"
});
var decoder = codec.createDecoder();
var frames = codec.encode(1, 7, 0, {
  data: rpc.bytes([0, 1, 2, 255])
});

// A TCP/serial receive callback may pass chunks of any size.
var messages = decoder.feed(incomingChunk);

decoder.close();
codec.close();
```

The public C wire contract and incremental decoder are declared in
`include/esp32qjs_rpc_wire.h`, so a firmware application may use the framing
layer without adopting the JavaScript Agent product.
