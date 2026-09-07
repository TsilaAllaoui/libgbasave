#!/usr/bin/env python3
from pathlib import Path
import subprocess, tempfile

ROOT = Path(__file__).resolve().parents[1]

def main():
    with tempfile.TemporaryDirectory() as td:
        exe = Path(td) / "nor_patch_host"
        cmd = [
            "c++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
            "-I", str(ROOT / "include"), "-I", str(ROOT / "generated"),
            str(ROOT / "tests" / "nor_patch_host.cpp"),
            str(ROOT / "src" / "embedded" / "nor_patch.cpp"),
            "-o", str(exe),
        ]
        subprocess.run(cmd, check=True)
        subprocess.run([str(exe)], check=True)
    print("v1.3 generic NOR patch composer: PASS")

if __name__ == "__main__":
    main()
