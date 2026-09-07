#!/usr/bin/env python3
"""Generate a constexpr header from frozen direct-RWW payload assets + ABI manifest."""
import argparse, hashlib, json
from pathlib import Path

def camel(name):
    return ''.join(part.capitalize() for part in name.split('_'))

def emit_array(name,data):
    rows=[]
    for i in range(0,len(data),16):
        rows.append('    '+','.join(f'0x{x:02X}u' for x in data[i:i+16])+',')
    return f'inline constexpr uint8_t {name}[] = {{\n'+'\n'.join(rows)+'\n};\ninline constexpr size_t '+name+'Size = sizeof('+name+');\n'

def emit_constants(prefix,obj,out):
    for k,v in obj.items():
        if isinstance(v,dict): emit_constants(prefix+camel(k),v,out)
        elif isinstance(v,int): out.append(f'inline constexpr size_t {prefix}{camel(k)} = 0x{v:X}u;')

def main():
    ap=argparse.ArgumentParser(); ap.add_argument('asset_dir',type=Path); ap.add_argument('--header',type=Path,required=True); a=ap.parse_args()
    m=json.loads((a.asset_dir/'manifest.json').read_text())
    text=['#pragma once','#include <stddef.h>','#include <stdint.h>','namespace gbasave::generated_direct_rww_assets {']
    arrays={
      'sfw_save_stubs':'kSfwSaveStubs','sram_rww_payload':'kSramRwwPayload',
      'eeprom_dual_rww_payload':'kEepromDualRwwPayload','flash_compact_rww_payload':'kFlashCompactRwwPayload'}
    for key,name in arrays.items():
        d=(a.asset_dir/m['assets'][key]['file']).read_bytes(); got=hashlib.sha256(d).hexdigest(); exp=m['assets'][key]['sha256']
        if got!=exp: raise SystemExit(f'{key}: SHA256 mismatch {got} != {exp}')
        text.append(emit_array(name,d))
    const=[]
    emit_constants('kSram', {k:v for k,v in m['assets']['sram_rww_payload'].items() if k not in ('file','sha256')}, const)
    emit_constants('kEeprom', {k:v for k,v in m['assets']['eeprom_dual_rww_payload'].items() if k not in ('file','sha256')}, const)
    emit_constants('kFlashCompact', {k:v for k,v in m['assets']['flash_compact_rww_payload'].items() if k not in ('file','sha256')}, const)
    # Stub constants are emitted individually for readable call sites.
    for n,s in m['assets']['sfw_save_stubs']['stubs'].items():
        p='kStub'+camel(n)
        const.append(f'inline constexpr size_t {p}Offset = 0x{s["offset"]:X}u;')
        const.append(f'inline constexpr size_t {p}Length = 0x{s["length"]:X}u;')
        if 'dispatch_offset' in s: const.append(f'inline constexpr size_t {p}DispatchOffset = 0x{s["dispatch_offset"]:X}u;')
    text += const; text.append('}')
    a.header.parent.mkdir(parents=True,exist_ok=True); a.header.write_text('\n'.join(text)+'\n')
if __name__=='__main__': main()
