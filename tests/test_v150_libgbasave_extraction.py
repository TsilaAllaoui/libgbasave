#!/usr/bin/env python3
"""Architecture regression for libgbasave 1.0 + GBASaveHandler 1.5 consumer."""
from pathlib import Path
import os
import subprocess

ROOT = Path(__file__).resolve().parents[1]
WORKSPACE = ROOT.parent
from test_support import GBASAVEHANDLER_BIN as BIN


def main() -> None:
    assert BIN.exists(), BIN

    lib_cmake = (ROOT / 'CMakeLists.txt').read_text()
    wrapper_cmake = (WORKSPACE / 'GBASaveHandler/CMakeLists.txt').read_text()
    wrapper_main = (WORKSPACE / 'GBASaveHandler/src/main.cpp').read_text()
    wrapper_io = (WORKSPACE / 'GBASaveHandler/src/file_io.cpp').read_text()
    umbrella = (ROOT / 'include/gbasave/gbasave.h').read_text()

    assert 'add_library(gbasave_shared SHARED' in lib_cmake
    assert 'add_library(gbasave_static STATIC' in lib_cmake
    assert 'gbasave::shared' in wrapper_cmake and 'gbasave::static' in wrapper_cmake
    assert '#include "gbasave/gbasave.h"' in wrapper_main
    assert 'SaveEngine{}.patch' in wrapper_main
    assert 'SaveLibraryScanner' not in wrapper_main
    assert 'SavePatcher' not in wrapper_main
    assert '#include <fstream>' in wrapper_io

    library_production = '\n'.join(
        p.read_text(errors='ignore')
        for directory in ('src', 'include')
        for p in (ROOT / directory).rglob('*')
        if p.suffix in ('.cpp', '.h')
    )
    assert '#include <fstream>' not in library_production
    assert 'static RomImage load(' not in library_production
    assert 'void save(const std::string' not in library_production

    for required in ('engine.h', 'rom_image.h', 'save_patcher.h', 'nor_backends.h', 'version.h'):
        assert f'gbasave/{required}' in umbrella

    out = subprocess.check_output([str(BIN), 'nor', 'validate'], text=True)
    assert out.startswith('PASS:')
    out = subprocess.check_output([str(BIN), 'nor', 'list'], text=True)
    assert 'libgbasave 1.0.0' in out
    for key in ('m6m', 'm36', 'm6mgd137', 'mx26l6420mc-90'):
        assert key in out

    # On Linux, prove the reference wrapper is really using the shared library.
    if os.name == 'posix' and Path('/usr/bin/ldd').exists():
        ldd = subprocess.check_output(['/usr/bin/ldd', str(BIN)], text=True)
        assert 'libgbasave.so.1' in ldd

    print('PASS libgbasave extraction: shared/static library + thin filesystem/CLI consumer boundary')


if __name__ == '__main__':
    main()
