"""Stable repository root for tests, independent of case directory depth."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
