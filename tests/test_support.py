"""Shared paths for libgbasave regression tests.

Tests live in the library repo, while GBASaveHandler is now a separate consumer.
Override GBASAVEHANDLER_BIN when testing against another consumer build.
"""
from pathlib import Path
import os

LIB_ROOT = Path(__file__).resolve().parents[1]
WORKSPACE_ROOT = LIB_ROOT.parent
GBASAVEHANDLER_BIN = Path(
    os.environ.get(
        "GBASAVEHANDLER_BIN",
        str(WORKSPACE_ROOT / "build" / "GBASaveHandler" / "GBASaveHandler"),
    )
)
