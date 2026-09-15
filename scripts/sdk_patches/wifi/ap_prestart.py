#!/usr/bin/env python3
"""Build a private, pinned AP configuration entry for the mode-change window.

The public SDK setter and archive stay unchanged. The private entry keeps the
SDK validation/configuration body, but accepts only the old STA mode while the
framework's guarded mode hook has not allocated or started the AP interface.
Mutable SDK objects are external references, never duplicated into the clone.
This generator is a build input; it does not establish runtime/RF correctness.
"""
from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import struct
from sdk_patches.common.elf import ElfObject
import subprocess
import tempfile

ENTRY = "esp32qjs_wifi_ap_prestart_set_config_native"
ORIGINAL = "wifi_softap_set_config"
ARCHIVES = {
    "esp32c3": "0fcbed322d3254063b2c191bc8b8004e703ffd640296164a6552676180909a0c",
    "esp32c5": "4c86fc1d2f40a933af7f672972978eb7c82651d838fd10584e006438d585793b",
    "esp32s3": "265e5c89ac0d2a9444b5b466afee49f631ec549f96774f91a721ab5069c58e20",
}
MEMBERS = {
    "esp32c3": "03c9df29b0f8f943fcd541e75cebe268aeec358f3a438642d99c1e4cbb64c4a5",
    "esp32c5": "4c25bfc9472a6c5fa6267e6b94b2d781ddd64e90d557f82776f6f01650cd5b71",
    "esp32s3": "8ef51bb49d6543e5df0f17ead1e33bfd9430f9512621f13ff3b8f7d5c1703306",
}



def prepare_object(source: bytes, target: str) -> bytes:
    if hashlib.sha256(source).hexdigest() != MEMBERS[target]:
        raise ValueError("Unreviewed AP ioctl object")
    obj = ElfObject(source)
    machine = struct.unpack_from("<H", source, 18)[0]
    if machine != (94 if target == "esp32s3" else 243):
        raise ValueError("Wrong AP object architecture")
    entry = obj.symbol_names.index(ORIGINAL)
    code_index = obj.symbols[entry][5]
    if obj.names[code_index] != ".text." + ORIGINAL:
        raise ValueError("Unexpected AP setter section")
    result = bytearray(source)
    base = obj.sections[code_index][4]
    code = obj.body(code_index)
    if target == "esp32s3":
        # Keep the three-byte branch and its existing SLOT0 relocation. Only
        # STA passes: subtract one, branch on zero to the original valid path.
        if code[0x198:0x19b] != bytes.fromhex("22c2fe") or code[0x19e:0x1a1] != bytes.fromhex("b62202"):
            raise ValueError("Unreviewed Xtensa AP mode gate")
        result[base + 0x198:base + 0x19b] = bytes.fromhex("22c2ff")
        result[base + 0x19e:base + 0x1a1] = bytes.fromhex("162200")
    else:
        if code[0x2c:0x30] != bytes.fromhex("f9170544"):
            raise ValueError("Unreviewed RISC-V AP mode gate")
        branch = struct.unpack_from("<I", code, 0x30)[0]
        if branch & 0x01fff07f != (0x63 | (6 << 12) | (8 << 15) | (15 << 20)):
            raise ValueError("Unreviewed RISC-V AP mode rejection branch")
        result[base + 0x2c:base + 0x2e] = bytes.fromhex("fd17")  # c.addi a5,-1
        # bne a5,zero to the original error path; preserve displacement bits.
        struct.pack_into("<I", result, base + 0x30, (branch & 0xfe000f80) | 0x63 | (1 << 12) | (15 << 15))

    retained = {code_index}
    visited = set()
    while retained - visited:
        section_index = next(iter(retained - visited))
        visited.add(section_index)
        for reloc in obj.sections:
            if reloc[1] != 4 or reloc[7] != section_index:
                continue
            if reloc[6] != obj.table_index or reloc[9] != 12:
                raise ValueError("Unreviewed AP RELA table")
            for pos in range(reloc[4], reloc[4] + reloc[5], 12):
                _, info, addend = struct.unpack_from("<IIi", source, pos)
                index = info >> 8
                sym = obj.symbols[index]
                dest = sym[5]
                if not 0 < dest < len(obj.sections) or (sym[3] >> 4 and index != entry):
                    continue  # SDK globals become external below.
                if obj.sections[dest][2] & 1:  # No private copy of mutable SDK state.
                    offset = sym[1] + addend
                    aliases = [i for i, s in enumerate(obj.symbols) if s[3] >> 4 == 1 and s[5] == dest and
                               s[2] and s[1] <= offset < s[1] + s[2]]
                    if len(aliases) != 1:
                        raise ValueError("No unique SDK global for mutable AP reference: " + obj.symbol_names[index])
                    alias = aliases[0]
                    struct.pack_into("<Ii", result, pos + 4, (alias << 8) | (info & 255), offset - obj.symbols[alias][1])
                elif obj.sections[dest][2] & 2:  # Allocated immutable/code dependency.
                    retained.add(dest)

    strings = obj.strings + ENTRY.encode("ascii") + b"\0"
    symbols = bytearray()
    for i, original in enumerate(obj.symbols):
        symbol = original.copy()
        if i == entry:
            symbol[0] = len(obj.strings)
        elif symbol[3] >> 4 and 0 < symbol[5] < len(obj.sections):
            symbol[1] = symbol[2] = symbol[5] = 0  # Reuse the SDK's definition.
        symbols += struct.pack("<IIIBBH", *symbol)
    for index, contents in ((obj.strings_index, strings), (obj.table_index, symbols)):
        result += bytes((-len(result)) % max(1, obj.sections[index][8]))
        section = obj.sections[index].copy()
        section[4:6] = [len(result), len(contents)]
        result += contents
        struct.pack_into("<10I", result, obj.shoff + 40 * index, *section)
    return bytes(result)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--target", choices=tuple(ARCHIVES), required=True)
    for name in ("ar", "objcopy", "linker"):
        parser.add_argument("--" + name, required=True)
    args = parser.parse_args()
    if args.source.resolve() == args.output.resolve() or hashlib.sha256(args.source.read_bytes()).hexdigest() != ARCHIVES[args.target]:
        parser.error("AP clone requires a separate output and the original reviewed SDK archive")
    member = subprocess.check_output([args.ar, "p", str(args.source), "ieee80211_ioctl.o"])
    prepared = prepare_object(member, args.target)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="ap-prestart-", dir=args.output.parent) as directory:
        source = Path(directory) / "private.o"
        stripped = Path(directory) / "stripped.o"
        linked = Path(directory) / "linked.o"
        source.write_bytes(prepared)
        subprocess.run([args.objcopy, "--strip-debug", str(source), str(stripped)], check=True)
        subprocess.run([args.linker, "-r", "--gc-sections", "--undefined=" + ENTRY, "-o", str(linked), str(stripped)], check=True)
        result = ElfObject(linked.read_bytes())
        exports = [name for name, s in zip(result.symbol_names, result.symbols) if s[3] >> 4 and 0 < s[5] < len(result.sections)]
        if exports != [ENTRY] or any(s[2] & 3 == 3 and s[5] for s in result.sections):
            raise ValueError("AP clone exports extra definitions or duplicates mutable SDK state")
        args.output.write_bytes(result.source)
    print("Prepared private AP pre-start setter: " + args.target)


if __name__ == "__main__":
    main()
