#!/usr/bin/env python3
"""Compile gba_static_analyzer GBABRPLANS.json into a constexpr libgbasave table.

The generated table intentionally contains only analysis/ROM-ABI facts. Physical
NOR placement remains a consumer/target-capability decision.
"""
import argparse
import json
from pathlib import Path


def game_code_u32(value: str) -> int:
    raw = str(value).encode('ascii', 'strict')
    if len(raw) != 4:
        raise ValueError(f'invalid game_code {value!r}')
    return int.from_bytes(raw, 'little')


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument('input', type=Path)
    ap.add_argument('--header', type=Path, required=True)
    a = ap.parse_args()
    src = json.loads(a.input.read_text())
    entries = []
    ops = []
    irqs = []
    raw = []
    regions = []

    for p in src.get('plans', []):
        if 'full_crc32' not in p or 'rom_size' not in p:
            continue
        first_op = len(ops)
        for o in p.get('ops', []):
            rb = bytes.fromhex(str(o.get('raw') or ''))
            ro = len(raw)
            raw.extend(rb)
            ops.append((int(o.get('offset', 0)), int(o.get('kind', 0)), ro, len(rb)))
        first_irq = len(irqs)
        for x in p.get('irq_offsets', []):
            irqs.append(int(x))
        first_region = len(regions)
        for r in p.get('regions', []):
            regions.append((
                int(r.get('start', 0)), int(r.get('size', 0)), int(r.get('fill', 0)),
                int(r.get('flags', 0)), int(r.get('alignment_log2', 0)), int(r.get('source', 0))))

        entries.append({
            'crc32': int(p['full_crc32']) & 0xFFFFFFFF,
            'rom_size': int(p['rom_size']),
            'save_type': int(p.get('save_type_id', 0) or 0),
            'first_op': first_op,
            'op_count': len(ops) - first_op,
            'first_irq': first_irq,
            'irq_count': len(irqs) - first_irq,
            'flags': int(p.get('flags', 0)),
            'game_code': game_code_u32(p.get('game_code', '????')),
            'entry_word': int(p.get('entry_word', 0)),
            'sample0_off': int(p.get('sample0_off', 0)), 'sample0_crc': int(p.get('sample0_crc', 0)),
            'sample1_off': int(p.get('sample1_off', 0)), 'sample1_crc': int(p.get('sample1_crc', 0)),
            'sample2_off': int(p.get('sample2_off', 0)), 'sample2_crc': int(p.get('sample2_crc', 0)),
            'tail_start': int(p.get('tail_start', p['rom_size'])), 'tail_bytes': int(p.get('tail_bytes', 0)),
            'first_region': first_region, 'region_count': len(regions) - first_region,
            'revision': int(p.get('revision', 0)), 'header_checksum': int(p.get('header_checksum', 0)),
            'fixed96': int(p.get('fixed96', 0x96)), 'source_kind': int(p.get('source_kind', 0)),
            'reject_code': int(p.get('reject_code', 0)), 'ready': 1 if p.get('ready') else 0,
        })

    # Preserve the historical crc32/size ordering used by the desktop exact-plan lookup.
    entries.sort(key=lambda x: (x['crc32'], x['rom_size']))
    a.header.parent.mkdir(parents=True, exist_ok=True)
    with a.header.open('w') as f:
        f.write('#pragma once\n#include <stdint.h>\nnamespace gbasave { namespace generated_gbabr_plan {\n')
        f.write('struct Op { uint32_t offset; uint8_t kind; uint32_t rawOffset; uint16_t rawSize; };\n')
        f.write('struct Region { uint32_t start; uint32_t size; uint8_t fill; uint8_t flags; uint8_t alignmentLog2; uint8_t source; };\n')
        f.write('struct Entry {\n')
        f.write('  uint32_t crc32, romSize, firstOp; uint16_t opCount; uint32_t firstIrq; uint16_t irqCount; uint8_t saveType;\n')
        f.write('  uint32_t flags, gameCode, entryWord;\n')
        f.write('  uint32_t sample0Off, sample0Crc, sample1Off, sample1Crc, sample2Off, sample2Crc;\n')
        f.write('  uint32_t tailStart, tailBytes, firstRegion; uint16_t regionCount;\n')
        f.write('  uint8_t revision, headerChecksum, fixed96, sourceKind, rejectCode, ready;\n')
        f.write('};\n')
        f.write(f'inline constexpr uint32_t kFormatVersion={int(src.get("version", 6))}u;\n')
        f.write('inline constexpr uint8_t kRawBytes[] = {')
        f.write(','.join(f'0x{x:02X}u' for x in raw)); f.write('};\n')
        f.write('inline constexpr Op kOps[] = {\n')
        for off, kind, ro, rs in ops:
            f.write(f'{{0x{off:08X}u,{kind}u,{ro}u,{rs}u}},\n')
        f.write('};\ninline constexpr uint32_t kIrqs[] = {')
        f.write(','.join(f'0x{x:08X}u' for x in irqs)); f.write('};\n')
        f.write('inline constexpr Region kRegions[] = {\n')
        for start, size, fill, flags, align, source in regions:
            f.write(f'{{0x{start:08X}u,0x{size:08X}u,{fill}u,{flags}u,{align}u,{source}u}},\n')
        f.write('};\ninline constexpr Entry kEntries[] = {\n')
        for e in entries:
            f.write(
                '{0x%(crc32)08Xu,0x%(rom_size)08Xu,%(first_op)uu,%(op_count)uu,%(first_irq)uu,%(irq_count)uu,%(save_type)uu,'
                '0x%(flags)08Xu,0x%(game_code)08Xu,0x%(entry_word)08Xu,'
                '0x%(sample0_off)08Xu,0x%(sample0_crc)08Xu,0x%(sample1_off)08Xu,0x%(sample1_crc)08Xu,0x%(sample2_off)08Xu,0x%(sample2_crc)08Xu,'
                '0x%(tail_start)08Xu,0x%(tail_bytes)08Xu,%(first_region)uu,%(region_count)uu,'
                '%(revision)uu,%(header_checksum)uu,%(fixed96)uu,%(source_kind)uu,%(reject_code)uu,%(ready)uu},\n' % e)
        f.write('};\n}} // namespace gbasave::generated_gbabr_plan\n')
    print('entries', len(entries), 'ops', len(ops), 'irqs', len(irqs), 'regions', len(regions), 'raw', len(raw))


if __name__ == '__main__':
    main()
