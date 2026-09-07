#!/usr/bin/env python3
from pathlib import Path
import re
ROOT=Path(__file__).resolve().parents[1]
h=(ROOT/'include/gbasave/embedded/exact_rom_plan.h').read_text()
s=(ROOT/'src/embedded/exact_rom_plan.cpp').read_text()
assert 'gbasave_exact_rom_plan_database_version' in h
assert 'gbasave_exact_rom_plan_identity_candidate_count' in h
assert 'gbasave_exact_rom_plan_identity_candidate_get' in h
assert 'kFormatVersion' in s and 'copy_entry(entry, out)' in s
assert 'GBABRPLANS.DAT' not in h+s
print('PASS libgbasave embedded exact-plan API: compiled DB introspection + installed identity candidates; no external DAT dependency')
