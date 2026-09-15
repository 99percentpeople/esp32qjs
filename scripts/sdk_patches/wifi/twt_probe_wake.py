"""Retarget reviewed C5 probe, teardown and information PM calls without code/layout changes.

Called only after the parent patch validates the entire original SDK archive.
Append undefined ELF symbols and retarget exact CALL relocations. Existing
symbols, code/data sections, and every other relocation keep their identities.
Rebuild GNU archive offsets because selected members grow; never touch shared IDF.
"""
from __future__ import annotations
import struct
from sdk_patches.common.archive import rewrite_archive

UP = 'esp32_mquickjs_wifi_twt_probe_wake_up_native'
DONE = 'esp32_mquickjs_wifi_twt_probe_wake_done_native'
SITES = {
    'ieee80211_ioctl.o': [('wifi_sta_itwt_send_probe_req_process', 0x4a, 'pm_wake_up', UP),
                        ('wifi_sta_itwt_send_probe_req_process', 0x9c, 'pm_wake_done', DONE)],
    'ieee80211_twt.o': [('ieee80211_itwt_information', 0x160, 'pm_twt_wake_up', 'esp32_mquickjs_wifi_twt_information_wake_up_native'),
                      ('ieee80211_itwt_teardown', 0x58, 'pm_twt_wake_up', 'esp32_mquickjs_wifi_twt_teardown_wake_up_native'),
                      ('he_twt_teardown_txcb', 0x28, 'pm_twt_wake_done', 'esp32_mquickjs_wifi_twt_teardown_wake_done_native'),
                      ('itwt_stop_process', 0x6e, 'pm_wake_done', DONE),
                      ('itwt_probe_timeout_fn_process', 0x9a, 'pm_wake_done', DONE),
                      ('itwt_probe_rc_tx_cb', 0xea, 'pm_wake_done', DONE)],
    'ieee80211_btwt.o': [('ieee80211_btwt_teardown', 0x5a, 'pm_twt_wake_up', 'esp32_mquickjs_wifi_twt_teardown_wake_up_native')],
    'ieee80211_sta.o': [('sta_recv_mgmt', 0x2cc, 'pm_wake_done', DONE)],
}


def patch_object(source: bytes, sites: list[tuple[str, int, str, str]]) -> bytes:
    if source[:7] != b'\x7fELF\x01\x01\x01' or struct.unpack_from('<HH', source, 16) != (1, 243):
        raise ValueError('Expected reviewed ELF32 little-endian RISC-V relocatable object')
    shoff = struct.unpack_from('<I', source, 32)[0]
    size, count, names_index = struct.unpack_from('<HHH', source, 46)
    if size != 40 or count == 0 or names_index >= count or shoff + size * count > len(source):
        raise ValueError('Unreviewed ELF section table')
    sections = [list(struct.unpack_from('<10I', source, shoff + size * i)) for i in range(count)]

    def body(section):
        start, length = section[4:6]
        if start + length > len(source):
            raise ValueError('ELF section exceeds object')
        return source[start:start + length]

    def name(strings, offset):
        return strings[offset:strings.index(b'\0', offset)].decode('ascii')

    section_names = body(sections[names_index])
    by_name = {name(section_names, section[0]): i for i, section in enumerate(sections)}
    tables = [i for i, section in enumerate(sections) if section[1] == 2]
    if len(tables) != 1:
        raise ValueError('Expected one ELF symbol table')
    table_index = tables[0]
    table = sections[table_index]
    if table[9] != 16 or table[5] % 16 or table[6] >= count:
        raise ValueError('Unreviewed ELF symbol table')
    strings_index = table[6]
    strings = body(sections[strings_index])
    symbols = body(table)
    symbol_names = [name(strings, struct.unpack_from('<I', symbols, i)[0]) for i in range(0, len(symbols), 16)]
    added = {}
    for _, _, _, target in sites:
        if target in added:
            continue
        if target in symbol_names:
            raise ValueError('Probe PM object already patched')
        added[target] = len(symbols) // 16
        symbols += struct.pack('<IIIBBH', len(strings), 0, 0, 0x12, 0, 0)  # GLOBAL FUNC UND
        strings += target.encode('ascii') + b'\0'
    result = bytearray(source)
    for function, offset, original, target in sites:
        code_index = by_name.get('.text.' + function)
        if code_index is None or offset + 8 > sections[code_index][5]:
            raise ValueError('Missing reviewed probe function ' + function)
        code = body(sections[code_index])
        if code[offset:offset + 8] != bytes.fromhex('97000000e7800000'):
            raise ValueError('Expected reviewed unrelocated CALL instruction pair')
        relocations = [s for s in sections if s[1] == 4 and s[7] == code_index and s[6] == table_index]
        if len(relocations) != 1 or relocations[0][9] != 12:
            raise ValueError('Expected one function RELA table')
        relocation = relocations[0]
        matches = []
        for pos in range(relocation[4], relocation[4] + relocation[5], 12):
            address, info, addend = struct.unpack_from('<IIi', source, pos)
            if address == offset and info & 0xff == 18:  # R_RISCV_CALL
                if info >> 8 >= len(symbol_names) or symbol_names[info >> 8] != original or addend != 0:
                    raise ValueError('Unreviewed probe PM relocation target')
                matches.append(pos)
        if len(matches) != 1:
            raise ValueError('Expected exactly one probe PM CALL relocation')
        struct.pack_into('<I', result, matches[0] + 4, (added[target] << 8) | 18)
    # Orphan original non-alloc table bytes are retained; all old indices stay
    # stable, including debug and relaxation relocations. Only the section
    # headers for these appended tables change offset/size.
    for index, contents in [(strings_index, strings), (table_index, symbols)]:
        result += bytes((-len(result)) % max(1, sections[index][8]))
        sections[index][4:6] = [len(result), len(contents)]
        result += contents
        struct.pack_into('<10I', result, shoff + size * index, *sections[index])
    return bytes(result)


def patch_archive(source: bytes) -> bytes:
    return rewrite_archive(source, member_patches={
        name: (lambda data, sites=sites: patch_object(data, sites))
        for name, sites in SITES.items()
    })
