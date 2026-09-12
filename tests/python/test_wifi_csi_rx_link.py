"""Use the production SDK patch and target linker with actual ROM assignments."""
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
import patch_idf_csi_rx_copy as patcher


def elf(data):
    offset = struct.unpack_from('<I', data, 32)[0]
    width, count, names_index = struct.unpack_from('<HHH', data, 46)
    sections = [struct.unpack_from('<10I', data, offset + i * width) for i in range(count)]
    def body(section):
        return data[section[4]:section[4] + section[5]]
    def string(strings, index):
        return strings[index:strings.index(b'\0', index)].decode('ascii')
    names = body(sections[names_index])
    section_names = [string(names, s[0]) for s in sections]
    table = next(s for s in sections if s[1] == 2)
    strings = body(sections[table[6]])
    symbols = []
    for pos in range(table[4], table[4] + table[5], 16):
        symbol = struct.unpack_from('<IIIBBH', data, pos)
        symbols.append((string(strings, symbol[0]), symbol))
    relocations = {}
    for section in sections:
        if section[1] != 4:
            continue
        for pos in range(section[4], section[4] + section[5], 12):
            site, info, addend = struct.unpack_from('<IIi', data, pos)
            relocations[section_names[section[7]], site, info & 255] = (symbols[info >> 8], addend)
    return sections, section_names, relocations


def tool(target, name):
    prefix = 'xtensa-esp-elf' if target == 'esp32s3' else 'riscv32-esp-elf'
    executable = ('xtensa-esp32s3-elf' if target == 'esp32s3' else prefix) + '-' + name
    found = shutil.which(executable)
    if found:
        return found
    matches = sorted((Path.home() / '.espressif/tools' / prefix).glob('*/' + prefix + '/bin/' + executable))
    return str(matches[-1]) if matches else None


class WiFiCsiRxLink(unittest.TestCase):
    def test_copied_origins_cannot_resolve_back_to_rom(self):
        sdk = Path(os.environ.get('IDF_PATH', str(Path.home() / 'esp/esp-idf')))
        for target in ('esp32c3', 'esp32s3', 'esp32c5'):
            with self.subTest(target=target), tempfile.TemporaryDirectory() as directory:
                archive = sdk / 'components/esp_wifi/lib' / target / 'libpp.a'
                ar, ld = tool(target, 'ar'), tool(target, 'ld')
                if not archive.exists() or not ar or not ld:
                    self.skipTest('Pinned SDK archive and target linker required: ' + target)
                source = subprocess.check_output([ar, 'p', str(archive), 'wdev.o'], timeout=20)
                patched = patcher.patch_object(source, target)
                sections, names, refs = elf(source)
                origins = {key: names[symbol[-1]] for key, ((name, symbol), _) in refs.items()
                           if name in ('wDev_IndicateFrame', 'wDev_IndicateAmpdu')}
                self.assertTrue(origins, 'Actual compiled RX caller must participate')
                # A full firmware link supplies these ROM symbols independently
                # of wdev.o. A relocatable link reproduces that override exactly.
                root = Path(directory)
                (root / 'wdev.o').write_bytes(patched)
                result = subprocess.run([ld, '-EL', '-r', '--no-relax', '--defsym=wDev_IndicateFrame=0x40000fc0',
                    '--defsym=wDev_IndicateAmpdu=0x40000fbc', str(root / 'wdev.o'),
                    '-o', str(root / 'linked.o')], capture_output=True, text=True, timeout=30)
                self.assertEqual(result.returncode, 0, result.stderr)
                _, linked_names, linked_refs = elf((root / 'linked.o').read_bytes())
                for key, expected_section in origins.items():
                    (name, symbol), _ = linked_refs[key]
                    self.assertLess(symbol[-1], len(linked_names),
                        f'{target} {key} resolves to absolute ROM symbol {name}')
                    self.assertEqual(linked_names[symbol[-1]], expected_section)
                # Instrumentation redirects calls without rewriting SDK code.
                patched_sections, patched_names, _ = elf(patched)
                for section, name in zip(sections, names):
                    if section[2] & 4:
                        changed = patched_sections[patched_names.index(name)]
                        self.assertEqual(source[section[4]:section[4] + section[5]],
                                         patched[changed[4]:changed[4] + changed[5]])
