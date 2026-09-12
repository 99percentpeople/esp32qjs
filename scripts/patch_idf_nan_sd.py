"""Track allocations from the pinned C5 NAN discovery and datapath objects.

Rename undefined boundaries and retarget reviewed same-object NDP calls while
retaining code/data and existing symbol indices. The common object hash gates
the five-word allocator ABI and EB metadata layout.
The parent validates the whole original archive and owns build-local output.
"""
import hashlib
import struct
from patch_idf_twt_probe_wake import patch_archive as rewrite_archive

REVIEWED = {
    'ieee80211_nan_sd.o': '298f91824b73a7045ac0acfba6ef0baab6c760a5744854b8eb8fbaf717c163b8',
    'ieee80211_nan_common.o': '27dad58562f3ad38d4f197f7d0337c96e072c13f215d7116f341cae6f63513c4',
    'ieee80211_nan_datapath.o': '87d666fcdaefbe96548c1f27cd6c5167b6602fec5a267727d79beaa06a271dea',
}
PAIRING_TX_REVIEWED = {
    # Pairing AUTH: copied request -> synchronous Wi-Fi-task ioctl -> output
    # -> NAN queue. Keep the unmodified producer/consumer ABI pinned too.
    'ieee80211_supplicant.o': '6ea6236c7c1fa63c38cc7c721256d1df478154d9e8cfecdc3b2956aae4993a50',
    'ieee80211_ioctl.o': '4c25bfc9472a6c5fa6267e6b94b2d781ddd64e90d557f82776f6f01650cd5b71',
    'ieee80211_output.o': 'ee92da6a1bc66af65cff6c7ac1490803161eb5f84aa0dac408971501224ac128',
}


def validate_pairing_tx_archive(source: bytes) -> None:
    # Run before the existing Vendor IE/FTM rewrites of these same members.
    # The parent still validates the whole original archive independently.
    def checked(data, member):
        if hashlib.sha256(data).hexdigest() != PAIRING_TX_REVIEWED[member]:
            raise ValueError('Unreviewed C5 NAN pairing TX member ' + member)
        return data
    rewrite_archive(source, member_patches={
        member: (lambda data, member=member: checked(data, member)) for member in PAIRING_TX_REVIEWED
    })

NDP_CALLS = {
    ('nan_dp_parse_ndpa', 0x66): ('nan_dp_alloc_ndp', 'esp32qjs_nan_ndp_alloc'),
    ('nan_datapath_send_req', 0xbe): ('nan_dp_alloc_ndp', 'esp32qjs_nan_ndp_alloc'),
    **{(name, offset): ('nan_dp_delete_peer', 'esp32qjs_nan_ndp_delete') for name, offset in [
        ('nan_ndp_setup_timeout_process', 0x86), ('nan_send_ndp_resp', 0x2fa),
        ('nan_parse_ndp_req', 0x2f8), ('nan_parse_ndp_req', 0x418),
        ('nan_parse_ndp_resp', 0x126), ('nan_parse_ndp_confirm', 0x316),
        ('nan_parse_ndp_security_install', 0x1ca), ('nan_parse_ndp_terminate', 0x4a),
        ('nan_naf_txcb', 0x20e), ('nan_datapath_send_req', 0x1f4),
        ('nan_datapath_send_resp', 0xc4), ('nan_dp_deinit', 0x46)]},
}

NDP_TIMER_CALLBACKS = {
    'nan_ndp_setup_timeout': 'esp32qjs_nan_setup_timer_callback',
    'nan_ndp_inactivity_handler': 'esp32qjs_nan_inactivity_timer_callback',
}


def patch_ndp_calls(source: bytes) -> bytes:
    """Retarget exact CALL/tail-CALL relocations; leave original definitions.

    Called after original-member validation and undefined-symbol renaming.
    No code or existing symbol index is rewritten; only selected call targets
    change to appended undefined hook symbols.
    """
    shoff = struct.unpack_from('<I', source, 32)[0]
    width, count, names_index = struct.unpack_from('<HHH', source, 46)
    sections = [list(struct.unpack_from('<10I', source, shoff + i * width)) for i in range(count)]
    def body(section):
        return source[section[4]:section[4] + section[5]]
    def name(strings, offset):
        return strings[offset:strings.index(b'\0', offset)].decode('ascii')
    names = body(sections[names_index])
    by_name = {name(names, s[0]): i for i, s in enumerate(sections)}
    table_index = next(i for i, s in enumerate(sections) if s[1] == 2)
    table = sections[table_index]
    strings_index = table[6]
    symbols = body(table)
    strings = body(sections[strings_index])
    symbol_names = [name(strings, struct.unpack_from('<I', symbols, i)[0]) for i in range(0, len(symbols), 16)]
    added = {}
    for original, target in NDP_CALLS.values():
        if target in added:
            continue
        if target in symbol_names:
            raise ValueError('NDP calls already prepared')
        added[target] = len(symbols) // 16
        symbols += struct.pack('<IIIBBH', len(strings), 0, 0, 0x12, 0, 0)
        strings += target.encode() + b'\0'
    # Export addresses of the two local callbacks for exact OSI classification.
    # Preserve their code, original local binding and every existing index.
    for original, target in NDP_TIMER_CALLBACKS.items():
        if symbol_names.count(original) != 1 or target in symbol_names:
            raise ValueError('Unexpected NDP timer callback symbol')
        row = list(struct.unpack_from('<IIIBBH', symbols, symbol_names.index(original) * 16))
        if row[1] or row[2] != 16 or row[3] != 2 or row[4] or not row[5]:
            raise ValueError('Unexpected NDP timer callback layout')
        row[0], row[3] = len(strings), 0x12
        strings += target.encode() + b'\0'
        symbols += struct.pack('<IIIBBH', *row)
    result = bytearray(source)
    for (function, offset), (original, target) in NDP_CALLS.items():
        code_index = by_name['.text.' + function]
        code = body(sections[code_index])
        first, second = struct.unpack_from('<II', code, offset)
        if first & 0x7f != 0x17 or second & 0x707f != 0x67:
            raise ValueError('Expected NDP AUIPC/JALR call pair')
        rows = [s for s in sections if s[1] == 4 and s[7] == code_index and s[6] == table_index]
        if len(rows) != 1 or rows[0][9] != 12:
            raise ValueError('Invalid NDP relocation table')
        found = []
        for pos in range(rows[0][4], rows[0][4] + rows[0][5], 12):
            address, info, addend = struct.unpack_from('<IIi', source, pos)
            if address == offset and info & 0xff == 18:
                if addend or symbol_names[info >> 8] != original:
                    raise ValueError('Unexpected NDP native call target')
                found.append(pos)
        if len(found) != 1:
            raise ValueError('Missing NDP call relocation')
        struct.pack_into('<I', result, found[0] + 4, (added[target] << 8) | 18)
    for index, contents in ((strings_index, strings), (table_index, symbols)):
        result += bytes((-len(result)) % max(1, sections[index][8]))
        sections[index][4:6] = [len(result), len(contents)]
        result += contents
        struct.pack_into('<10I', result, shoff + index * width, *sections[index])
    return bytes(result)


def patch_object(source: bytes, member: str) -> bytes:
    if member not in REVIEWED or hashlib.sha256(source).hexdigest() != REVIEWED[member]:
        raise ValueError('Unreviewed C5 NAN member ' + member)
    if member not in ('ieee80211_nan_sd.o', 'ieee80211_nan_datapath.o'):
        return source
    if source[:7] != b'\x7fELF\x01\x01\x01' or struct.unpack_from('<HH', source, 16) != (1, 243):
        raise ValueError('Expected reviewed ELF32 RISC-V object')
    shoff = struct.unpack_from('<I', source, 32)[0]
    width, count = struct.unpack_from('<HH', source, 46)
    if width != 40 or not count or shoff + count * width > len(source):
        raise ValueError('Invalid NAN ELF section table')
    sections = [list(struct.unpack_from('<10I', source, shoff + i * width)) for i in range(count)]
    tables = [section for section in sections if section[1] == 2]
    if len(tables) != 1 or tables[0][9] != 16 or tables[0][5] % 16:
        raise ValueError('Invalid NAN ELF symbol table')
    table = tables[0]
    index = table[6]
    if index >= count:
        raise ValueError('Invalid NAN ELF string table')
    string_section = sections[index]
    start, size = string_section[4:6]
    if start + size > len(source) or table[4] + table[5] > len(source):
        raise ValueError('Truncated NAN ELF data')
    strings = source[start:start + size]
    result = bytearray(source)
    allocator = b'nan_alloc_action' if member == 'ieee80211_nan_datapath.o' else b'nan_alloc_sdf'
    replacement = b'esp32qjs_wifi_nan_alloc_action' if member == 'ieee80211_nan_datapath.o' else b'esp32qjs_wifi_nan_alloc_sdf'
    replacements = {allocator: replacement}
    if member == 'ieee80211_nan_datapath.o':
        replacements[b'nan_get_peer_svc_record'] = b'esp32qjs_nan_ndp_peer_service'
    found = set()
    for offset in range(table[4], table[4] + table[5], 16):
        name, value, size, info, other, section = struct.unpack_from('<IIIBBH', source, offset)
        end = strings.index(b'\0', name)
        symbol = strings[name:end]
        if symbol not in replacements:
            continue
        if section or value or size or info >> 4 != 1 or other:
            raise ValueError('Expected one undefined NAN allocator symbol')
        if symbol in found:
            raise ValueError('Duplicate NAN boundary symbol')
        found.add(symbol)
        struct.pack_into('<I', result, offset, len(strings))
        strings += replacements[symbol] + b'\0'
    if found != set(replacements):
        raise ValueError('Missing or duplicate NAN allocator boundary')
    result += bytes((-len(result)) % max(1, string_section[8]))
    string_section[4:6] = [len(result), len(strings)]
    result += strings
    struct.pack_into('<10I', result, shoff + index * width, *string_section)
    return patch_ndp_calls(bytes(result)) if member == 'ieee80211_nan_datapath.o' else bytes(result)


def patch_archive(source: bytes, target: str) -> bytes:
    if target != 'esp32c5':
        raise ValueError('NAN-Sync buffer bridge requires the reviewed C5 implementation')
    return rewrite_archive(source, member_patches={
        member: (lambda data, member=member: patch_object(data, member)) for member in REVIEWED
    })
