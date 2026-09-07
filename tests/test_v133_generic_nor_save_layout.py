#!/usr/bin/env python3
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]

def main():
    with tempfile.TemporaryDirectory() as td:
        exe = Path(td) / "nor_save_layout_host"
        cmd = [
            "c++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
            "-I", str(ROOT / "include"),
            str(ROOT / "tests" / "nor_save_layout_host.cpp"),
            str(ROOT / "src" / "embedded" / "nor_save_layout.cpp"),
            "-o", str(exe),
        ]
        subprocess.run(cmd, check=True)
        subprocess.run([str(exe)], check=True)
    print("PASS: generic NOR save layout is geometry/capability driven and chip-name free")

if __name__ == "__main__":
    main()
