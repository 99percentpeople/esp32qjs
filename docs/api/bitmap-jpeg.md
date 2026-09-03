# Bitmap JPEG decoder

The optional `bitmap_jpeg` native feature adds `Bitmap.prototype.decode` when
`sys.info.features.bitmapJpeg` is true. Decoding writes directly into an
existing linear RGB565 Bitmap.

```text
bitmapValue.decode(source, {
  codec: "jpeg",
  destinationRect?: { x: number, y: number, width: number, height: number }
}) -> {
  codec: "jpeg",
  engine: "rom-tjpgd" | "software-tjpgd",
  width: number,
  height: number,
  inputBytes: number,
  outputBytes: number
}
```

The destination Bitmap must be linear `rgb565`. `source` is a `ByteSource` or
`{ chunks, lengths, byteLength }`, where useful bytes from each chunk form one
compressed image in order. Baseline JPEG is decoded at its source dimensions.
With no `destinationRect`, decoded dimensions equal the Bitmap dimensions.
With a destination rectangle, decoded dimensions equal that rectangle and the
remaining pixels are unchanged.

The call leases the destination and every source while decoding and performs
the bounded native work through the Future worker queue. Compressed input and
output pixels remain in native storage. The result reports `rom-tjpgd` when
the selected target uses the ROM TJpgDec path, otherwise `software-tjpgd`.
