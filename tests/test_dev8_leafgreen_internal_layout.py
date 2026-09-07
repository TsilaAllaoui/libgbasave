#!/usr/bin/env python3
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]

with tempfile.TemporaryDirectory() as td:
    exe = Path(td) / "leafgreen_internal_layout"
    cmd = [
        "c++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
        "-I", str(ROOT / "include"),
        "-I", str(ROOT / "generated"),
        str(ROOT / "tests" / "leafgreen_internal_layout_host.cpp"),
        str(ROOT / "src" / "embedded" / "exact_rom_plan.cpp"),
        str(ROOT / "src" / "embedded" / "superfw_savepatch_port.cpp"),
        str(ROOT / "src" / "embedded" / "nor_save_layout.cpp"),
        "-o", str(exe),
    ]
    subprocess.run(cmd, check=True)
    subprocess.run([str(exe)], check=True)
print("PASS libgbasave dev9: exact full-capacity protocol-save ROMs may use trusted internal FF journal blocks; stream fallback remains fail-closed")
