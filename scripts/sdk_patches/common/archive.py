"""Rewrite selected GNU archive members while retaining the symbol index."""
import struct


def rewrite_archive(source: bytes, *, member_patches) -> bytes:
    if not source.startswith(b'!<arch>\n'):
        raise ValueError('Expected GNU archive')
    members = []
    position = 8
    long_names = b''
    while position < len(source):
        header = source[position:position + 60]
        if len(header) != 60 or header[58:] != b'`\n':
            raise ValueError('Invalid GNU archive member header')
        length = int(header[48:58])
        data = source[position + 60:position + 60 + length]
        if len(data) != length:
            raise ValueError('Truncated GNU archive member')
        raw_name = header[:16].rstrip(b' ').decode('ascii')
        if raw_name == '//':
            long_names = data
        members.append([position, header, data, raw_name])
        position += 60 + length + (length & 1)
    if position != len(source):
        raise ValueError('Unreviewed GNU archive padding')
    found = set()
    for member in members:
        raw_name = member[3]
        if raw_name.startswith('/') and raw_name[1:].isdigit():
            start = int(raw_name[1:])
            member_name = long_names[start:long_names.index(b'/\n', start)].decode('ascii')
        else:
            member_name = raw_name.rstrip('/')
        if member_name in member_patches:
            if member_name in found:
                raise ValueError('Duplicate archive member')
            found.add(member_name)
            member[2] = member_patches[member_name](member[2])
    if found != set(member_patches):
        raise ValueError('Missing archive members')
    offsets = {}
    position = 8
    for old, _, data, _ in members:
        offsets[old] = position
        position += 60 + len(data) + (len(data) & 1)
    indexes = [m for m in members if m[3] == '/']
    if len(indexes) != 1 or any(m[3] == '/SYM64/' for m in members):
        raise ValueError('Expected one 32-bit GNU archive symbol index')
    index = bytearray(indexes[0][2])
    count = struct.unpack_from('>I', index)[0]
    if 4 + count * 4 > len(index):
        raise ValueError('Truncated GNU archive symbol index')
    for i in range(count):
        old = struct.unpack_from('>I', index, 4 + 4 * i)[0]
        if old not in offsets:
            raise ValueError('Invalid GNU archive symbol member offset')
        struct.pack_into('>I', index, 4 + 4 * i, offsets[old])
    indexes[0][2] = bytes(index)
    result = bytearray(b'!<arch>\n')
    for _, header, data, _ in members:
        result += header[:48] + str(len(data)).encode('ascii').ljust(10) + header[58:]
        result += data
        if len(data) & 1:
            result += b'\n'
    return bytes(result)
