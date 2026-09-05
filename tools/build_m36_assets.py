#!/usr/bin/env python3
import argparse
from pathlib import Path

def emit(name,data):
    vals=','.join(f'0x{x:02X}u' for x in data)
    return f'inline constexpr std::uint8_t {name}[] = {{{vals}}};\ninline constexpr std::size_t {name}Size = sizeof({name});\n'
def main():
    ap=argparse.ArgumentParser(); ap.add_argument('asset_dir',type=Path); ap.add_argument('--header',type=Path,required=True); a=ap.parse_args()
    items=[('kSfwSaveStubs','sfw_save_stubs.bin'),('kM36SramPayload','m36_sram_generic_gbs2_payload.bin'),('kM36CompactPayload','m36_compact_rww_payload.bin')]
    a.header.parent.mkdir(parents=True,exist_ok=True)
    with a.header.open('w') as f:
        f.write('#pragma once\n#include <cstddef>\n#include <cstdint>\nnamespace gbasave::generated_m36_assets {\n')
        for n,fn in items: f.write(emit(n,(a.asset_dir/fn).read_bytes()))
        f.write('}\n')
if __name__=='__main__': main()
