"""Write generated build inputs only when their contents change."""
from pathlib import Path


def write_if_changed(path: Path, data: bytes | str) -> None:
    data = data.encode() if isinstance(data, str) else data
    if path.is_file() and path.read_bytes() == data:
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
