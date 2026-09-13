"""Attach a native identity to the pinned SDK Raw TX descriptor lifecycle.

Redirect only Raw TX allocation and Raw TX completion-info construction. No RF
bytes, frame validation, scheduling or unrelated callback paths are changed.
The allocation hook binds the returned descriptor before HMAC enqueue. The
completion hook receives that same descriptor before SDK recycle, independent
of packet contents, sequence rewriting or callback order.
"""
from __future__ import annotations
import argparse
import hashlib
from pathlib import Path
import struct
from patch_idf_twt_probe_wake import patch_archive as rewrite_archive

HASHES = {'esp32c3': {'ieee80211.o': ['83420e3931b7df0ba3643833042ffa349592c6b2b38605f8a584a38e2ffb488f'], 'ieee80211_output.o': ['b33a6b13f6df5314a0743deb08473e6b9599b1eb121ac85f41aa41595572d268', 'acb07e76d2593320ac1a2d4310382368c827c7a0237e8dd9bb417ce6bb437b8d']}, 'esp32c5': {'ieee80211.o': ['7179778fc6bf1d20144ef24c821acf1eeb14e81a24ff39c5aa3d100a059764f6'], 'ieee80211_output.o': ['3474a017e151a09be6d578796bb167cc277f2ecc40b07c6dc37e618a76095b00', '3cd8190b34f939750652a6ff2836b57801b48ddb24518c8be0d4d0a1c3297b79']}, 'esp32s3': {'ieee80211.o': ['6e6ebe3d8b04c0db1332b1e78492e5a019ff6206cd97abf48692b2825bbedfba'], 'ieee80211_output.o': ['eacbdfa4d6f46c145a7783334c56f3d8ca6e1768e777e562d8aba585f788b566', '028b16bb3751b58d2e8103278dee54406f6a989b963aa34b4700d0ecb6757712']}}
# ppProcTxDone invokes descriptor callbacks before recycling. Guard that other
# archive too: net80211 alone cannot establish the descriptor lifetime.
PP_HASHES = {
    'esp32c3': '248eb9e648d224dad1c22adc4c7870109f03af04dbdc43306e29c03a7d01e215',
    'esp32c5': '104bb7f94ecca8339a61fb9b682828e281c00fafdbea6a8c8ad37212b49bede5',
    'esp32s3': 'c53d962a8b87e6da1a22a80b12af18f86e46998f409c9f9c4a1a62e126775725',
}


def verify_pp(source: bytes, target: str) -> None:
    def verify(member):
        if hashlib.sha256(member).hexdigest() != PP_HASHES.get(target):
            raise ValueError('Unreviewed Raw TX descriptor recycle lifetime: '+target+'/pp.o')
        return member
    rewrite_archive(source, member_patches={'pp.o': verify})


HOOKS = {
    'ieee80211_output.o': ('esp_wifi_80211_tx', 'ic_ebuf_alloc', 'esp32qjs_raw_tx_alloc'),
    'ieee80211.o': ('ieee80211_freedom_inside_cb', 'ieee80211_get_tx_info_from_eb', 'esp32qjs_raw_tx_info'),
}


def patch_object(source: bytes, target: str, member: str) -> bytes:
    if hashlib.sha256(source).hexdigest() not in HASHES.get(target, {}).get(member, []):
        raise ValueError('Unreviewed Raw TX identity object: '+target+'/'+member)
    shoff = struct.unpack_from('<I', source, 32)[0]
    width, count, names_index = struct.unpack_from('<HHH', source, 46)
    if width != 40:
        raise ValueError('Expected ELF32')
    sections = [list(struct.unpack_from('<10I', source, shoff+i*width)) for i in range(count)]
    names_section = sections[names_index]
    names = source[names_section[4]:names_section[4]+names_section[5]]
    section_names = [names[s[0]:names.index(b'\0', s[0])].decode() for s in sections]
    function, original, replacement = HOOKS[member]
    edits = []
    symbol_table_index = None
    for rel in sections:
        if rel[1] != 4 or section_names[rel[7]] != '.text.'+function:
            continue
        table = sections[rel[6]]
        strings_section = sections[table[6]]
        strings = source[strings_section[4]:strings_section[4]+strings_section[5]]
        for at in range(rel[4], rel[4]+rel[5], rel[9]):
            offset, info = struct.unpack_from('<II', source, at)
            name_index = struct.unpack_from('<I', source, table[4]+(info >> 8)*table[9])[0]
            name = strings[name_index:strings.index(b'\0', name_index)].decode()
            if name == original:
                if symbol_table_index is not None and symbol_table_index != rel[6]:
                    raise ValueError('Multiple symbol tables')
                symbol_table_index = rel[6]
                edits.append((at, offset, info & 255))
    expected = {'esp32c3': [0x40] if member == 'ieee80211_output.o' else [0x26],
                'esp32c5': [0x42] if member == 'ieee80211_output.o' else [0x26],
                'esp32s3': [0x18, 0x62] if member == 'ieee80211_output.o' else [0x8, 0x2a]}
    if sorted(e[1] for e in edits) != expected[target] or symbol_table_index is None:
        raise ValueError('Unexpected Raw TX call relocation set')
    table = sections[symbol_table_index]
    strings_section = sections[table[6]]
    strings = source[strings_section[4]:strings_section[4]+strings_section[5]]
    symbols = source[table[4]:table[4]+table[5]]
    if table[9] != 16:
        raise ValueError('Unexpected symbol width')
    new_index = len(symbols)//16
    result = bytearray(source)
    result.extend(b'\0' * (-len(result) % 4))
    table[4] = len(result)
    result.extend(symbols)
    result.extend(struct.pack('<IIIBBH', len(strings), 0, 0, 0x12, 0, 0))
    table[5] += 16
    strings_section[4] = len(result)
    result.extend(strings + replacement.encode() + b'\0')
    strings_section[5] += len(replacement)+1
    for i, section in enumerate(sections):
        struct.pack_into('<10I', result, shoff+i*width, *section)
    for at, _, kind in edits:
        struct.pack_into('<I', result, at+4, (new_index << 8) | kind)
    return bytes(result)


def patch_archive(source: bytes, target: str) -> bytes:
    return rewrite_archive(source, member_patches={name: (lambda data, name=name:
        patch_object(data, target, name)) for name in HOOKS})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--pp-source', type=Path, required=True)
    parser.add_argument('--target', choices=tuple(HASHES), required=True)
    args = parser.parse_args()
    if args.source.resolve() == args.output.resolve():
        parser.error('Output must be a separate build archive')
    verify_pp(args.pp_source.read_bytes(), args.target)
    result = patch_archive(args.source.read_bytes(), args.target)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(result)
    print('Raw TX descriptor identity hooks: '+args.target)

if __name__ == '__main__':
    main()
