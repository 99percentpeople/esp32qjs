"""Repair the pinned SDK rate validator's init-before-START PHY read.

Only replace its interface-object/PHY read with the framework validation context.
The bridge supplies stopped protocols and a separately validated LR selector;
the native writer, other functions and instruction offsets stay intact. A
separate build archive preserves the shared SDK and earlier member patches.
"""
from __future__ import annotations
import argparse
import hashlib
from pathlib import Path
import struct

from patch_idf_twt_probe_wake import patch_archive as rewrite_archive

OBJECTS = {
    'esp32c3': '9176f6604fac2b33f0c9f7ff1f3f053f83daadb3e6c91dd5a8e2cc03cd66d02e',
    'esp32s3': '3f1442eba2b182786179ba7cf0d9eb0787fa7fd9dae4305548efd4078de6cc8a',
    'esp32c5': '86ee438f6c15297d483f8c3f3a787013dff7e57a3c0b2b7262a36ebeb5cf126d',
}
HELPER = 'esp32qjs_wifi_tx_rate_context'
FUNCTION = '.text.esp_wifi_config_80211_tx'
# RISC-V: mv a0,s1; mv a1,s0; call helper; li a5,31;
# bltu a5,a0,original_exit; andi s2,a0,7; srli a0,a0,3; three nop.
# Band and PHY come from one coherent validation context.
# The lbu at 0x90 becomes mv a3,s2, using the returned scalar PHY.
RISCV_EDITS = {
    0x6e: bytes.fromhex('2685a28597000000e7800000fd47e3e0a7fc137975000d81010001000100'),
    0x90: bytes.fromhex('93060900'),
}
# Xtensa: pass interface/config; l32r/callx8 helper; preserve its error or
# unpack its PHY/band into a4/a10. Reload requested PHY into a8.
XTENSA_EDITS = {
    0x9d: bytes.fromhex('ad0230b32081dcffe008008d0ab6c802c65c0080402480a341822300'),
    0xbc: bytes.fromhex('40b420'),
    0x11b: bytes.fromhex('409420'),
    0x148: bytes.fromhex('40b420'),
}


def patch_object(source: bytes, target: str) -> bytes:
    if target not in OBJECTS or hashlib.sha256(source).hexdigest() != OBJECTS[target]:
        raise ValueError('Unreviewed TX rate ieee80211_api.o: ' + target)
    if source[:7] != b'\x7fELF\x01\x01\x01':
        raise ValueError('Expected ELF32 little-endian SDK object')
    shoff = struct.unpack_from('<I', source, 32)[0]
    width, count, names_index = struct.unpack_from('<HHH', source, 46)
    if width != 40:
        raise ValueError('Unreviewed TX rate section table')
    sections = [list(struct.unpack_from('<10I', source, shoff+i*width)) for i in range(count)]
    def body(index):
        section = sections[index]
        return source[section[4]:section[4]+section[5]]
    def string(strings, offset):
        return strings[offset:strings.index(b'\0', offset)].decode('ascii')
    names = body(names_index)
    by_name = {string(names, section[0]): i for i, section in enumerate(sections)}
    table_index = next(i for i, section in enumerate(sections) if section[1] == 2)
    strings_index = sections[table_index][6]
    strings, symbols = body(strings_index), body(table_index)
    entries = [struct.unpack_from('<IIIBBH', symbols, i) for i in range(0,len(symbols),16)]
    symbol_names = [string(strings, entry[0]) for entry in entries]
    helper_index = len(entries)
    if HELPER in symbol_names:
        raise ValueError('TX rate object is already patched')
    symbols += struct.pack('<IIIBBH',len(strings),0,0,0x12,0,0)
    strings += HELPER.encode('ascii')+b'\0'
    code_index = by_name[FUNCTION]
    rela_index = next(i for i,s in enumerate(sections) if s[1] == 4 and s[7] == code_index)
    old = [struct.unpack_from('<IIi',body(rela_index),i) for i in range(0,sections[rela_index][5],12)]
    result = bytearray(source)
    edits = XTENSA_EDITS if target == 'esp32s3' else RISCV_EDITS
    for offset, value in edits.items():
        start = sections[code_index][4]+offset
        result[start:start+len(value)] = value
    if target == 'esp32s3':
        section_symbol = next(i for i,e in enumerate(entries) if e[3] == 3 and e[5] == code_index)
        # Reuse the original g_ic literal exclusively used by this function;
        # update both actual literal relocations and Xtensa relaxation hints.
        new = [r for r in old if not (r[0] == 0x14 or 0x9d <= r[0] < 0xb9)]
        new += [(0x14,(helper_index<<8)|1,0),
                (0xa2,(section_symbol<<8)|20,0x14),
                (0xa2,(helper_index<<8)|11,0),
                (0xaa,(section_symbol<<8)|20,0xb0),
                (0xad,(section_symbol<<8)|20,0x224)]
    else:
        exit_info = next(info for offset,info,addend in old if offset == 0x80 and info&255 == 16)
        new = [r for r in old if not 0x6e <= r[0] < 0x8c]
        new += [(0x72,(helper_index<<8)|18,0),(0x72,51,0),(0x7c,exit_info,0)]
    relocs = b''.join(struct.pack('<IIi',*r) for r in sorted(new,key=lambda r:r[0]))
    for index,value in ((strings_index,strings),(table_index,symbols),(rela_index,relocs)):
        result += bytes((-len(result)) % max(1,sections[index][8]))
        sections[index][4:6] = [len(result),len(value)]
        result += value
        struct.pack_into('<10I',result,shoff+index*width,*sections[index])
    return bytes(result)


def patch_archive(source: bytes, target: str) -> bytes:
    return rewrite_archive(source,member_patches={'ieee80211_api.o':lambda data:patch_object(data,target)})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--target',choices=tuple(OBJECTS),required=True)
    args = parser.parse_args()
    if args.source.resolve() == args.output.resolve():
        parser.error('TX rate output must be a separate build archive')
    output = patch_archive(args.source.read_bytes(),args.target)
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_bytes(output)
    print('TX rate stopped PHY validation: '+args.target)


if __name__ == '__main__':
    main()
