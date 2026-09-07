#!/usr/bin/env python3
from pathlib import Path
import subprocess, sys
ROOT=Path(__file__).resolve().parents[1]
TESTS=[
 'test_dev10_flash_compact_audio_only.py',
 'test_dev8_leafgreen_internal_layout.py',
 'test_dev6_flash1m_stream_parity.py',
 'test_dev7_stream_irq_ownership.py',
 'test_v134_generic_nor_patch.py',
 'test_no_title_hardcodes.py',
 'test_v160_embedded_fram_parity.py',
]
for name in TESTS:
    print(f'== {name} ==')
    subprocess.run([sys.executable,str(ROOT/'tests'/name)],cwd=ROOT,check=True)
print('libgbasave focused release gate: PASS')
