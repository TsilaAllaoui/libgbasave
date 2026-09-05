#!/usr/bin/env python3
"""Decode GBASaveHandler v0.32+ persistent NOR breadcrumbs from FlashGBX dumps."""
from __future__ import annotations
import argparse, struct
from pathlib import Path

MAGIC=0xBC32
OPS={1:'SRAM_COMMIT',2:'FLASH_SNAPSHOT',3:'FLASH_PATCH',4:'FLASH_BLANK'}
STAGES={
 0xFFFE:'ENTER',0xFFFC:'DATA_WRITTEN',0xFFF8:'DATA_VERIFIED',
 0xFFF0:'METADATA_WRITTEN',0xFFE0:'COMMITTED',0xFFC0:'FINAL_VERIFIED'
}

def stage_name(v:int)->str:
    if v in STAGES: return STAGES[v]
    # Power loss can leave a valid monotonic intermediate value. Report deepest
    # known stage whose cleared bits are all present rather than guessing success.
    order=[0xFFFE,0xFFFC,0xFFF8,0xFFF0,0xFFE0,0xFFC0]
    names=[STAGES[x] for x in order]
    deepest='UNKNOWN'
    for x,n in zip(order,names):
        if (v | x) == x: deepest=n+' (or later/partial)'
    return deepest

def u16(b,o): return struct.unpack_from('<H',b,o)[0]
def u32(b,o): return struct.unpack_from('<I',b,o)[0]

def decode(path:Path, base:int)->str:
    b=path.read_bytes(); out=[]
    out += [f'GBASaveHandler breadcrumb decoder',f'File: {path}',f'Size: 0x{len(b):X} ({len(b)} bytes)',f'Assumed NOR base: 0x{base:08X}','']
    hits=[]
    sig=struct.pack('<H',MAGIC)
    pos=0
    while True:
        pos=b.find(sig,pos)
        if pos<0: break
        if pos+8<=len(b):
            op,subject,stage=struct.unpack_from('<HHH',b,pos+2)
            if op in OPS:
                hits.append((pos,op,subject,stage))
        pos+=1
    if not hits:
        out += ['BREADCRUMBS: NONE FOUND','Interpretation: transaction did not reach breadcrumb ENTER, dump range is wrong, or this is a pre-v0.32 layout.']
    else:
        out += [f'BREADCRUMBS: {len(hits)} record(s) found']
        for i,(o,op,sub,st) in enumerate(hits,1):
            out += [f'[{i}] file+0x{o:X} / NOR 0x{base+o:08X}',f'    operation: {OPS[op]}',f'    subject:   {sub} (0x{sub:04X})',f'    stage:     {stage_name(st)} (0x{st:04X})']
            if OPS[op]=='SRAM_COMMIT': out.append('    subject meaning: low 16 bits of snapshot generation')
            else: out.append('    subject meaning: logical FLASH unit/sector')
        out.append('')
    # Decode SR16 headers if this is a 64KiB-style SRAM block dump.
    for block in range(0,len(b),0x10000):
        if block+0xFF20>len(b): break
        h=block+0xFF00
        if b[h:h+4]==b'SR16':
            ver,save,gen,geni,dh,dhi=struct.unpack_from('<IIIIII',b,h+4)
            commit=u16(b,h+28)
            valid_meta=(geni==(~gen&0xffffffff) and dhi==(~dh&0xffffffff) and commit==0xA55A)
            out += [f'SR16 @ file+0x{h:X} / NOR 0x{base+h:08X}: version={ver} save=0x{save:X} generation={gen}',f'    hash=0x{dh:08X} commit=0x{commit:04X} metadata={"VALID" if valid_meta else "INCOMPLETE/INVALID"}']
    # Decode generic FLASH metadata per 64KiB block when present.
    flash_lines=[]
    for block in range(0,len(b),0x10000):
        if block+0xF100>len(b): break
        head=u16(b,block+0xF000)
        markers=[u16(b,block+0xF002+i*2) for i in range(4)]
        if head!=0xFFFF or any(x!=0xFFFF for x in markers):
            flash_lines.append(f'FLASH block {block//0x10000}: head=0x{head:04X} markers='+' '.join(f'{x:04X}' for x in markers))
    if flash_lines:
        out += ['','FLASH SLOT METADATA:']+flash_lines
    out += ['','Stage guide:','  ENTER -> transaction selected a fresh patcher-owned record/slot','  DATA_WRITTEN -> physical NOR data program returned success','  DATA_VERIFIED -> exact data verification passed','  METADATA_WRITTEN -> slot/header metadata was programmed','  COMMITTED -> commit publication completed','  FINAL_VERIFIED -> committed state was re-read and accepted by GBASaveHandler']
    return '\n'.join(out)+'\n'

def main():
    ap=argparse.ArgumentParser(description='Decode GBASaveHandler v0.32+ FlashGBX NOR dump breadcrumbs.')
    ap.add_argument('dump',type=Path)
    ap.add_argument('--base',default='0',help='NOR address corresponding to dump byte 0, e.g. 0x7A0000')
    ap.add_argument('-o','--output',type=Path,help='write text log here (also prints to stdout)')
    a=ap.parse_args(); base=int(a.base,0)
    text=decode(a.dump,base)
    print(text,end='')
    if a.output:
        a.output.write_text(text,encoding='utf-8')
        print(f'Wrote: {a.output}')
if __name__=='__main__': main()
