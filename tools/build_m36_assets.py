#!/usr/bin/env python3
"""Validate and generate constexpr M36 runtime assets from one ABI manifest."""
from __future__ import annotations
import argparse, hashlib, json
from pathlib import Path


def camel(name: str) -> str:
    return ''.join(part.capitalize() for part in name.split('_'))


def emit_array(name: str, data: bytes) -> str:
    rows=[]
    for i in range(0,len(data),16):
        rows.append('    '+','.join(f'0x{x:02X}u' for x in data[i:i+16])+',')
    return f'inline constexpr std::uint8_t {name}[] = {{\n'+'\n'.join(rows)+'\n};\ninline constexpr std::size_t '+name+'Size = sizeof('+name+');\n'


def emit_constants(prefix: str, obj: dict, out: list[str]) -> None:
    for key,value in obj.items():
        if isinstance(value,dict):
            emit_constants(prefix+camel(key),value,out)
        elif isinstance(value,int):
            out.append(f'inline constexpr std::size_t {prefix}{camel(key)} = 0x{value:X}u;')


def main() -> None:
    ap=argparse.ArgumentParser()
    ap.add_argument('asset_dir',type=Path)
    ap.add_argument('--header',type=Path,required=True)
    a=ap.parse_args()
    manifest=json.loads((a.asset_dir/'manifest.json').read_text())
    mapping={
      'sfw_save_stubs':'kSfwSaveStubs',
      'm36_sram_payload':'kM36SramPayload',
      'm36_eeprom_payload':'kM36EepromPayload',
      'm36_flash_compact_payload':'kM36CompactPayload',
    }
    text=['#pragma once','#include <cstddef>','#include <cstdint>','namespace gbasave::generated_m36_assets {']
    for key,name in mapping.items():
        item=manifest['assets'][key]
        data=(a.asset_dir/item['file']).read_bytes()
        got=hashlib.sha256(data).hexdigest()
        if got!=item['sha256']:
            raise SystemExit(f'{key}: SHA256 mismatch {got} != {item["sha256"]}')
        text.append(emit_array(name,data))
    const=[]
    for key,prefix in (
        ('m36_sram_payload','kSram'),('m36_eeprom_payload','kEeprom'),('m36_flash_compact_payload','kFlashCompact')):
        emit_constants(prefix,{k:v for k,v in manifest['assets'][key].items() if k not in ('file','sha256')},const)
    for name,stub in manifest['assets']['sfw_save_stubs']['stubs'].items():
        prefix='kStub'+camel(name)
        const.append(f'inline constexpr std::size_t {prefix}Offset = 0x{stub["offset"]:X}u;')
        const.append(f'inline constexpr std::size_t {prefix}Length = 0x{stub["length"]:X}u;')
        if 'dispatch_offset' in stub:
            const.append(f'inline constexpr std::size_t {prefix}DispatchOffset = 0x{stub["dispatch_offset"]:X}u;')
    text += const
    text.append('} // namespace gbasave::generated_m36_assets')
    a.header.parent.mkdir(parents=True,exist_ok=True)
    a.header.write_text('\n'.join(text)+'\n')

if __name__=='__main__':
    main()
