"""Clear reviewed decoder local frames in immutable C3/S3/C5 SDK copies.

The parent validates the complete SDK archive before this transformer runs.
Local-frame ranges and every frame exit below come from that archive's actual
instructions. No extra stack, allocation, callback or mutable native state.
RISC-V exits retain their saved-register loads and tail-call arguments. Xtensa
keeps the upper 32 bytes used by the reviewed call8 window spill ABI.
"""
from __future__ import annotations

import hashlib
import struct

from sdk_patches.common.archive import rewrite_archive


# function: (section SHA256, local bytes, epilogue starts). Xtensa has one final
# retw.n per section; branches to it enter the appended cleanup by fallthrough.
REVIEWED = {
    'esp32c3': {
        'sc_get_ssid_passwd': ('934030bd90aacfbe80b601f5156cf50431e5f6a7c02ab3d43cb736e14e92f15c', 364, (0x61c,)),
        'sc_recv_completed': ('d2b13178611b84508b8294d53725668242f034310d07acef1fb7d62c26a95b3b', 52, (0x5e,)),
        'TOUCH_Deal_with': ('5127d5e43238b84e3cb1cd6f3784b4579dcb590ddbd76c374c2bd8b02a53ff7c', 172, (0x76, 0x168, 0xc90)),
        'KISS_Get_sequ_data': ('7b43222994e695f2e894966a533568cb798387992ae40781015f896ed2e307ae', 36, (0xd0,)),
        'KISS_Deal_with_ht20': ('2a5c559bbc471be17a17ac6fffe31f3c741b480de39f8fc2fbb323501a3999e7', 156, (0x70, 0x400, 0x84c)),
    },
    'esp32c5': {
        'sc_get_ssid_passwd': ('934030bd90aacfbe80b601f5156cf50431e5f6a7c02ab3d43cb736e14e92f15c', 364, (0x61c,)),
        'sc_recv_completed': ('7b50a23b52ec1c412d67211b70ef5d6b42009ebd75d6df64479110a99c3f495a', 52, (0x5e,)),
        'TOUCH_Deal_with': ('b0e51f55fd8bc0bca4127f8955d4916da628cde306dcf8dd6dd9b2aa62c09f98', 172, (0xa2, 0x19e, 0xcca)),
        'KISS_Get_sequ_data': ('78a737b980eced2a19c0b4a53a56111afffd5a9880403dc7e4b12906341c52d6', 36, (0xea,)),
        'KISS_Deal_with_ht20': ('a947c4d61b9e97dff0b97ae1047d70613d5b55a277e64cce09ed4a680a450952', 156, (0x70, 0x420, 0x886)),
    },
    'esp32s3': {
        'sc_get_ssid_passwd': ('92c198999ebe28e0ff6db53be93c8cefed29a4044fc52113d999af8eae40425d', 384, ()),
        'sc_recv_completed': ('c7cb1ee2af7667c1dad0f860ad38a8a91981021453a2f9a6c1f76b5ccc6db914', 64, ()),
        'TOUCH_Deal_with': ('db54bc5cd7e2c179e8a66f22ec412af2a738136b318741c5b6292755312d9f69', 192, ()),
        'KISS_Get_sequ_data': ('9c706da64a27bdd534f2a9fbab665084a6a2e0ab087059c7478cdb1fd3ab75b6', 16, ()),
        'KISS_Deal_with_ht20': ('8b2af20c48d05ba2bbe8ed1ff3c1be0ca6de7dd4b42dfe362eb08543367f362a', 160, ()),
    },
}
MEMBERS = {
    'sc_esptouch_v2.o': ('sc_get_ssid_passwd', 'sc_recv_completed'),
    'sc_esptouch.o': ('TOUCH_Deal_with',),
    'sc_airkiss.o': ('KISS_Get_sequ_data', 'KISS_Deal_with_ht20'),
}


def _riscv_clear(length: int) -> bytes:
    # addi t0,sp,0; addi t1,sp,length; sw zero,0(t0); addi t0,t0,4;
    # bltu t0,t1,-8. t0/t1 are caller-saved; arguments/results remain intact.
    if not 0 < length < 2048 or length % 4:
        raise ValueError('Invalid decoder local-frame length')
    return struct.pack('<5I', 0x00010293, (length << 20) | 0x10313,
                       0x0002a023, 0x00428293, 0xfe62ece3)


def _xtensa_clear(length: int) -> bytes:
    # Assembled with the actual S3 toolchain, no-transform. Only a8/a9/a10
    # change; a0/a1 and a2..a7 (return values) stay intact. The loop is local.
    # movi a8,0; mov.n a9,a1; movi a10,length; s32i.n a8,a9,0;
    # addi.n a9,a9,4; addi a10,a10,-4; bnez a10,loop; retw.n.
    if not 0 < length < 2048 or length % 4:
        raise ValueError('Invalid decoder local-frame length')
    immediate = bytes((0xa2, 0xa0 | (length >> 8), length & 255))
    return bytes.fromhex('82a0009d01') + immediate + bytes.fromhex('89094b99a2cafc565aff1df0')


def patch_object(source: bytes, target: str, functions: tuple[str, ...]) -> bytes:
    machine = 94 if target == 'esp32s3' else 243
    if target not in REVIEWED or source[:7] != b'\x7fELF\x01\x01\x01' or struct.unpack_from('<HH', source, 16) != (1, machine):
        raise ValueError('Unreviewed SmartConfig decoder object')
    shoff = struct.unpack_from('<I', source, 32)[0]
    width, count, names_index = struct.unpack_from('<HHH', source, 46)
    if width != 40 or not count or names_index >= count or shoff + count * width > len(source):
        raise ValueError('Invalid SmartConfig section table')
    sections = [list(struct.unpack_from('<10I', source, shoff + i * width)) for i in range(count)]

    def body(index):
        start, size = sections[index][4:6]
        if start + size > len(source):
            raise ValueError('SmartConfig section exceeds object')
        return source[start:start + size]

    names = body(names_index)
    by_name = {names[s[0]:names.index(b'\0', s[0])].decode(): i for i, s in enumerate(sections)}
    tables = [i for i, s in enumerate(sections) if s[1] == 2]
    if len(tables) != 1 or sections[tables[0]][9] != 16:
        raise ValueError('Invalid SmartConfig symbol table')
    table_index = tables[0]
    symbols = bytearray(body(table_index))
    strings_index = sections[table_index][6]
    strings = bytearray(body(strings_index))
    symbol_rows = [struct.unpack_from('<IIIBBH', symbols, i) for i in range(0, len(symbols), 16)]

    def label(name, section, address):
        index = len(symbols) // 16
        # Appended hidden globals preserve the existing local/global split.
        # Named section-relative values are adjusted by RISC-V relaxation;
        # a section-symbol-plus-addend jump does not track deleted call bytes.
        symbols.extend(struct.pack('<IIIBBH', len(strings), address, 0, 0x10, 2, section))
        strings.extend(name.encode('ascii') + b'\0')
        return index

    changes = {}
    for function in functions:
        expected, length, sites = REVIEWED[target][function]
        code_index = by_name['.text.' + function]
        original = body(code_index)
        if hashlib.sha256(original).hexdigest() != expected:
            raise ValueError('Unreviewed SmartConfig local-frame function: ' + function)
        section_symbols = [i for i, row in enumerate(symbol_rows) if row[3] & 15 == 3 and row[5] == code_index and row[1] == 0]
        relocations = [i for i, s in enumerate(sections) if s[1] == 4 and s[7] == code_index and s[6] == table_index]
        if len(section_symbols) != 1 or len(relocations) != 1 or sections[relocations[0]][9] != 12:
            raise ValueError('Missing decoder section symbol/relocations')
        section_symbol, relocation_index = section_symbols[0], relocations[0]
        relocation = bytearray(body(relocation_index))
        code = bytearray(original)
        if target == 'esp32s3':
            if code[-2:] != bytes.fromhex('1df0'):
                raise ValueError('Expected final decoder retw.n')
            code[-2:] = bytes.fromhex('3df0')  # nop.n: all old exits fall through
            start = len(code)
            code += _xtensa_clear(length)
            # The loop branch must remain correct if Xtensa linking relaxes
            # preceding code. Existing section/symbol indices remain stable.
            relocation += struct.pack('<IIi', start + 15, (section_symbol << 8) | 20, start + 8)
        else:
            for site in sites:
                for index, s in enumerate(sections):
                    if s[1] != 4 or s[6] != table_index:
                        continue
                    for pos in range(0, s[5], 12):
                        address, info, addend = struct.unpack_from('<IIi', body(index), pos)
                        if s[7] == code_index and site <= address < site + 4:
                            raise ValueError('Relocation overwrites decoder exit')
                        row = symbol_rows[info >> 8]
                        if row[5] == code_index and site < row[1] + addend < site + 4:
                            raise ValueError('Relocation enters a displaced decoder instruction')
                start = len(code)
                displaced = code[site:site + 4]
                code[site:site + 4] = struct.pack('<I', 0x6f)  # jal zero,cleanup
                code += _riscv_clear(length) + displaced + struct.pack('<I', 0x6f)
                suffix = function + '_' + format(site, 'x')
                enter = label('__esp32qjs_sc_clear_' + suffix, code_index, start)
                resume = label('__esp32qjs_sc_resume_' + suffix, code_index, site + 4)
                relocation += struct.pack('<IIi', site, (enter << 8) | 17, 0)
                relocation += struct.pack('<IIi', start + 24, (resume << 8) | 17, 0)
        changes[code_index] = code
        changes[relocation_index] = relocation
        for i, row in enumerate(symbol_rows):
            if row[3] & 15 == 2 and row[5] == code_index and row[1] + row[2] == len(original):
                struct.pack_into('<I', symbols, i * 16 + 8, len(code) - row[1])
    changes[table_index] = symbols
    changes[strings_index] = strings
    result = bytearray(source)
    # Keep old symbol indices, literal pools, branch targets and section indices.
    # The GNU archive rebuilder updates member offsets after these bodies grow.
    for index, contents in changes.items():
        result += bytes((-len(result)) % max(1, sections[index][8]))
        sections[index][4:6] = [len(result), len(contents)]
        result += contents
        struct.pack_into('<10I', result, shoff + index * width, *sections[index])
    return bytes(result)


def patch_archive(source: bytes, target: str) -> bytes:
    if target not in REVIEWED:
        raise ValueError('Unreviewed SmartConfig stack target')
    patches = {member: (lambda data, functions=functions: patch_object(data, target, functions))
               for member, functions in MEMBERS.items()}
    return rewrite_archive(source, member_patches=patches)
