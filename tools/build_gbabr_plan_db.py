#!/usr/bin/env python3
import argparse, json
from pathlib import Path

def main():
    ap=argparse.ArgumentParser(); ap.add_argument('input',type=Path); ap.add_argument('--header',type=Path,required=True); a=ap.parse_args()
    src=json.loads(a.input.read_text())
    entries=[]; ops=[]; irqs=[]; raw=[]
    for p in src.get('plans',[]):
        if 'full_crc32' not in p or 'rom_size' not in p: continue
        first_op=len(ops)
        for o in p.get('ops',[]):
            rb=bytes.fromhex(str(o.get('raw') or ''))
            ro=len(raw); raw.extend(rb)
            ops.append((int(o.get('offset',0)),int(o.get('kind',0)),ro,len(rb)))
        first_irq=len(irqs)
        for x in p.get('irq_offsets',[]): irqs.append(int(x))
        entries.append((int(p['full_crc32'])&0xffffffff,int(p['rom_size']),int(p.get('save_type_id',0) or 0),first_op,len(ops)-first_op,first_irq,len(irqs)-first_irq))
    entries.sort(key=lambda x:(x[0],x[1]))
    a.header.parent.mkdir(parents=True,exist_ok=True)
    with a.header.open('w') as f:
        f.write('#pragma once\n#include <cstddef>\n#include <cstdint>\nnamespace gbasave::generated_gbabr_plan {\n')
        f.write('struct Op { std::uint32_t offset; std::uint8_t kind; std::uint32_t rawOffset; std::uint16_t rawSize; };\n')
        f.write('struct Entry { std::uint32_t crc32; std::uint32_t romSize; std::uint32_t firstOp; std::uint16_t opCount; std::uint32_t firstIrq; std::uint16_t irqCount; std::uint8_t saveType; };\n')
        f.write(f'inline constexpr std::uint32_t kFormatVersion={int(src.get("version",6))}u;\n')
        f.write('inline constexpr std::uint8_t kRawBytes[] = {')
        f.write(','.join(f'0x{x:02X}u' for x in raw)); f.write('};\n')
        f.write('inline constexpr Op kOps[] = {\n')
        for off,k,ro,rs in ops: f.write(f'{{0x{off:08X}u,{k}u,{ro}u,{rs}u}},\n')
        f.write('};\ninline constexpr std::uint32_t kIrqs[] = {')
        f.write(','.join(f'0x{x:08X}u' for x in irqs)); f.write('};\n')
        f.write('inline constexpr Entry kEntries[] = {\n')
        for crc,size,st,fo,oc,fi,ic in entries: f.write(f'{{0x{crc:08X}u,0x{size:08X}u,{fo}u,{oc}u,{fi}u,{ic}u,{st}u}},\n')
        f.write('};\n}\n')
    print('entries',len(entries),'ops',len(ops),'irqs',len(irqs),'raw',len(raw))
if __name__=='__main__': main()
