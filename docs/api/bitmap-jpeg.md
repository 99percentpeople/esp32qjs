# Bitmap JPEG decoder

The optional `bitmap_jpeg` native feature adds `Bitmap.prototype.decode` when
`sys.info.features.bitmapJpeg` is true. It does not add a second Bitmap type or
an encoder.

```text
bitmapValue.decode(source, {
  codec: "jpeg",
  destinationRect?: { x: number, y: number, width: number, height: number }
}) -> {
  codec: "jpeg",
  width: number,
  height: number,
  outputWidth: number,
  outputHeight: number,
  decoder: "hardware-ppa" | "software-tjpgd"
}
```

The destination Bitmap must be linear `rgb565`. `source` is a `ByteSource` or
`{ chunks, lengths, byteLength }`, where useful bytes from each chunk form one
compressed image in order. Baseline JPEG is supported; progressive JPEG and
scaling are rejected. With no `destinationRect`, decoded dimensions must equal
the Bitmap dimensions. With a destination rectangle, decoded dimensions must
equal that rectangle and the remaining pixels are unchanged.

The call leases the destination and every source while decoding and performs
the bounded native work through the Future worker queue. It never materializes
the compressed image or output pixels as a JavaScript number array. The result
reports `hardware-ppa` when the selected target and dimensions can use the
hardware path, otherwise `software-tjpgd`.
