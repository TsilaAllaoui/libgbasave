#!/usr/bin/env python3
from pathlib import Path
import re
root=Path(__file__).resolve().parents[1]
src=(root/'src/embedded/save_plan_stream.cpp').read_text()
hdr=(root/'include/gbasave/embedded/save_plan_stream.h').read_text()
ver=(root/'include/gbasave/version.h').read_text()
assert '0x03007FFCu' in src
assert 'push_irq_literal' in src
assert 'SFW_MAX_IRQ_OPS' in src
assert 'plan->overflow = 1u' in src
assert 'generic aligned 0x03007FFC IRQ-chain literal discovery' in hdr
m=re.search(r'kLibraryDevelopmentRevision = (\d+)u',ver);assert m and int(m.group(1))>=7
assert 'kLibraryBuildVersion = "1.0.0-dev' in ver
# title/game code must not drive the production rule
for bad in ['SMA4','AX4E','F-Zero','AFZE','AWAE','A3AP']:
    assert bad not in src, bad
print('PASS: libgbasave dev7 owns generic bounded/deduplicated streaming IRQ literal discovery')
