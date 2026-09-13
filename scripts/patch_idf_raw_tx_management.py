"""Expand only the pinned SDK Raw TX management-subtype gate for laboratory use.

Keep encryption, length, interface, lock, association, sequence and data checks.
The framework admits only named PV0 management subtypes, rejecting reserved
layouts. This is an experimental SDK extension, not Espressif API support or RF
qualification. Never modify the shared SDK; preserve prior off-channel fixes.
"""
from __future__ import annotations
import argparse
import hashlib
from pathlib import Path
import struct

from patch_idf_twt_probe_wake import patch_archive as rewrite_archive

OBJECTS = {
    'esp32c3': ('bd56e238d6e54fc1ad41f80aa2f4637308b05865b0cfd6514384d4d306b000fb',
                '595dc2687f5b602193e217793cb2f3f1f53f9f94e67f71ee4468a0f63b332f96'),
    'esp32s3': ('42e78e49e36e26566c63ddbd191f5a558b43ee8bff7c17f22bf34d33d6525bbe',
                '3a9c90bef50a448e80620d338acada33193a8efdd6d5d7007aa061c6d4f531b0'),
    'esp32c5': ('ee92da6a1bc66af65cff6c7ac1490803161eb5f84aa0dac408971501224ac128',
                'f0fd1b8319dfa2f9864d8fa9bee9757b8736fc3b37c6c751ccbc6d9270313c75'),
}
FUNCTION = '.text.ieee80211_raw_frame_sanity_check'
# Reached only after type == management and the Protected check. Turn a subtype
# comparison into an always-taken comparison, retaining its existing target and
# relocation. The rest of the function and the entire TX path remain byte-exact.
EDITS = {
    'esp32c3': (0x118, bytes.fromhex('6386d704'), bytes.fromhex('63060004')),
    'esp32c5': (0x118, bytes.fromhex('6386d704'), bytes.fromhex('63060004')),
    'esp32s3': (0x11a, bytes.fromhex('771f1e'), bytes.fromhex('77171e')),
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
    offset,expected,replacement=EDITS[target]
    section=selected[0]
    if offset+len(expected)>section[5]:
        raise ValueError('Raw TX instruction outside reviewed function')
    start=section[4]+offset
    if source[start:start+len(expected)]!=expected:
        raise ValueError('Unexpected Raw TX management branch')
    result=bytearray(source)
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
    print('Experimental Raw TX management-subtype extension: '+args.target)


if __name__=='__main__':
    main()
