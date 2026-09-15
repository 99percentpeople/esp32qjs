"""Read ELF32 little-endian relocatable objects without target policy."""
import struct


class ElfObject:
    def __init__(self, source: bytes):
        if len(source) < 52 or source[:7] != b"\x7fELF\x01\x01\x01" or struct.unpack_from("<H", source, 16)[0] != 1:
            raise ValueError("Expected ELF32 little-endian relocatable object")
        self.source = source
        self.shoff = struct.unpack_from("<I", source, 32)[0]
        width, count, names = struct.unpack_from("<HHH", source, 46)
        if width != 40 or not count or names >= count or self.shoff + width * count > len(source):
            raise ValueError("Invalid ELF object section table")
        self.sections = [list(struct.unpack_from("<10I", source, self.shoff + i * width)) for i in range(count)]
        strings = self.body(names)
        self.names = [self.string(strings, s[0]) for s in self.sections]
        tables = [i for i, s in enumerate(self.sections) if s[1] == 2]
        if len(tables) != 1:
            raise ValueError("Expected one ELF object symbol table")
        self.table_index = tables[0]
        table = self.sections[self.table_index]
        if table[9] != 16 or table[5] % 16:
            raise ValueError("Invalid ELF object symbols")
        self.strings_index = table[6]
        self.strings = self.body(self.strings_index)
        symbols = self.body(self.table_index)
        self.symbols = [list(struct.unpack_from("<IIIBBH", symbols, i)) for i in range(0, len(symbols), 16)]
        self.symbol_names = [self.string(self.strings, s[0]) for s in self.symbols]

    @staticmethod
    def string(strings: bytes, offset: int) -> str:
        return strings[offset:strings.index(b"\0", offset)].decode("ascii")

    def body(self, index: int) -> bytes:
        section = self.sections[index]
        if section[1] == 8:  # NOBITS has no file payload.
            return b""
        offset, size = section[4:6]
        if offset + size > len(self.source):
            raise ValueError("ELF object section exceeds input")
        return self.source[offset:offset + size]
