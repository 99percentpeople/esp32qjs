# `Stream` Type

`fs.open()` returns a `Stream`. `Response.body`, `Request.body`, and
`Response.stream(...)` also use the same stream interface. A `ByteView` is a
read-only native byte view; `getUint8(offset)` reads one byte without copying
the view and `toArray()` makes an explicit JavaScript copy. A
`ByteSpanSource` is a retained, one-shot producer of native spans. Its read-only
`byteLength` is the total number of bytes the producer will yield; `close()` is
idempotent and releases producer-owned resources. Owned
`ByteView` values also have an idempotent `close()`; call it after the last
consumer or `toArray()` conversion to release native storage deterministically.
Opening a `ByteSpanSource` acquires a read lease until the consumer finishes.
The current span remains valid until the consumer asks for the next span or
closes the iterator, so cooperative UART and USB consumers do not make a
defensive full-span copy. Calling `close()` or changing a reusable source with
`setRect()` while it is leased throws a busy error.

- `Stream.SEEK_SET`
- `Stream.SEEK_CUR`
- `Stream.SEEK_END`
  Seek constants for `stream.seek(...)`.

Stream instance shape:

- `kind`
  Stream kind. File streams currently report `"file"`.
- `path`
  Full LittleFS path for file-backed streams, for example `"/littlefs/notes.txt"`.
- `mode`
  The mode string passed to `fs.open(...)`.
- `readable`
- `writable`
- `read(size?)`
  Read up to `size` bytes. Text modes are a trusted-text convenience and do not
  validate encoding or preserve character boundaries. Use a binary mode such
  as `"rb"` for arbitrary data and application-owned decoding; binary reads
  return an owned native `ByteView`. Returns `null` at EOF. Default chunk size
  is `1024`. Call `toArray()` only when JavaScript needs to inspect or parse a
  `ByteView`, and close that view in `finally`.
- `write(value)`
  Write and return the exact byte count. Text file modes accept strings.
  Binary file modes accept `ByteView`, array-like byte data, or a retained
  `ByteSpanSource`; the Future driver opens a source lease during capture and
  writes one span at a time without materializing the complete source. No
  newline is added.
- `flush()`
- `close()`
  Successful `flush()` or `close()` publishes one filesystem `write` event if
  the stream has committed changes; individual spans do not publish events.
- `seek(offset, whence?)`
  Move the file cursor and return the new position.
- `tell()`
  Return the current file cursor position.
- `eof()`
  Return `true` once the stream reached EOF.

Example:

```js
var stream = fs.open("_sys/display.js", "r");
print(stream.tell());
print(JSON.stringify(stream.read(32)));
stream.seek(0, Stream.SEEK_SET);
stream.close();
```
