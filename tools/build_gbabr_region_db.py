#!/usr/bin/env python3
"""Generate the embedded GBABR universal safe-region index.

The source remains the shared GBABRPLANS v6 database. Region flags/source are
preserved so every consumer applies the same structural-safety contract. The
canonical JSON/DAT remains authoritative and retains all GBABR fields.
"""
import argparse,json
from pathlib import Path
SAFE=1<<2; FF=1<<5; ZERO=1<<6

def main():
 ap=argparse.ArgumentParser(); ap.add_argument('input',type=Path); ap.add_argument('--header',type=Path,required=True); a=ap.parse_args()
 src=json.loads(a.input.read_text())
 entries=[]; regions=[]
 for p in src.get('plans',[]):
  if 'full_crc32' not in p or 'rom_size' not in p: continue
  by_key={}
  for r in p.get('regions',[]):
   flags=int(r.get('flags',0)); fill=int(r.get('fill',-1)); source=int(r.get('source',0))
   if not (flags&SAFE) or fill not in (0,255): continue
   st=int(r.get('start',0)); sz=int(r.get('size',0))
   if sz<=0: continue
   key=(st,sz,fill)
   item=(st,sz,fill,flags,source)
   prior=by_key.get(key)
   # Exact GBABR runs are preferred for identical extents, but unique SAFE
   # SuperFW ranges are retained. Runtime byte-verifies every embedded range.
   if prior is None or (source==1 and prior[4]!=1): by_key[key]=item
  rr=sorted(by_key.values())
  first=len(regions); regions.extend(rr)

  save_type=int(p.get('save_type_id',0) or 0)
  save_name=str(p.get('save_type_name',''))
  save_bytes={'SRAM':0x8000,'EEPROM4K':0x200,'EEPROM64K':0x2000,'FLASH512':0x10000,'FLASH1M':0x20000}.get(save_name,0)
  entries.append((int(p['full_crc32'])&0xffffffff,int(p['rom_size']),save_type,save_bytes,first,len(rr)))
 entries.sort(key=lambda x:(x[0],x[1]))
 a.header.parent.mkdir(parents=True,exist_ok=True)
 with a.header.open('w') as f:
  f.write('#pragma once\n#include <cstddef>\n#include <cstdint>\nnamespace gbasave::generated_gbabr {\n')
  f.write('inline constexpr std::uint8_t kRegionSafeCode = 1u << 2;\n')
  f.write('inline constexpr std::uint8_t kRegionFf = 1u << 5;\n')
  f.write('inline constexpr std::uint8_t kRegionZero = 1u << 6;\n')
  f.write('struct Region { std::uint32_t offset; std::uint32_t size; std::uint8_t fill; std::uint8_t flags; std::uint8_t source; };\n')
  f.write('struct Entry { std::uint32_t crc32; std::uint32_t romSize; std::uint32_t saveBytes; std::uint32_t firstRegion; std::uint16_t regionCount; std::uint8_t saveType; };\n')
  f.write(f'inline constexpr std::uint32_t kFormatVersion = {int(src.get("version",6) or 6)}u;\n')
  f.write('inline constexpr Region kRegions[] = {\n')
  for st,sz,fill,flags,source in regions: f.write(f'{{0x{st:08X}u,0x{sz:08X}u,0x{fill:02X}u,0x{flags:02X}u,0x{source:02X}u}},\n')
  f.write('};\ninline constexpr Entry kEntries[] = {\n')
  for crc,size,save_type,save_bytes,first,count in entries: f.write(f'{{0x{crc:08X}u,0x{size:08X}u,0x{save_bytes:08X}u,{first}u,{count}u,{save_type}u}},\n')
  f.write('};\ninline constexpr std::size_t kEntryCount=sizeof(kEntries)/sizeof(kEntries[0]);\n')
  f.write('inline constexpr std::size_t kRegionCount=sizeof(kRegions)/sizeof(kRegions[0]);\n}\n')
 print('entries',len(entries),'regions',len(regions))
if __name__=='__main__': main()
