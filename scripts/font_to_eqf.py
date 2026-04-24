#!/usr/bin/env python3
"""Convert simple bitmap fonts to esp32qjs EQF1 fonts.

EQF1 is intentionally small: one fixed-size bitmap cell per single-byte
codepoint, stored column-major with vertical pixels in least-significant-bit
order. This tool keeps that model explicit and rejects inputs that need a
Unicode cmap or variable glyph metrics.
"""

from __future__ import annotations

import argparse
import json
import struct
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any


EQF_MAGIC = b"EQF1"
EQF_FORMAT_BITMAP_FIXED = 1
EQF_FLAGS_COLUMN_LSB = 0
MAX_EQF1_CODEPOINT = 0xFF
MAX_EQF1_DIMENSION = 0xFF
MAX_EQF1_HEIGHT = 64
DEFAULT_SAFE_SLOT_FIRST = 0x20
DEFAULT_SAFE_SLOT_LAST = 0x7E


@dataclass(frozen=True)
class BitmapGlyph:
    codepoint: int
    width: int
    height: int
    pixels: tuple[tuple[bool, ...], ...]


@dataclass(frozen=True)
class FontSource:
    glyphs: dict[int, BitmapGlyph]
    width: int
    height: int
    advance: int
    line_height: int


@dataclass
class BdfGlyphBuilder:
    codepoint: int | None = None
    dwidth: int | None = None
    bbx: tuple[int, int, int, int] | None = None
    bitmap_rows: list[bytes] | None = None


def parse_int(value: str) -> int:
    try:
        return int(value, 0)
    except ValueError as exc:
        raise argparse.ArgumentTypeError(f"invalid integer: {value}") from exc


def key_to_codepoint(key: str) -> int:
    if len(key) == 1:
        return ord(key)
    normalized = key.strip()
    if normalized.startswith(("U+", "u+")):
        return int(normalized[2:], 16)
    return int(normalized, 0)


def ensure_metric(name: str, value: int, max_value: int = MAX_EQF1_DIMENSION) -> int:
    if value <= 0 or value > max_value:
        raise ValueError(f"{name} must be in 1..{max_value}, got {value}")
    return value


def empty_pixels(width: int, height: int) -> list[list[bool]]:
    return [[False for _ in range(width)] for _ in range(height)]


def freeze_pixels(pixels: list[list[bool]]) -> tuple[tuple[bool, ...], ...]:
    return tuple(tuple(row) for row in pixels)


def parse_bdf_bitmap_row(line: str, width: int) -> list[bool]:
    row_bytes = bytes.fromhex(line.strip())
    pixels: list[bool] = []
    for x in range(width):
        byte = row_bytes[x // 8] if x // 8 < len(row_bytes) else 0
        pixels.append((byte & (0x80 >> (x & 7))) != 0)
    return pixels


def convert_bdf_glyph(
    builder: BdfGlyphBuilder,
    cell_width: int,
    cell_height: int,
    font_x_offset: int,
    font_y_offset: int,
) -> BitmapGlyph | None:
    if builder.codepoint is None or builder.codepoint < 0:
        return None
    if builder.bbx is None:
        raise ValueError(f"BDF glyph {builder.codepoint} is missing BBX")

    glyph_width, glyph_height, glyph_x_offset, glyph_y_offset = builder.bbx
    bitmap_rows = builder.bitmap_rows or []
    pixels = empty_pixels(cell_width, cell_height)

    if len(bitmap_rows) < glyph_height:
        raise ValueError(f"BDF glyph {builder.codepoint} has truncated BITMAP data")

    top = cell_height - ((glyph_y_offset - font_y_offset) + glyph_height)
    left = glyph_x_offset - font_x_offset
    for src_y in range(glyph_height):
        row_pixels = parse_bdf_bitmap_row(bitmap_rows[src_y].hex(), glyph_width)
        dst_y = top + src_y
        if dst_y < 0 or dst_y >= cell_height:
            continue
        for src_x, enabled in enumerate(row_pixels):
            dst_x = left + src_x
            if 0 <= dst_x < cell_width:
                pixels[dst_y][dst_x] = enabled

    return BitmapGlyph(
        codepoint=builder.codepoint,
        width=cell_width,
        height=cell_height,
        pixels=freeze_pixels(pixels),
    )


def finish_bdf_glyph(
    builder: BdfGlyphBuilder | None,
    glyphs: dict[int, BdfGlyphBuilder],
    raw_glyphs: list[BdfGlyphBuilder],
) -> None:
    if builder is None:
        return
    if builder.codepoint is not None and builder.codepoint >= 0:
        glyphs[builder.codepoint] = builder
        raw_glyphs.append(builder)


def parse_bdf(path: Path, args: argparse.Namespace) -> FontSource:
    font_bbox: tuple[int, int, int, int] | None = None
    raw_by_codepoint: dict[int, BdfGlyphBuilder] = {}
    raw_glyphs: list[BdfGlyphBuilder] = []
    current: BdfGlyphBuilder | None = None
    in_bitmap = False

    for line_number, line in enumerate(path.read_text(encoding="utf-8", errors="replace").splitlines(), start=1):
        stripped = line.strip()
        if not stripped:
            continue
        if current is not None and in_bitmap:
            if stripped == "ENDCHAR":
                finish_bdf_glyph(current, raw_by_codepoint, raw_glyphs)
                current = None
                in_bitmap = False
            else:
                try:
                    bytes.fromhex(stripped)
                except ValueError as exc:
                    raise ValueError(f"{path}:{line_number}: invalid BDF bitmap row") from exc
                current.bitmap_rows = current.bitmap_rows or []
                current.bitmap_rows.append(bytes.fromhex(stripped))
            continue

        parts = stripped.split()
        keyword = parts[0]
        if keyword == "FONTBOUNDINGBOX" and len(parts) >= 5:
            font_bbox = tuple(int(part, 10) for part in parts[1:5])  # type: ignore[assignment]
        elif keyword == "STARTCHAR":
            current = BdfGlyphBuilder()
        elif keyword == "ENCODING" and current is not None and len(parts) >= 2:
            current.codepoint = int(parts[1], 10)
        elif keyword == "DWIDTH" and current is not None and len(parts) >= 2:
            current.dwidth = int(parts[1], 10)
        elif keyword == "BBX" and current is not None and len(parts) >= 5:
            current.bbx = tuple(int(part, 10) for part in parts[1:5])  # type: ignore[assignment]
        elif keyword == "BITMAP" and current is not None:
            current.bitmap_rows = []
            in_bitmap = True
        elif keyword == "ENDCHAR" and current is not None:
            finish_bdf_glyph(current, raw_by_codepoint, raw_glyphs)
            current = None

    if not raw_by_codepoint:
        raise ValueError(f"{path} does not contain any encoded BDF glyphs")

    if font_bbox is None:
        min_x = min(builder.bbx[2] for builder in raw_glyphs if builder.bbx is not None)
        min_y = min(builder.bbx[3] for builder in raw_glyphs if builder.bbx is not None)
        max_x = max(builder.bbx[2] + builder.bbx[0] for builder in raw_glyphs if builder.bbx is not None)
        max_y = max(builder.bbx[3] + builder.bbx[1] for builder in raw_glyphs if builder.bbx is not None)
        font_bbox = (max_x - min_x, max_y - min_y, min_x, min_y)

    bbox_width, bbox_height, bbox_x_offset, bbox_y_offset = font_bbox
    width = args.width if args.width is not None else bbox_width
    height = args.height if args.height is not None else bbox_height
    advance = args.advance
    if advance is None:
        dwidths = [builder.dwidth for builder in raw_glyphs if builder.dwidth and builder.dwidth > 0]
        advance = max(dwidths) if dwidths else width
    line_height = args.line_height if args.line_height is not None else height

    width = ensure_metric("width", width)
    height = ensure_metric("height", height, MAX_EQF1_HEIGHT)
    advance = ensure_metric("advance", advance)
    line_height = ensure_metric("line-height", line_height)
    if line_height < height:
        raise ValueError("line-height must be greater than or equal to height")

    glyphs: dict[int, BitmapGlyph] = {}
    for codepoint, builder in raw_by_codepoint.items():
        glyph = convert_bdf_glyph(builder, width, height, bbox_x_offset, bbox_y_offset)
        if glyph is not None:
            glyphs[codepoint] = glyph

    return FontSource(glyphs=glyphs, width=width, height=height, advance=advance, line_height=line_height)


def row_strings_to_pixels(rows: list[str], width: int, height: int) -> tuple[tuple[bool, ...], ...]:
    pixels = empty_pixels(width, height)
    for y, row in enumerate(rows[:height]):
        for x, value in enumerate(row[:width]):
            pixels[y][x] = value not in ("0", ".", " ", "_")
    return freeze_pixels(pixels)


def columns_to_pixels(columns: list[int], width: int, height: int) -> tuple[tuple[bool, ...], ...]:
    bytes_per_column = (height + 7) // 8
    expected = width * bytes_per_column
    if len(columns) != expected:
        raise ValueError(f"column glyph expects {expected} bytes, got {len(columns)}")
    pixels = empty_pixels(width, height)
    for x in range(width):
        for byte_index in range(bytes_per_column):
            bits = columns[(x * bytes_per_column) + byte_index]
            for bit in range(8):
                y = (byte_index * 8) + bit
                if y < height and (bits & (1 << bit)) != 0:
                    pixels[y][x] = True
    return freeze_pixels(pixels)


def parse_json_glyph(value: Any, width: int, height: int) -> tuple[tuple[bool, ...], ...]:
    if isinstance(value, dict):
        if "rows" in value:
            rows = value["rows"]
            if not isinstance(rows, list) or not all(isinstance(row, str) for row in rows):
                raise ValueError("JSON glyph rows must be a list of strings")
            return row_strings_to_pixels(rows, width, height)
        if "columns" in value:
            columns = value["columns"]
            if not isinstance(columns, list) or not all(isinstance(item, int) for item in columns):
                raise ValueError("JSON glyph columns must be a list of integers")
            return columns_to_pixels(columns, width, height)
        raise ValueError("JSON glyph object must contain rows or columns")
    if isinstance(value, list) and all(isinstance(row, str) for row in value):
        return row_strings_to_pixels(value, width, height)
    if isinstance(value, list) and all(isinstance(item, int) for item in value):
        return columns_to_pixels(value, width, height)
    raise ValueError("JSON glyph must be row strings, column bytes, or an object")


def parse_json_font(path: Path, args: argparse.Namespace) -> FontSource:
    data = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(data, dict):
        raise ValueError("JSON font must be an object")

    glyph_data = data.get("glyphs")
    if not isinstance(glyph_data, dict) or not glyph_data:
        raise ValueError("JSON font must contain a non-empty glyphs object")

    width = args.width if args.width is not None else data.get("width")
    height = args.height if args.height is not None else data.get("height")
    advance = args.advance if args.advance is not None else data.get("advance", width)
    line_height = args.line_height if args.line_height is not None else data.get("lineHeight", height)
    if not all(isinstance(value, int) for value in (width, height, advance, line_height)):
        raise ValueError("JSON font metrics width, height, advance, and lineHeight must be integers")

    width = ensure_metric("width", width)
    height = ensure_metric("height", height, MAX_EQF1_HEIGHT)
    advance = ensure_metric("advance", advance)
    line_height = ensure_metric("line-height", line_height)
    if line_height < height:
        raise ValueError("line-height must be greater than or equal to height")

    glyphs: dict[int, BitmapGlyph] = {}
    for key, value in glyph_data.items():
        codepoint = key_to_codepoint(str(key))
        glyphs[codepoint] = BitmapGlyph(
            codepoint=codepoint,
            width=width,
            height=height,
            pixels=parse_json_glyph(value, width, height),
        )

    return FontSource(glyphs=glyphs, width=width, height=height, advance=advance, line_height=line_height)


def unique_chars(text: str) -> list[str]:
    chars: list[str] = []
    seen: set[str] = set()

    for ch in text:
        if ch not in seen:
            chars.append(ch)
            seen.add(ch)
    return chars


def resolve_manifest_path(manifest_path: Path, path_value: str) -> Path:
    path = Path(path_value)
    if path.is_absolute():
        return path
    return manifest_path.parent / path


def manifest_int(data: dict[str, Any], key: str, default: int) -> int:
    value = data.get(key, default)
    if not isinstance(value, int):
        raise ValueError(f"manifest field {key!r} must be an integer")
    return value


def render_truetype_glyph(
    font: Any,
    ch: str,
    codepoint: int,
    width: int,
    height: int,
    threshold: int,
) -> BitmapGlyph:
    from PIL import Image, ImageDraw

    image = Image.new("L", (width, height), 0)
    draw = ImageDraw.Draw(image)
    bbox = draw.textbbox((0, 0), ch, font=font)
    glyph_width = bbox[2] - bbox[0]
    glyph_height = bbox[3] - bbox[1]
    x = ((width - glyph_width) // 2) - bbox[0]
    y = ((height - glyph_height) // 2) - bbox[1]
    pixels = empty_pixels(width, height)

    draw.text((x, y), ch, font=font, fill=255)
    for row in range(height):
        for col in range(width):
            pixels[row][col] = image.getpixel((col, row)) >= threshold

    return BitmapGlyph(codepoint=codepoint, width=width, height=height, pixels=freeze_pixels(pixels))


def parse_manifest_size(key: str, value: Any) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise ValueError(f"manifest size {key!r} must be an object")
    return value


def generate_manifest_fonts(path: Path, output: Path | None, args: argparse.Namespace) -> list[tuple[Path, bytes, FontSource, int, int]]:
    try:
        from PIL import ImageFont
    except ImportError as exc:
        raise ValueError("manifest font generation requires Pillow (python3-pil or pillow)") from exc

    data = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(data, dict):
        raise ValueError("manifest must be a JSON object")

    chars_value = data.get("chars")
    if not isinstance(chars_value, str) or not chars_value:
        raise ValueError("manifest field 'chars' must be a non-empty string")
    chars = unique_chars(chars_value)

    slot_first = manifest_int(data, "slotFirst", DEFAULT_SAFE_SLOT_FIRST)
    slot_last = slot_first + len(chars) - 1
    if slot_first < 0 or slot_last > MAX_EQF1_CODEPOINT:
        raise ValueError(f"manifest slot range must fit EQF1 0..255, got {slot_first:#x}..{slot_last:#x}")
    if data.get("safeAscii", True) and slot_last > DEFAULT_SAFE_SLOT_LAST:
        raise ValueError(
            f"manifest has {len(chars)} unique chars but safe ASCII slots only hold "
            f"{DEFAULT_SAFE_SLOT_LAST - slot_first + 1}; reduce chars or set safeAscii=false"
        )

    source = data.get("source", {})
    if not isinstance(source, dict):
        raise ValueError("manifest field 'source' must be an object")
    font_path_value = args.font_path or source.get("path")
    if not isinstance(font_path_value, str) or not font_path_value:
        raise ValueError("manifest source.path or --font-path is required")
    font_path = Path(font_path_value)
    if not font_path.exists():
        raise ValueError(f"font file does not exist: {font_path}")

    sizes = data.get("sizes")
    if not isinstance(sizes, dict) or not sizes:
        raise ValueError("manifest field 'sizes' must be a non-empty object")

    outputs: list[tuple[Path, bytes, FontSource, int, int]] = []
    for size_name, raw_size in sizes.items():
        size = parse_manifest_size(str(size_name), raw_size)
        width = ensure_metric("width", int(size.get("width", size_name)))
        height = ensure_metric("height", int(size.get("height", size_name)), MAX_EQF1_HEIGHT)
        advance = ensure_metric("advance", int(size.get("advance", width)))
        line_height = ensure_metric("lineHeight", int(size.get("lineHeight", height)))
        font_size = ensure_metric("fontSize", int(size.get("fontSize", height)))
        threshold = int(size.get("threshold", data.get("threshold", 96)))
        if line_height < height:
            raise ValueError(f"manifest size {size_name!r} lineHeight must be >= height")
        if output is not None and len(sizes) == 1:
            output_path = output
        else:
            output_path_value = size.get("path")
            if not isinstance(output_path_value, str) or not output_path_value:
                raise ValueError(f"manifest size {size_name!r} requires path")
            output_path = resolve_manifest_path(path, output_path_value)

        truetype = ImageFont.truetype(str(font_path), font_size)
        glyphs: dict[int, BitmapGlyph] = {}
        for index, ch in enumerate(chars):
            glyphs[slot_first + index] = render_truetype_glyph(
                truetype,
                ch,
                slot_first + index,
                width,
                height,
                threshold,
            )
        source_font = FontSource(
            glyphs=glyphs,
            width=width,
            height=height,
            advance=advance,
            line_height=line_height,
        )
        eqf = build_eqf(source_font, slot_first, slot_last, "error")
        outputs.append((output_path, eqf, source_font, slot_first, slot_last))

    return outputs


def infer_format(path: Path, requested: str) -> str:
    if requested != "auto":
        return requested
    suffix = path.suffix.lower()
    if suffix == ".bdf":
        return "bdf"
    if suffix == ".json":
        return "json"
    raise ValueError(f"cannot infer input format from {path}; pass --format bdf, json, or manifest")


def make_blank_glyph(codepoint: int, width: int, height: int) -> BitmapGlyph:
    return BitmapGlyph(codepoint=codepoint, width=width, height=height, pixels=freeze_pixels(empty_pixels(width, height)))


def choose_missing_glyph(
    source: FontSource,
    codepoint: int,
    missing: str,
) -> BitmapGlyph:
    if missing == "error":
        raise ValueError(f"source font is missing required glyph {codepoint:#04x}")
    if missing == "question" and ord("?") in source.glyphs:
        return source.glyphs[ord("?")]
    return make_blank_glyph(codepoint, source.width, source.height)


def encode_glyph(glyph: BitmapGlyph, width: int, height: int) -> bytes:
    bytes_per_column = (height + 7) // 8
    encoded = bytearray()
    for x in range(width):
        for byte_index in range(bytes_per_column):
            value = 0
            for bit in range(8):
                y = (byte_index * 8) + bit
                if y < height and glyph.pixels[y][x]:
                    value |= 1 << bit
            encoded.append(value)
    return bytes(encoded)


def resolve_range(source: FontSource, args: argparse.Namespace) -> tuple[int, int]:
    if args.first is not None or args.last is not None:
        if args.first is None or args.last is None:
            raise ValueError("--first and --last must be provided together")
        first = args.first
        last = args.last
    else:
        codepoints = [codepoint for codepoint in source.glyphs if 0 <= codepoint <= MAX_EQF1_CODEPOINT]
        if not codepoints:
            raise ValueError("EQF1 only supports 0..255 codepoints; no source glyphs are in range")
        first = min(codepoints)
        last = max(codepoints)

    if first < 0 or last < first or last > MAX_EQF1_CODEPOINT:
        raise ValueError(f"EQF1 range must be within 0..255, got {first:#x}..{last:#x}")
    return first, last


def build_eqf(source: FontSource, first: int, last: int, missing: str) -> bytes:
    glyph_bytes = bytearray()
    for codepoint in range(first, last + 1):
        glyph = source.glyphs.get(codepoint)
        if glyph is None:
            glyph = choose_missing_glyph(source, codepoint, missing)
        glyph_bytes.extend(encode_glyph(glyph, source.width, source.height))

    header = bytearray()
    header.extend(EQF_MAGIC)
    header.append(EQF_FORMAT_BITMAP_FIXED)
    header.append(EQF_FLAGS_COLUMN_LSB)
    header.extend((first, last, source.width, source.height, source.advance, source.line_height))
    header.extend(struct.pack("<I", len(glyph_bytes)))
    return bytes(header + glyph_bytes)


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Convert BDF, simple JSON, or mapped-font manifests to the esp32qjs EQF1 format."
    )
    parser.add_argument("input", type=Path, help="Input .bdf, .json bitmap font, or manifest file.")
    parser.add_argument("output", nargs="?", type=Path, help="Output .eqf file. Manifest input usually uses per-size paths.")
    parser.add_argument("--format", choices=("auto", "bdf", "json", "manifest"), default="auto", help="Input format.")
    parser.add_argument("--first", type=parse_int, help="First output codepoint, 0..255.")
    parser.add_argument("--last", type=parse_int, help="Last output codepoint, 0..255.")
    parser.add_argument("--width", type=parse_int, help="Override fixed glyph cell width.")
    parser.add_argument("--height", type=parse_int, help="Override fixed glyph cell height.")
    parser.add_argument("--advance", type=parse_int, help="Override cursor advance.")
    parser.add_argument("--line-height", type=parse_int, help="Override line height.")
    parser.add_argument("--font-path", help="Override manifest source.path for TrueType/OpenType generation.")
    parser.add_argument(
        "--missing",
        choices=("blank", "question", "error"),
        default="blank",
        help="How to fill missing glyphs in the continuous EQF1 range.",
    )
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    try:
        input_format = infer_format(args.input, args.format)
        if input_format == "manifest":
            outputs = generate_manifest_fonts(args.input, args.output, args)
            for output_path, eqf, source, first, last in outputs:
                output_path.parent.mkdir(parents=True, exist_ok=True)
                output_path.write_bytes(eqf)
                glyph_count = last - first + 1
                print(
                    f"wrote {output_path} ({len(eqf)} bytes): "
                    f"range={first:#04x}..{last:#04x} glyphs={glyph_count} "
                    f"cell={source.width}x{source.height} advance={source.advance} lineHeight={source.line_height}"
                )
            return 0

        if args.output is None:
            raise ValueError("output is required for bdf and json input")
        if input_format == "bdf":
            source = parse_bdf(args.input, args)
        elif input_format == "json":
            source = parse_json_font(args.input, args)
        else:
            raise ValueError(f"unsupported input format: {input_format}")

        first, last = resolve_range(source, args)
        eqf = build_eqf(source, first, last, args.missing)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_bytes(eqf)
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        print(f"font_to_eqf.py: error: {exc}", file=sys.stderr)
        return 1

    glyph_count = last - first + 1
    print(
        f"wrote {args.output} ({len(eqf)} bytes): "
        f"range={first:#04x}..{last:#04x} glyphs={glyph_count} "
        f"cell={source.width}x{source.height} advance={source.advance} lineHeight={source.line_height}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
