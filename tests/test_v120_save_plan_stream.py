#!/usr/bin/env python3
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='libgbasave_stream_') as tmp:
    exe = Path(tmp) / 'save_plan_stream_host'
    cmd = [
        'c++', '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
        '-I', str(ROOT / 'include'),
        str(ROOT / 'tests/save_plan_stream_host.cpp'),
        str(ROOT / 'src/embedded/superfw_savepatch_port.cpp'),
        str(ROOT / 'src/embedded/save_plan_stream.cpp'),
        '-o', str(exe),
    ]
    subprocess.run(cmd, check=True, cwd=ROOT)
    subprocess.run([str(exe)], check=True, cwd=ROOT)
