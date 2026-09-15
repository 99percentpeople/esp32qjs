"""Pass the actual management EB at the pinned SDK TX-status call site.

Called after the parent archive hash gate. Keep every symbol/relocation index
and all allocated section offsets. Rename only reviewed undefined symbols;
change one equal-width argument instruction in the completion caller. The
archive rebuilder preserves its member order and repairs the existing index.
The CHM member's own resets invalidate both deadline tickets, including calls
within the object which would bypass a linker wrap of chm_end_op.
"""
import hashlib
import struct
from sdk_patches.common.archive import rewrite_archive

OBJECTS = {
    'esp32c3': ('f24dd5d8e98cedba24e6a84d9d312960a86a6f1eafec83e62e7dffd4b4e87db3',
                'bd56e238d6e54fc1ad41f80aa2f4637308b05865b0cfd6514384d4d306b000fb',
                '14cd5c6c5288884e684b6ec0475aa0a5e37457cfdff72321dd2a4b48c59eee6a'),
    'esp32s3': ('a66c59a2559dd3752194a66f0bc52cd5cbac4e94b814b13786a3525b5015e16a',
                '42e78e49e36e26566c63ddbd191f5a558b43ee8bff7c17f22bf34d33d6525bbe',
                '7cb45a8052d4c1600a85c1e635c3672957dffe23124f8a6e8653d1c2943b1678'),
    'esp32c5': ('22514c530bec18cddfb6473da83facc0f979adba1e827ee1b9529d76803a48e9',
                'ee92da6a1bc66af65cff6c7ac1490803161eb5f84aa0dac408971501224ac128',
                'fb451cf821378977e97491ff15f5234cd6fe90832c7ca482ff53006afcfd26c1'),
}
FRAME_ARGUMENT = {
    'esp32c3': (0x188, '0589', '4a85'),  # andi a0,1 -> c.mv a0,s2
    'esp32c5': (0x1a0, '0d89', '4a85'),  # andi a0,3 -> c.mv a0,s2
    'esp32s3': (0x1b5, 'a0a305', '20a220'),  # extui a10,19,1 -> or a10,a2,a2
}


def patch_object(source, target, member):
    members = ('wl_offchan.o', 'ieee80211_output.o', 'wl_chm.o')
    if target not in OBJECTS or member not in members:
        raise ValueError('Unreviewed off-channel frame target/member')
    output = member == 'ieee80211_output.o'
    if hashlib.sha256(source).hexdigest() != OBJECTS[target][members.index(member)]:
        raise ValueError('Unreviewed off-channel frame object ' + target + '/' + member)
    machine = 94 if target == 'esp32s3' else 243
    if source[:7] != b'\x7fELF\x01\x01\x01' or struct.unpack_from('<HH', source, 16) != (1, machine):
        raise ValueError('Expected reviewed ELF32 relocatable object')
    shoff = struct.unpack_from('<I', source, 32)[0]
    width, count, names_index = struct.unpack_from('<HHH', source, 46)
    if width != 40 or not count or names_index >= count or shoff + count * width > len(source):
        raise ValueError('Unreviewed off-channel section table')
    sections = [list(struct.unpack_from('<10I', source, shoff + i * width)) for i in range(count)]

    def body(section):
        start, size = section[4:6]
        if start + size > len(source):
            raise ValueError('Off-channel section exceeds object')
        return source[start:start + size]

    def string(strings, offset):
        return strings[offset:strings.index(b'\0', offset)].decode('ascii')

    names = body(sections[names_index])
    by_name = {string(names, section[0]): index for index, section in enumerate(sections)}
    tables = [section for section in sections if section[1] == 2]
    if len(tables) != 1 or tables[0][9] != 16 or tables[0][5] % 16:
        raise ValueError('Unreviewed off-channel symbol table')
    table = tables[0]
    strings_index = table[6]
    strings = body(sections[strings_index])
    symbols = body(table)
    renames = {'offchan_send_action_tx_status': 'esp32qjs_wifi_offchan_frame_done'} if output else {
        'memset': 'esp32qjs_wifi_offchan_record_reset',
        'ieee80211_post_hmac_tx': 'esp32qjs_wifi_offchan_frame_post',
        'wifi_event_post': 'esp32qjs_wifi_offchan_event_post'}
    if member == 'wl_chm.o':
        # Native end/cancel/zero-duration reset must invalidate both timer arms
        # before calling an owner which can synchronously start another op.
        renames = {'memset': 'esp32qjs_wifi_chm_record_reset'}
    result = bytearray(source)
    found = set()
    for index in range(len(symbols) // 16):
        offset, value, size, info, other, section_index = struct.unpack_from('<IIIBBH', symbols, index * 16)
        name = string(strings, offset)
        if name not in renames:
            continue
        if name in found or section_index != 0 or value or size or info >> 4 != 1:
            raise ValueError('Expected unique undefined SDK symbol ' + name)
        found.add(name)
        struct.pack_into('<I', result, table[4] + index * 16, len(strings))
        strings += renames[name].encode('ascii') + b'\0'
    if found != set(renames):
        raise ValueError('Missing off-channel SDK call boundary')
    if output:
        code = sections[by_name['.text.ieee80211_tx_mgt_cb']]
        offset, before, after = FRAME_ARGUMENT[target]
        before, after = bytes.fromhex(before), bytes.fromhex(after)
        if len(before) != len(after) or body(code)[offset:offset + len(before)] != before:
            raise ValueError('Unreviewed management frame argument instruction')
        result[code[4] + offset:code[4] + offset + len(before)] = after
    result += bytes((-len(result)) % max(1, sections[strings_index][8]))
    sections[strings_index][4:6] = [len(result), len(strings)]
    result += strings
    struct.pack_into('<10I', result, shoff + strings_index * width, *sections[strings_index])
    return bytes(result)


def patch_archive(source, target):
    return rewrite_archive(source, member_patches={
        member: (lambda data, member=member: patch_object(data, target, member))
        for member in ('wl_offchan.o', 'ieee80211_output.o', 'wl_chm.o')})
