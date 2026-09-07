#!/usr/bin/env python3
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
fixture = os.environ.get('GBASAVE_TEST_ROM_SMA3')
if not fixture:
    common = Path('/mnt/data/gbash_inputs/Super Mario Advance 3 - Yoshi\'s Island (USA).gba')
    if common.exists(): fixture = str(common)
if not fixture or not Path(fixture).exists():
    print('SKIP libgbasave exact ROM plan: SMA3 exact fixture unavailable')
    raise SystemExit(0)
with tempfile.TemporaryDirectory(prefix='libgbasave_exactplan_') as tmp:
    exe = Path(tmp) / 'exact_rom_plan_host'
    cmd = [
        'c++', '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
        '-I', str(ROOT / 'include'), '-I', str(ROOT / 'generated'),
        str(ROOT / 'tests/exact_rom_plan_host.cpp'),
        str(ROOT / 'src/embedded/superfw_savepatch_port.cpp'),
        str(ROOT / 'src/embedded/save_plan_stream.cpp'),
        str(ROOT / 'src/embedded/exact_rom_plan.cpp'),
        '-o', str(exe),
    ]
    subprocess.run(cmd, check=True, cwd=ROOT)
    subprocess.run([str(exe), fixture], check=True, cwd=ROOT)
