#!/usr/bin/env python3
"""Rebuild the hardware-proven embedded FRAM patch assets from source.

The default toolchain is the portable LLVM path used by GBABR reference builds.
Outputs are written into data/embedded_assets only after their SHA-256 matches
known hardware-proven bytes unless --accept-new is explicitly supplied.
"""
from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import shutil
import subprocess
import tempfile

EXPECTED = {
    "sfw_save_stubs.bin": "603fbadc87af89138b2b6913a7ba82d7c3b5a304f5cf075e752fbbfb6436e18d",
    "fram_flash512_inline.bin": "5d137d775fd386bd4b0dcab31d0c071553484131b99cf906e51ddac0d8721fe3",
    "fram_banked_runtime.bin": "ff44d11f28884a627540bddc72333bc6d56759d32e17f0c34af17bc32aa7adf8",
}

TARGETS = (
    ("sfw_save_stubs", "sfw_save_stubs.bin", "thumb"),
    ("savehw_flash512_inline", "fram_flash512_inline.bin", "thumb"),
    ("savehw_fram_runtime", "fram_banked_runtime.bin", "arm"),
)


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def require_tool(name: str) -> str:
    path = shutil.which(name)
    if not path:
        raise SystemExit(f"missing required tool: {name}")
    return path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--accept-new", action="store_true", help="allow replacing frozen bytes with new hashes")
    args = parser.parse_args()

    root = args.root.resolve()
    source_dir = root / "runtime/fram"
    output_dir = root / "data/embedded_assets"
    output_dir.mkdir(parents=True, exist_ok=True)

    clang = require_tool("clang")
    ld = require_tool("ld.lld")
    objcopy = require_tool("llvm-objcopy")

    built: list[tuple[str, Path, str]] = []
    with tempfile.TemporaryDirectory(prefix="libgbasave-fram-runtime-") as td:
        tmp = Path(td)
        for stem, output_name, mode in TARGETS:
            source = source_dir / f"{stem}.S"
            linker = source_dir / f"{stem}.ld"
            obj = tmp / f"{stem}.o"
            elf = tmp / f"{stem}.elf"
            binary = tmp / output_name
            march = "-mthumb" if mode == "thumb" else "-marm"
            subprocess.run([
                clang, "--target=arm-none-eabi", "-mcpu=arm7tdmi", march,
                "-c", str(source), "-o", str(obj),
            ], check=True)
            subprocess.run([
                ld, "-flavor", "gnu", "-m", "armelf", "-T", str(linker),
                str(obj), "-o", str(elf),
            ], check=True)
            subprocess.run([objcopy, "-O", "binary", str(elf), str(binary)], check=True)
            actual = digest(binary)
            expected = EXPECTED[output_name]
            if actual != expected and not args.accept_new:
                raise SystemExit(
                    f"REFUSED: {output_name} changed\nexpected {expected}\nactual   {actual}\n"
                    "Audit on hardware before using --accept-new."
                )
            built.append((output_name, binary, actual))

        for output_name, binary, _actual in built:
            shutil.copyfile(binary, output_dir / output_name)

    sums = output_dir / "SHA256SUMS.txt"
    sums.write_text("".join(f"{actual}  {name}\n" for name, _binary, actual in built))
    subprocess.run(["python3", str(root / "tools/build_embedded_assets.py")], cwd=root, check=True)
    for name, _binary, actual in built:
        print(f"{name}: {actual}")
    print("PASS: embedded FRAM runtime/stub sources reproduce frozen hardware-proven bytes")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
