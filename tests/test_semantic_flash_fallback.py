#!/usr/bin/env python3
"""Regression for structurally discovered/recompiled FLASH1M save libraries.

Set GBASAVE_SEMANTIC_FLASH_FIXTURE to any legal test ROM whose FLASH1M save
library keeps the Nintendo setup/ABI semantics but no longer matches the stock
routine byte signatures. No game/title/code/hash is encoded here.
"""
from __future__ import annotations

import os
from pathlib import Path
import subprocess
import tempfile

from test_support import GBASAVEHANDLER_BIN as BIN


def run(*args: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run([str(BIN), *args], text=True, capture_output=True)


def main() -> None:
    fixture_text = os.environ.get("GBASAVE_SEMANTIC_FLASH_FIXTURE")
    if not fixture_text:
        print("SKIP semantic FLASH fallback: GBASAVE_SEMANTIC_FLASH_FIXTURE is not set")
        return

    fixture = Path(fixture_text)
    if not fixture.is_file():
        raise AssertionError(f"semantic FLASH fixture does not exist: {fixture}")

    scan = run("scan", str(fixture))
    assert scan.returncode == 0, scan.stderr
    assert "FLASH1M" in scan.stdout
    assert "Discovery: SEMANTIC_OR_NONUNIQUE" in scan.stdout
    assert "SwitchFlashBank[semantic]" in scan.stdout
    assert "patch=" in scan.stdout and "body=" in scan.stdout

    with tempfile.TemporaryDirectory() as td_text:
        td = Path(td_text)
        fram_out = td / "fram.gba"
        nor_out = td / "nor.gba"

        fram = run(str(fixture), str(fram_out), "fram_dual64_romwrite_bit0")
        assert fram.returncode == 0, fram.stderr
        assert "FLASH1M_TO_BANKED_SRAM_WINDOW" in fram.stdout
        assert fram_out.stat().st_size == fixture.stat().st_size

        nor = run(str(fixture), str(nor_out), "intel-word40")
        assert nor.returncode == 0, nor.stderr
        assert "Generic Intel word40" in nor.stdout
        assert nor_out.stat().st_size >= fixture.stat().st_size
        assert "Placement proof:" in nor.stdout
        # Structural terminal-FF placement may keep a full-size ROM unchanged;
        # otherwise the runtime/storage may be appended within target capacity.
        assert (
            "STRUCTURAL_TRAILING_FF" in nor.stdout
            or "APPENDED_WITHIN_TARGET_CAPACITY" in nor.stdout
            or "GBABR_v" in nor.stdout
        )

    print("PASS semantic FLASH fallback: structural scan + FRAM route + NOR-only route")


if __name__ == "__main__":
    main()
