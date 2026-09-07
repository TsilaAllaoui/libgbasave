#!/usr/bin/env python3
"""Generate the compiled SuperFW compatibility/save-plan database used by libgbasave.

The binary database is developer-supplied analyzer/source data. Consumers never
load it directly; they call libgbasave's lookup API.
"""
from pathlib import Path
import argparse

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--input', default='data/superfw_patches.db')
    ap.add_argument('--output', default='generated/superfw_patch_db.h')
    ns=ap.parse_args()
    data=Path(ns.input).read_bytes()
    out=Path(ns.output); out.parent.mkdir(parents=True, exist_ok=True)
    lines=['#pragma once','#include <stdint.h>','namespace gbasave { namespace generated {',
           'inline constexpr uint8_t kSuperfwPatchDb[] = {']
    for i in range(0,len(data),16):
        lines.append('    '+','.join(f'0x{b:02X}' for b in data[i:i+16])+',')
    lines += ['};',f'inline constexpr uint32_t kSuperfwPatchDbSize = {len(data)}u;',
              '} } // namespace gbasave::generated','']
    out.write_text('\n'.join(lines))
    print(f'generated {len(data)} bytes -> {out}')
if __name__=='__main__': main()
