"""Expand the pinned SDK Raw TX admission gates for laboratory use.

Two reviewed edits in ieee80211_raw_frame_sanity_check:
1. Management-subtype allowlist: turn the reserved-subtype reject into an
   always-taken accept (named PV0 management subtypes plus the rest).
2. Protected-frame gate: always continue as if FC Protected were clear.
Length, interface, lock, association, sequence and data checks remain.
Encryption is NOT applied by the framework; PMF/decrypting receivers still
apply their own rules. Not Espressif API support or RF qualification.
Never modify the shared SDK; preserve prior off-channel fixes.
"""
from __future__ import annotations
import argparse
import hashlib
from pathlib import Path
import struct

from sdk_patches.common.archive import rewrite_archive

OBJECTS = {
    'esp32c3': ('bd56e238d6e54fc1ad41f80aa2f4637308b05865b0cfd6514384d4d306b000fb',
                '595dc2687f5b602193e217793cb2f3f1f53f9f94e67f71ee4468a0f63b332f96'),
    'esp32s3': ('42e78e49e36e26566c63ddbd191f5a558b43ee8bff7c17f22bf34d33d6525bbe',
                '3a9c90bef50a448e80620d338acada33193a8efdd6d5d7007aa061c6d4f531b0'),
    'esp32c5': ('ee92da6a1bc66af65cff6c7ac1490803161eb5f84aa0dac408971501224ac128',
                'f0fd1b8319dfa2f9864d8fa9bee9757b8736fc3b37c6c751ccbc6d9270313c75'),
}
FUNCTION = '.text.ieee80211_raw_frame_sanity_check'
# Each target: list of (offset, expected, replacement) in file byte order.
# Management-subtype branch (after type==0 path) and Protected bit branch.
EDITS = {
    'esp32c3': (
        (0x118, bytes.fromhex('6386d704'), bytes.fromhex('63060004')),
        (0xe4, bytes.fromhex('93f60604'), bytes.fromhex('93f60600')),
    ),
    'esp32c5': (
        (0x118, bytes.fromhex('6386d704'), bytes.fromhex('63060004')),
        (0xe4, bytes.fromhex('93f60604'), bytes.fromhex('93f60600')),
    ),
    'esp32s3': (
        (0x11a, bytes.fromhex('771f1e'), bytes.fromhex('77171e')),
        (0xf3, bytes.fromhex('676702'), bytes.fromhex('676e02')),
    ),
}


def patch_object(source: bytes, target: str) -> bytes:
    if target not in OBJECTS or hashlib.sha256(source).hexdigest() not in OBJECTS[target]:
        raise ValueError('Unreviewed Raw TX management object: '+target)
    shoff=struct.unpack_from('<I',source,32)[0]
    width,count,names_index=struct.unpack_from('<HHH',source,46)
    if width != 40:
        raise ValueError('Expected reviewed ELF32 section table')
    sections=[struct.unpack_from('<10I',source,shoff+i*width) for i in range(count)]
    names_section=sections[names_index]
    names=source[names_section[4]:names_section[4]+names_section[5]]
    selected=[s for s in sections if names[s[0]:names.index(b'\0',s[0])].decode()==FUNCTION]
    if len(selected)!=1:
        raise ValueError('Missing unique SDK Raw TX validator')
    result=bytearray(source)
    section=selected[0]
    for offset,expected,replacement in EDITS[target]:
        if offset+len(expected)>section[5]:
            raise ValueError('Raw TX instruction outside reviewed function')
        start=section[4]+offset
        if result[start:start+len(expected)]!=expected:
            raise ValueError('Unexpected Raw TX admission branch at +0x%x' % offset)
        result[start:start+len(expected)]=replacement
    return bytes(result)


def patch_archive(source: bytes, target: str) -> bytes:
    return rewrite_archive(source,member_patches={
        'ieee80211_output.o': lambda data: patch_object(data,target)})


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--target',choices=tuple(OBJECTS),required=True)
    args=parser.parse_args()
    if args.source.resolve()==args.output.resolve():
        parser.error('Raw TX management output must be a separate build archive')
    output=patch_archive(args.source.read_bytes(),args.target)
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_bytes(output)
    print('Experimental Raw TX management+protected admission extension: '+args.target)


if __name__=='__main__':
    main()
