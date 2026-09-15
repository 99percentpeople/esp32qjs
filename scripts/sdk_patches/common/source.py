"""Exact transformations for upstream C sources already accepted by a hash gate."""
import re
from collections.abc import Callable


def replace(source: str, before: str, after: str, count: int = 1) -> str:
    actual = source.count(before)
    if not before or actual != count:
        raise ValueError(f"Expected {count} source anchors, found {actual}: {before[:100]}")
    return source.replace(before, after)


def function(source: str, name: str, transform: Callable[[str], str] | None = None) -> str:
    """Select one reviewed top-level C definition, optionally replacing its body."""
    pattern = (r'^(?:static\s+)?[a-zA-Z_]\w*(?:[ \t]+[a-zA-Z_]\w*)*[ \t\n*]+'
               + re.escape(name) + r'\([^;{}]*\)\n\{')
    matches = list(re.finditer(pattern, source, re.M))
    if len(matches) != 1:
        raise ValueError(f"Expected one C function {name}, found {len(matches)}")
    start = matches[0].start()
    end = source.index('\n}\n', matches[0].end()) + 3
    body = source[start:end]
    return body if transform is None else source[:start] + transform(body) + source[end:]
