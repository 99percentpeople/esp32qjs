"""Discover target tools using the exported environment or ESP-IDF tools root."""
import os
from pathlib import Path
import shutil


def find_target_tool(target: str, name: str) -> str | None:
    directory = 'xtensa-esp-elf' if target == 'esp32s3' else 'riscv32-esp-elf'
    prefix = 'xtensa-esp32s3-elf' if target == 'esp32s3' else directory
    executable = prefix + '-' + name
    found = shutil.which(executable)
    if found:
        return found
    root = Path(os.environ.get('IDF_TOOLS_PATH', str(Path.home() / '.espressif')))
    suffix = '.exe' if os.name == 'nt' else ''
    candidates = sorted((root / 'tools' / directory).glob('*/' + directory + '/bin/' + executable + suffix))
    return str(candidates[-1]) if candidates else None
