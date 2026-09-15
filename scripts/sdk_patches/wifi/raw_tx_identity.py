"""Attach a native identity to the pinned SDK Raw TX descriptor lifecycle.

Redirect Raw TX allocation, the HMAC cache descriptor-copy call, and Raw TX
completion-info construction. No RF bytes, frame validation or scheduling are
changed. The cache hook transfers only an existing Raw TX binding before the
old descriptor is recycled. Completion uses the final descriptor identity,
independent of packet contents, sequence rewriting or callback order.
"""
from __future__ import annotations
import argparse
import hashlib
from pathlib import Path
import struct
from sdk_patches.common.archive import rewrite_archive

# Inputs produced by the current management patch, with/without off-channel repair.
HASHES = {
    'esp32c3': {
        'ieee80211.o': [
            '83420e3931b7df0ba3643833042ffa349592c6b2b38605f8a584a38e2ffb488f'
        ],
        'ieee80211_output.o': [
            '11475173c984c58f3b5b2aa2bc3ddca20a642adfe02b4c4c534bbbe957abe81d',
            '69411d83b61f3e1eef819e8b39b5c8f40e71fd238681f3623f7b9e53e830ab67'
        ]
    },
    'esp32c5': {
        'ieee80211.o': [
            '7179778fc6bf1d20144ef24c821acf1eeb14e81a24ff39c5aa3d100a059764f6'
        ],
        'ieee80211_output.o': [
            '2bf0f8ca63db4427b00501078f9c05088a9ae0a8a74b90cbbaf757af4bdfa097',
            'a3ac7976b388b82153677ce7f833c9d17d12f2e2c1cadbfe37dae167bd83e86f'
        ]
    },
    'esp32s3': {
        'ieee80211.o': [
            '6e6ebe3d8b04c0db1332b1e78492e5a019ff6206cd97abf48692b2825bbedfba'
        ],
        'ieee80211_output.o': [
            'c6ecb35dbf6e1ffe3f64a6fa1640ddf2facc56bb586203f62c06dcfd9a50dce9',
            '6a9c2e054c4468e07a051bc46372f278e44bb9eaf258fb97eca3d079029a4b63'
        ]
    }
}
# ppProcTxDone invokes descriptor callbacks before recycling. Guard that other
# archive too: net80211 alone cannot establish the descriptor lifetime.
PP_HASHES = {
    'esp32c3': '248eb9e648d224dad1c22adc4c7870109f03af04dbdc43306e29c03a7d01e215',
    'esp32c5': '104bb7f94ecca8339a61fb9b682828e281c00fafdbea6a8c8ad37212b49bede5',
    'esp32s3': 'c53d962a8b87e6da1a22a80b12af18f86e46998f409c9f9c4a1a62e126775725',
}
# Descriptor status names also depend on the reviewed TX-info writers. The
# LMAC state-machine byte at the same offset belongs to a different structure.
LMAC_HASHES = {
    'esp32c3': 'c2aaca7c0905cd7ea26bf2c9730bf9d6461474c62ab2c7a1d049947994972472',
    'esp32s3': 'dfa15aeaa934655ac82da7af4ae4552c67d8b05d3879a4730b7b24fd3a9b0306',
    'esp32c5': 'e4bb60c6af41febc510dad605700a55f35fb5a68877127a654ea824fcea00845',
}
# Reviewed API variants: original, TX-rate, FTM, and FTM + TX-rate. All retain
# the same ROM function-table initialization relocations.
API_HASHES = {
    'esp32c3': [
        '9176f6604fac2b33f0c9f7ff1f3f053f83daadb3e6c91dd5a8e2cc03cd66d02e',
        '4a9a21a409dd553adcc11bc3edd637485047961f02f0ffa9f17f2525272b8564',
        'e55528e3087c15a78f68e242206b66cb85fb3973ac6dc21b4dcef2937fad1fe9',
        '3add6e4a1c60e74c2cf6eaaded6f0bc6bc818bbd75e5e6ecf5ebf3b9033b0794'
    ],
    'esp32s3': [
        '3f1442eba2b182786179ba7cf0d9eb0787fa7fd9dae4305548efd4078de6cc8a',
        'c582f2b218c0c30ee8ee98e81a96bb15f6b81f49645d1b1fe3fe66cd5d79abd6',
        '367b4d2891d46e55df7d54b84d0d79b10b9737f81074d1359c700542fa0f1f60',
        '730feb83a082ed297388a05d159fdc431f0de5dc2ee3ffef942d823a311974df'
    ],
    'esp32c5': [
        '86ee438f6c15297d483f8c3f3a787013dff7e57a3c0b2b7262a36ebeb5cf126d',
        'cf751840fead589f6b992b3fd66d37910077d154e6f612acfa645cab3fb3e190',
        'dab78e77911afd49d214ae946d043df97cf63fa74c18b741eb9be11c54a03664',
        'a226174bdb2fd1e268c157da9001a682f4745682a1b4c665dd299f9ab2c09e3a'
    ]
}
for _target, _hashes in API_HASHES.items():
    HASHES[_target]['ieee80211_api.o'] = _hashes
OUTPUT_PROCESS = 'esp32qjs_raw_tx_output_process'


def verify_pp(source: bytes, target: str) -> None:
    def verify(data, name, hashes):
        if hashlib.sha256(data).hexdigest() != hashes.get(target):
            raise ValueError('Unreviewed Raw TX completion object: '+target+'/'+name)
        return data
    rewrite_archive(source, member_patches={
        'pp.o': lambda data: verify(data, 'pp.o', PP_HASHES),
        'lmac.o': lambda data: verify(data, 'lmac.o', LMAC_HASHES),
    })


HOOKS = {
    'ieee80211_output.o': ('esp_wifi_80211_tx', 'ic_ebuf_alloc', 'esp32qjs_raw_tx_alloc'),
    'ieee80211.o': ('ieee80211_freedom_inside_cb', 'ieee80211_get_tx_info_from_eb', 'esp32qjs_raw_tx_info'),
    'ieee80211_api.o': ('net80211_funcs_init', 'ieee80211_output_process', OUTPUT_PROCESS),
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
    expected = {'esp32c3': [0x40] if member == 'ieee80211_output.o' else [0x26],
                'esp32c5': [0x42] if member == 'ieee80211_output.o' else [0x26],
                'esp32s3': [0x18, 0x62] if member == 'ieee80211_output.o' else [0x8, 0x2a]}
    if member == 'ieee80211_api.o':
        expected = {'esp32c3': [0x8c, 0x90], 'esp32c5': [0x8c, 0x90], 'esp32s3': [0x20]}
    hooks = [('.text.'+function, original, replacement, expected[target])]
    if member == 'ieee80211_output.o':
        # This pinned section includes Xtensa literals before the function.
        # output_process copies the cache EB into a new TX EB, then immediately
        # recycles the old EB. Binding must move at this exact call boundary.
        hooks.append(('.wifi0iram.8', 'ieee80211_copy_eb_header', 'esp32qjs_raw_tx_copy_header',
                      [0x4c, 0x13e] if target == 'esp32s3' else [0x108]))
    changes = []
    symbol_table_index = None
    for section_name, original, replacement, expected_offsets in hooks:
        edits = []
        for rel in sections:
            if rel[1] != 4 or section_names[rel[7]] != section_name:
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
        if sorted(e[1] for e in edits) != expected_offsets or symbol_table_index is None:
            raise ValueError('Unexpected Raw TX call relocation set: '+section_name)
        changes.append((replacement, edits))
    table = sections[symbol_table_index]
    strings_section = sections[table[6]]
    strings = source[strings_section[4]:strings_section[4]+strings_section[5]]
    symbols = source[table[4]:table[4]+table[5]]
    if table[9] != 16:
        raise ValueError('Unexpected symbol width')
    new_index = len(symbols)//16
    new_symbols = bytearray(symbols)
    new_strings = bytearray(strings)
    for replacement, _ in changes:
        new_symbols.extend(struct.pack('<IIIBBH', len(new_strings), 0, 0, 0x12, 0, 0))
        new_strings.extend(replacement.encode()+b'\0')
    result = bytearray(source)
    if member == 'ieee80211_output.o':
        symbol_names = [strings[n:strings.index(b'\0', n)].decode()
                        for n in (struct.unpack_from('<I', symbols, i)[0] for i in range(0, len(symbols), 16))]
        if symbol_names.count('ieee80211_output_process') != 1 or OUTPUT_PROCESS in symbol_names:
            raise ValueError('Unreviewed Raw TX output origin')
        original_index = symbol_names.index('ieee80211_output_process')
        _, value, size, info, other, section = struct.unpack_from('<IIIBBH', symbols, original_index*16)
        if info != 0x12 or other != 0 or not size or section >= len(sections) or \
                section_names[section] != '.wifi0iram.8' or value+size > sections[section][5]:
            raise ValueError('Unreviewed Raw TX output definition')
        # A hidden definition survives the ROM linker assignment to the SDK
        # name. Bind output_init's registration to it; api.o's ROM dispatch
        # table also points to this origin via the separate reviewed hook.
        alias_index = len(new_symbols)//16
        new_symbols.extend(struct.pack('<IIIBBH', len(new_strings), value, size, info, 2, section))
        new_strings.extend(OUTPUT_PROCESS.encode()+b'\0')
        for rel in sections:
            if rel[1] != 4 or rel[6] != symbol_table_index:
                continue
            for at in range(rel[4], rel[4]+rel[5], rel[9]):
                relocation = struct.unpack_from('<I', source, at+4)[0]
                if relocation >> 8 == original_index:
                    struct.pack_into('<I', result, at+4, (alias_index << 8) | (relocation & 255))
    result.extend(b'\0' * (-len(result) % 4))
    table[4] = len(result)
    result.extend(new_symbols)
    table[5] = len(new_symbols)
    strings_section[4] = len(result)
    result.extend(new_strings)
    strings_section[5] = len(new_strings)
    for i, section in enumerate(sections):
        struct.pack_into('<10I', result, shoff+i*width, *section)
    for i, (_, edits) in enumerate(changes):
        for at, _, kind in edits:
            struct.pack_into('<I', result, at+4, ((new_index+i) << 8) | kind)
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
