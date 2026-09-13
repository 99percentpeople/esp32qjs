"""Load literal native source fragments without newline or escape conversion."""
from functools import cache
from pathlib import Path

FIXTURE_ROOT = Path(__file__).resolve().parents[2] / "tests/c/fixtures"


@cache
def fixture_text(relative_path: str) -> str:
    return (FIXTURE_ROOT / relative_path).read_bytes().decode("utf-8")
