"""Carry exact initialized RX spans through the pinned CSI constructor.

Only reviewed memcpy and CSI-constructor call relocations are redirected.
Instruction/data layouts and all other relocations remain unchanged. Xtensa
literal and relaxation entries are changed together. Shared SDK files stay intact.
"""
from __future__ import annotations
import argparse
import hashlib
from pathlib import Path
import struct
from patch_idf_twt_probe_wake import patch_archive as rewrite_archive

ARCHIVES = {
    'esp32c3': '756aa3ba9fcd8bef1ee5a8ec36c82406b5ed3a4d43f94ef6b15e05b2cd7a593c',
    'esp32s3': 'ae7004d00c2c5e1cf106548b22e2ed5bb3a059d65393f4e15791019658191cdb',
    'esp32c5': 'a06374436b70eb6b8a2b8d653976218db6e08e7dbe90c8414b07ad9e923d9f99',
}
OBJECTS = {
    'esp32c3': '9ed62f04ae5760b9812e41428d8c3e0a83c0e958115b1abec358ff427754b928',
    'esp32s3': 'ac82076661eceff2ded534af762349c71aec5baeccb1e64eb549da6852a948fd',
    'esp32c5': '2df0eb1e31bc2bd20098deaf0ce406e35287005edb30886b8c7c596891bb435e',
}
BEGIN = 'esp32qjs_wifi_csi_rx_begin_copy'
APPEND = 'esp32qjs_wifi_csi_rx_append_copy'
COPIED = 'esp32qjs_wifi_csi_rx_copied'
# ROM linker scripts can override the original global definitions, discarding
# these instrumented sections. Hidden aliases bind wdev.o's own calls/pointers
# to the reviewed section. External SDK/ROM references retain their original ABI.
RX_ORIGINS = {
    'wDev_IndicateFrame': 'esp32qjs_wifi_csi_indicate_frame',
    'wDev_IndicateAmpdu': 'esp32qjs_wifi_csi_indicate_ampdu',
}
# section: (first-prefix copies, continuation copies, constructor calls).
# Xtensa records pair the literal offset with its ASM_EXPAND call offset.
SITES = {
    'esp32c3': {
        '.text.wDev_SnifferRxData': ([0x118, 0x168, 0x1f8], [0x12c, 0x18a, 0x1e2, 0x21a, 0x24c], [0x1ae]),
        '.text.wDev_IndicateCtrlFrame': ([0xb0], [0xda], [0xfe]),
        '.wifislprxiram.17': ([0x126, 0x1ba], [0x148, 0x17e, 0x1f0, 0x2da], [0x258]),
        '.wifirxiram.19': ([0x158], [0x16e, 0x1b6, 0x21a, 0x2be], [0x254]),
    },
    'esp32c5': {
        '.text.wDev_SnifferRxData': ([0x124, 0x178, 0x20e], [0x13a, 0x198, 0x1fa, 0x242], [0x1d2]),
        '.text.wDev_IndicateCtrlFrame': ([0xa6], [0xce], [0xf8]),
        '.wifislprxiram.15': ([0x130, 0x1d8], [0x168, 0x1a0, 0x210, 0x31a], [0x298]),
        '.wifirxiram.17': ([0x152], [0x168, 0x1b0, 0x214, 0x2b8], [0x24e]),
    },
    'esp32s3': {
        '.text.wDev_SnifferRxData': ([(0xc, 0xfe), (0x10, 0x12c), (0x18, 0x163)],
            [(0x14, 0x14a), (0x1c, 0x183), (0x20, 0x1a2), (0x24, 0x1c7)], [(0x28, 0x1ea)]),
        '.text.wDev_IndicateCtrlFrame': ([(0x14, 0x7f)], [(0x18, 0x9e)], [(0x1c, 0xb8)]),
        '.wifislprxiram.17': ([(0x24, 0x10e), (0x2c, 0x158)],
            [(0x28, 0x133), (0x30, 0x173), (0x34, 0x190), (0x38, 0x1b2)], [(0x3c, 0x216)]),
        '.wifirxiram.19': ([(0x14, 0x123)],
            [(0x18, 0x177), (0x20, 0x1aa), (0x28, 0x1ce)], [(0x30, 0x202)]),
    },
}


def patch_object(source: bytes, target: str) -> bytes:
    if target not in OBJECTS or hashlib.sha256(source).hexdigest() != OBJECTS[target]:
        raise ValueError('Unreviewed CSI RX wdev.o: ' + target)
    machine = 94 if target == 'esp32s3' else 243
    if source[:7] != b'\x7fELF\x01\x01\x01' or struct.unpack_from('<HH', source, 16) != (1, machine):
        raise ValueError('Expected reviewed CSI ELF32 relocatable object')
    shoff = struct.unpack_from('<I', source, 32)[0]
    width, count, names_index = struct.unpack_from('<HHH', source, 46)
    if width != 40 or not count or names_index >= count or shoff + width * count > len(source):
        raise ValueError('Invalid CSI section table')
    sections = [list(struct.unpack_from('<10I', source, shoff + i * width)) for i in range(count)]

    def body(section):
        offset, length = section[4:6]
        if offset + length > len(source):
            raise ValueError('CSI section exceeds object')
        return source[offset:offset + length]

    def string(strings, offset):
        return strings[offset:strings.index(b'\0', offset)].decode('ascii')

    names = body(sections[names_index])
    by_name = {string(names, section[0]): i for i, section in enumerate(sections)}
    tables = [i for i, section in enumerate(sections) if section[1] == 2]
    if len(tables) != 1:
        raise ValueError('Expected one CSI symbol table')
    table_index = tables[0]
    table = sections[table_index]
    if table[9] != 16 or table[5] % 16 or table[6] >= count:
        raise ValueError('Invalid CSI symbol table')
    strings_index = table[6]
    strings, symbols = body(sections[strings_index]), body(table)
    symbol_names = [string(strings, struct.unpack_from('<I', symbols, i)[0]) for i in range(0, len(symbols), 16)]
    added = {}
    for name in (BEGIN, APPEND, COPIED):
        if name in symbol_names:
            raise ValueError('CSI object already patched')
        added[name] = len(symbols) // 16
        symbols += struct.pack('<IIIBBH', len(strings), 0, 0, 0x12, 0, 0)
        strings += name.encode('ascii') + b'\0'
    bound_origins = {}
    for original, alias in RX_ORIGINS.items():
        if alias in symbol_names or symbol_names.count(original) != 1:
            raise ValueError('Unreviewed CSI RX origin symbol')
        index = symbol_names.index(original)
        _, value, size, info, other, section = struct.unpack_from('<IIIBBH', symbols, index * 16)
        if info != 0x12 or other != 0 or not size or section >= count or \
                value + size > sections[section][5] or \
                string(names, sections[section][0]) not in SITES[target]:
            raise ValueError('Unreviewed CSI RX origin definition')
        bound_origins[index] = len(symbols) // 16
        symbols += struct.pack('<IIIBBH', len(strings), value, size, info, 2, section)
        strings += alias.encode('ascii') + b'\0'
    result = bytearray(source)
    for section in sections:
        if section[1] != 4 or section[6] != table_index:
            continue
        if section[9] != 12:
            raise ValueError('Invalid CSI origin relocation table')
        for pos in range(section[4], section[4] + section[5], 12):
            info = struct.unpack_from('<I', source, pos + 4)[0]
            if info >> 8 in bound_origins:
                struct.pack_into('<I', result, pos + 4,
                    (bound_origins[info >> 8] << 8) | (info & 255))
    for section_name, groups in SITES[target].items():
        code_index = by_name[section_name]
        rela = [s for s in sections if s[1] == 4 and s[7] == code_index and s[6] == table_index]
        if len(rela) != 1 or rela[0][9] != 12:
            raise ValueError('Invalid CSI relocation table')
        expected = {}
        for group, destination in zip(groups, (BEGIN, APPEND, COPIED)):
            original = 'wdev_csi_rx_process' if destination == COPIED else 'memcpy'
            for site in group:
                if target == 'esp32s3':
                    literal, call = site
                    expected[(literal, 1)] = (original, destination)
                    expected[(call, 11)] = (original, destination)
                else:
                    if body(sections[code_index])[site:site + 8] != bytes.fromhex('97000000e7800000'):
                        raise ValueError('Unreviewed CSI CALL instruction')
                    expected[(site, 18)] = (original, destination)
        found = set()
        for pos in range(rela[0][4], rela[0][4] + rela[0][5], 12):
            offset, info, addend = struct.unpack_from('<IIi', source, pos)
            key = (offset, info & 255)
            if key not in expected:
                continue
            original, destination = expected[key]
            if key in found or info >> 8 >= len(symbol_names) or symbol_names[info >> 8] != original or addend:
                raise ValueError('Unreviewed CSI relocation target')
            found.add(key)
            struct.pack_into('<I', result, pos + 4, (added[destination] << 8) | (info & 255))
        if found != set(expected):
            raise ValueError('Missing CSI copy/constructor boundary')
    for index, data in ((strings_index, strings), (table_index, symbols)):
        result += bytes((-len(result)) % max(1, sections[index][8]))
        sections[index][4:6] = [len(result), len(data)]
        result += data
        struct.pack_into('<10I', result, shoff + index * width, *sections[index])
    return bytes(result)


def patch_archive(source: bytes, target: str) -> bytes:
    if target not in ARCHIVES or hashlib.sha256(source).hexdigest() != ARCHIVES[target]:
        raise ValueError('Unreviewed CSI libpp.a: ' + target)
    return rewrite_archive(source, member_patches={'wdev.o': lambda data: patch_object(data, target)})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--target', choices=tuple(ARCHIVES), required=True)
    args = parser.parse_args()
    if args.source.resolve() == args.output.resolve():
        parser.error('CSI patch output must not replace its SDK source')
    result = patch_archive(args.source.read_bytes(), args.target)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    temporary = args.output.with_suffix('.tmp')
    temporary.write_bytes(result)
    temporary.replace(args.output)
    print('CSI RX copy receipts: ' + args.target + ', four reviewed native RX origins')


if __name__ == '__main__':
    main()
