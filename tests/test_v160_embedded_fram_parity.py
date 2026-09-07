#!/usr/bin/env python3
"""Host parity oracle for the GBABR v2.1 -> libgbasave FRAM extraction."""
from pathlib import Path
import hashlib
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
EXPECTED_ASSETS = {
    'sfw_save_stubs.bin': '603fbadc87af89138b2b6913a7ba82d7c3b5a304f5cf075e752fbbfb6436e18d',
    'fram_flash512_inline.bin': '5d137d775fd386bd4b0dcab31d0c071553484131b99cf906e51ddac0d8721fe3',
    'fram_banked_runtime.bin': 'ff44d11f28884a627540bddc72333bc6d56759d32e17f0c34af17bc32aa7adf8',
}


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> None:
    for name, expected in EXPECTED_ASSETS.items():
        actual = sha256(ROOT / 'data/embedded_assets' / name)
        assert actual == expected, (name, actual, expected)

    subprocess.run(['python3', 'tools/build_embedded_assets.py'], cwd=ROOT, check=True)
    with tempfile.TemporaryDirectory(prefix='gbasave-fram-parity-') as td:
        binary = Path(td) / 'embedded_fram_parity_host'
        subprocess.run([
            'c++', '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
            '-Iinclude', '-Igenerated',
            'tests/embedded_fram_parity_host.cpp',
            'src/embedded/save_memory_patch.cpp',
            'src/embedded/fram_patch.cpp',
            'src/embedded/superfw_savepatch_port.cpp',
            '-o', str(binary),
        ], cwd=ROOT, check=True)
        subprocess.run([str(binary)], cwd=ROOT, check=True)


if __name__ == '__main__':
    main()
