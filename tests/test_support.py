"""Shared paths for libgbasave regression tests.

libgbasave and GBASaveHandler are separate repositories. Tests that exercise
the reference desktop consumer first honor GBASAVEHANDLER_BIN, then look for a
sibling GBASaveHandler checkout, and finally fall back to the historical
monorepo build layout.
"""
from pathlib import Path
import os

LIB_ROOT = Path(__file__).resolve().parents[1]
WORKSPACE_ROOT = LIB_ROOT.parent


def _default_handler_binary() -> Path:
    override = os.environ.get("GBASAVEHANDLER_BIN")
    if override:
        return Path(override)

    candidates = (
        WORKSPACE_ROOT / "GBASaveHandler" / "build" / "GBASaveHandler",
        WORKSPACE_ROOT / "build" / "GBASaveHandler" / "GBASaveHandler",
    )
    for candidate in candidates:
        if candidate.exists():
            return candidate
    return candidates[0]


GBASAVEHANDLER_BIN = _default_handler_binary()
