#!/usr/bin/env python3
"""v1.2 host-maintainability, target-capacity, and reporting contract."""
from pathlib import Path
import hashlib, subprocess, tempfile

ROOT=Path(__file__).resolve().parents[1]
from test_support import GBASAVEHANDLER_BIN as BIN
FULL16=Path('/mnt/data/roms_current/Game Boy Wars Advance 1+2 (Japan).gba')

def main():
    assert BIN.exists()
    assert (ROOT/'build_runtime/runtime.bin').stat().st_size >= 14260

    cmake=(ROOT/'CMakeLists.txt').read_text()
    for source in ('save_signature.cpp','save_marker.cpp','exact_plan_save_library.cpp','patch_report.cpp'):
        assert source in cmake
    assert 'save_library_profile.cpp' not in cmake  # removed dead version whitelist

    scanner=(ROOT/'src/save_library.cpp').read_text()
    assert len(scanner.splitlines()) < 1100
    assert 'scanSaveLibraryFromExactPlan' in scanner
    report=(ROOT/'src/patch_report.cpp').read_text()
    assert 'Size/layout summary:' in report and 'Why it grows:' in report

    backends=(ROOT/'generated/nor_profile_db.h').read_text()
    assert 'M6/M6M 8 MiB' in backends
    assert 'Generic Intel word40 (capacity up to GBA limit)' in backends
    assert 'M36 16 MiB' in backends

    production='\n'.join(
        p.read_text(errors='ignore')
        for d in ('src','include','runtime')
        for p in (ROOT/d).rglob('*')
        if p.suffix in ('.cpp','.h','.S'))
    for token in ('Magical Quest','Sonic Advance','Castlevania','Super Mario Advance','Game Boy Wars','Dragon Ball Z'):
        assert token not in production
    assert 'gameCode() ==' not in production and 'title() ==' not in production

    if FULL16.exists():
        with tempfile.TemporaryDirectory() as td:
            td=Path(td)
            too_big=subprocess.run(
                [str(BIN),'patch',str(FULL16),str(td/'m6m.gba'),'--nor-profile','m6m'],
                text=True,capture_output=True)
            assert too_big.returncode != 0
            assert 'source ROM exceeds selected target/profile capacity' in too_big.stderr
            generic=subprocess.run(
                [str(BIN),'patch',str(FULL16),str(td/'generic.gba'),'--nor-profile','intel-word40'],
                text=True,capture_output=True,check=True)
            assert 'Generic Intel word40' in generic.stdout
            assert (td/'generic.gba').stat().st_size > FULL16.stat().st_size

    print('PASS v1.2+ maintainability: decomposed host, no title hardcodes, explicit physical target capacities')

if __name__=='__main__': main()
