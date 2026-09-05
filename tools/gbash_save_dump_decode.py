#!/usr/bin/env python3
"""Decode GBASaveHandler-owned NOR save dumps without modifying the cartridge.

Supported layouts:
  eeprom-record-lanes  SAFE_V1 EEPROM512 / legacy EEPROM8K record lanes.
  eeprom-compact-ab    v1.3 COMPACT_V2 EEPROM8K two-block A/B lanes.
  raw-summary          generic non-FF mutation summary.

COMPACT_V2 never overlaps logical game save bytes. The decoder selects the
newest valid block header, reconstructs the 8 KiB logical EEPROM, and reports
per-dword record usage/torn records so a post-poweroff FlashGBX dump can tell
whether write/GC/publication succeeded.
"""
from __future__ import annotations
import argparse, struct
from pathlib import Path

UNIT=0x10000
DWORDS=64
RECORDS=64
RECORD=16
LANE=RECORDS*RECORD
TAG=b'\x5a\xa5'

C_MAGIC=0x324A4345  # ECJ2
C_VERSION=1
C_DWORDS=1024
C_RECORD=10
C_META_RESERVE=1024
C_HEADER_COMMIT=20


def non_ff_ranges(data: bytes):
    out=[]; start=None
    for i,b in enumerate(data):
        if b!=0xFF and start is None: start=i
        elif b==0xFF and start is not None: out.append((start,i)); start=None
    if start is not None: out.append((start,len(data)))
    return out


def decode_eeprom(data: bytes, base: int):
    if len(data)==0 or len(data)%UNIT:
        raise SystemExit(f'EEPROM record-lane dump size must be a non-zero multiple of 0x{UNIT:X}')
    units=len(data)//UNIT
    logical=bytearray(b'\xff'*(units*512))
    lines=[f'Mode: EEPROM_RECORD_LANES', f'Physical dump: 0x{len(data):X} bytes @ 0x{base:X}',
           f'Logical EEPROM: {units*512} bytes ({units} x 512-byte units)']
    committed_dwords=0; total_records=0; dirty_records=0
    for u in range(units):
        unit=data[u*UNIT:(u+1)*UNIT]; used=[]
        for d in range(DWORDS):
            lane=unit[d*LANE:(d+1)*LANE]
            latest=-1; valid=0; dirty=0
            for r in range(RECORDS):
                rec=lane[r*RECORD:(r+1)*RECORD]
                if rec[14:16]==TAG:
                    latest=r; valid+=1
                elif rec != b'\xff'*RECORD:
                    dirty+=1
            total_records+=valid; dirty_records+=dirty
            if latest>=0:
                committed_dwords+=1
                rec=lane[latest*RECORD:(latest+1)*RECORD]
                # Runtime stores Nintendo's serial bit/byte order reversed.
                logical[(u*DWORDS+d)*8:(u*DWORDS+d+1)*8]=rec[:8][::-1]
                used.append((d,latest,valid,dirty))
        lines.append(f'Unit {u:02d} physical 0x{base+u*UNIT:06X}..0x{base+(u+1)*UNIT-1:06X}: '
                     f'{len(used)}/64 dwords committed')
        if used:
            indices=[x[1] for x in used]
            lines.append(f'  latest-record index range: {min(indices)}..{max(indices)}; '
                         f'committed records: {sum(x[2] for x in used)}; dirty/torn: {sum(x[3] for x in used)}')
    lines += [f'Total committed logical dwords: {committed_dwords}/{units*DWORDS}',
              f'Total committed physical records: {total_records}',
              f'Total dirty/torn physical records: {dirty_records}',
              'WRITE PATH: ' + ('NOR mutation/commits PRESENT' if total_records else 'NO committed EEPROM records found')]
    return '\n'.join(lines)+'\n', bytes(logical)


def _compact_geometry(total: int):
    if total % 2:
        raise SystemExit('COMPACT_V2 dump must contain exactly two equal physical erase blocks')
    block=total//2
    if block not in (0x10000,0x20000):
        raise SystemExit(f'COMPACT_V2 expected total dump 0x20000 (M6M) or 0x40000 (M36), got 0x{total:X}')
    records=(block-C_META_RESERVE)//(C_DWORDS*C_RECORD)
    if records < 2:
        raise SystemExit('COMPACT_V2 geometry provides fewer than two records/dword')
    lane=records*C_RECORD
    metadata=C_DWORDS*lane
    return block,records,lane,metadata


def _header(block: bytes, metadata: int):
    if metadata+C_HEADER_COMMIT+2 > len(block):
        return {'valid':False,'reason':'header outside block'}
    magic,generation,inv,size=struct.unpack_from('<IIII',block,metadata)
    records,version=struct.unpack_from('<HH',block,metadata+16)
    commit=struct.unpack_from('<H',block,metadata+C_HEADER_COMMIT)[0]
    valid=(magic==C_MAGIC and inv==((~generation)&0xffffffff) and size==8192 and
           version==C_VERSION and commit==0xA55A)
    return dict(valid=valid, magic=magic, generation=generation, inverse=inv,
                size=size, records=records, version=version, commit=commit)


def _newer(a: int,b: int):
    return a!=b and ((a-b)&0xffffffff) < 0x80000000


def decode_eeprom_compact(data: bytes, base: int):
    block_bytes,records,lane,metadata=_compact_geometry(len(data))
    blocks=[data[:block_bytes],data[block_bytes:]]
    headers=[_header(b,metadata) for b in blocks]
    valid=[i for i,h in enumerate(headers) if h['valid'] and h['records']==records]
    lines=[
        'Mode: EEPROM_COMPACT_AB',
        f'Physical dump: 0x{len(data):X} bytes @ 0x{base:X}',
        f'Erase block: 0x{block_bytes:X}; records/dword: {records}; lane bytes: {lane}',
        f'Logical EEPROM: 8192 bytes / {C_DWORDS} dwords',
    ]
    for i,h in enumerate(headers):
        addr=base+i*block_bytes
        if h.get('valid') and h.get('records')==records:
            lines.append(f'Block {i} @ 0x{addr:06X}: VALID generation={h["generation"]} commit=A55A')
        else:
            lines.append(f'Block {i} @ 0x{addr:06X}: NOT VALID '
                         f'(magic=0x{h.get("magic",0):08X}, gen={h.get("generation",0)}, '
                         f'records={h.get("records",0)}, commit=0x{h.get("commit",0xffff):04X})')
    if not valid:
        lines += ['ACTIVE GENERATION: none', 'WRITE PATH: no committed compact EEPROM generation found']
        return '\n'.join(lines)+'\n', bytes(b'\xff'*8192)
    active=valid[0]
    if len(valid)==2 and _newer(headers[1]['generation'],headers[0]['generation']):
        active=1
    lines.append(f'ACTIVE GENERATION: block {active}, generation={headers[active]["generation"]}')

    logical=bytearray(b'\xff'*8192)
    total_committed=0; dirty=0; committed_dwords=0; max_latest=-1
    b=blocks[active]
    for d in range(C_DWORDS):
        start=d*lane
        latest=-1
        for r in range(records):
            off=start+r*C_RECORD
            rec=b[off:off+C_RECORD]
            if rec[8:10]==TAG:
                latest=r; total_committed+=1
            elif rec != b'\xff'*C_RECORD:
                dirty+=1
        if latest>=0:
            committed_dwords+=1
            max_latest=max(max_latest,latest)
            off=start+latest*C_RECORD
            logical[d*8:(d+1)*8]=b[off:off+8][::-1]
    lines += [
        f'Active committed logical dwords: {committed_dwords}/{C_DWORDS}',
        f'Active committed physical records: {total_committed}',
        f'Active dirty/torn records: {dirty}',
        f'Highest latest-record index: {max_latest if max_latest>=0 else "none"} / {records-1}',
        'GC STATUS: ' + ('lane exhaustion reached/near; next changed write may compact' if max_latest>=records-1 else 'free per-dword versions remain'),
        'WRITE PATH: ' + ('NOR mutation/commits PRESENT' if total_committed else 'generation exists but no logical dword records committed'),
    ]
    return '\n'.join(lines)+'\n', bytes(logical)


def raw_summary(data: bytes, base: int):
    ranges=non_ff_ranges(data)
    lines=[f'Mode: RAW_SUMMARY', f'Dump: 0x{len(data):X} bytes @ 0x{base:X}',
           f'Non-FF bytes: {sum(1 for b in data if b!=0xFF)}']
    if not ranges: lines.append('WRITE PATH: dump is ALL FF')
    else:
        lines.append(f'WRITE PATH: NOR mutation PRESENT; {len(ranges)} non-FF ranges')
        for a,b in ranges[:32]: lines.append(f'  0x{base+a:06X}..0x{base+b-1:06X} ({b-a} bytes)')
        if len(ranges)>32: lines.append(f'  ... {len(ranges)-32} more ranges')
    return '\n'.join(lines)+'\n'


def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('dump', type=Path)
    ap.add_argument('--mode', choices=['eeprom-record-lanes','eeprom-compact-ab','raw-summary'], required=True)
    ap.add_argument('--base', type=lambda x:int(x,0), default=0)
    ap.add_argument('-o','--output', type=Path)
    ap.add_argument('--extract-save', type=Path, help='EEPROM modes: write reconstructed logical EEPROM bytes')
    a=ap.parse_args(); data=a.dump.read_bytes()
    if a.mode=='eeprom-record-lanes': text,logical=decode_eeprom(data,a.base)
    elif a.mode=='eeprom-compact-ab': text,logical=decode_eeprom_compact(data,a.base)
    else: text=raw_summary(data,a.base); logical=None
    print(text,end='')
    if a.output: a.output.write_text(text)
    if a.extract_save:
        if logical is None: raise SystemExit('--extract-save requires an EEPROM decode mode')
        a.extract_save.write_bytes(logical)
if __name__=='__main__': main()
