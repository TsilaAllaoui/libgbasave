#!/usr/bin/env python3
"""libgbasave 1.0 extraction milestone contract.

The old GBASaveHandler-era source-file hashes are intentionally no longer the
architecture contract. Freeze proven runtime sources/blob bytes and the clean
library/consumer boundary instead.
"""
from pathlib import Path
import hashlib
import re

ROOT = Path(__file__).resolve().parents[1]

LEGACY_RUNTIME_SOURCES = {
    'runtime/legacy/gba_runtime.cpp': '9226092734dc5c47f9385a7a0b2eb6d2b54d157c7323c34818142e047eabf7f4',
    'runtime/legacy/sram_hotkey_irq.S': 'c89dc39212b7618a405d90bc2d861408650140a98803cce50a74cf9c9ec5c3d1',
    'runtime/legacy/sram_stack_worker.S': '6a68539bef8b6501de938992832bbbf63e57aad6818c2c9c20aeeb8ac0d9c409',
}


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def runtime_blob_sha(header: Path) -> str:
    text = header.read_text()
    body = text.split('kRuntimeBlob[] = {', 1)[1].split('};', 1)[0]
    blob = bytes(int(x, 16) for x in re.findall(r'0x([0-9A-Fa-f]{2})', body))
    return hashlib.sha256(blob).hexdigest()


def main() -> None:
    cmake = (ROOT / 'CMakeLists.txt').read_text()
    version = (ROOT / 'include/gbasave/version.h').read_text()
    assert 'project(libgbasave VERSION 1.0.0' in cmake
    assert 'kLibraryVersion = "1.0.0"' in version
    assert not (ROOT / 'src/main.cpp').exists(), 'CLI/main must not live in libgbasave'

    for relative, expected in LEGACY_RUNTIME_SOURCES.items():
        actual = sha(ROOT / relative)
        assert actual == expected, f'{relative}: {actual} != {expected}'

    assert runtime_blob_sha(ROOT / 'generated/runtime_blob.h') == \
        'e1031e63e15385a0ad1e32edf93689be7d3dbb0f54713e3e2816843b9a90b1f0'
    assert runtime_blob_sha(ROOT / 'generated/runtime_blob_extended.h') == \
        '4addfc123fa266193065bed407ba8f2ba1179a30e92516c089500221a6f864b8'

    production = '\n'.join(
        p.read_text(errors='ignore')
        for directory in ('src', 'include')
        for p in (ROOT / directory).rglob('*')
        if p.suffix in ('.cpp', '.h')
    )
    assert '#include <fstream>' not in production
    assert 'namespace gbasavehandler' not in production
    assert 'class RomImage' in (ROOT / 'include/gbasave/rom_image.h').read_text()
    assert 'class SaveEngine' in (ROOT / 'include/gbasave/engine.h').read_text()
    assert 'class SavePatcher' in (ROOT / 'include/gbasave/save_patcher.h').read_text()

    print('PASS libgbasave 1.0 milestone: clean consumer split + frozen legacy/extended runtime bytes')


if __name__ == '__main__':
    main()
