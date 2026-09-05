#!/usr/bin/env python3
DATA=0xA55A; BLANK=0x5AA5; VIRGIN=0xFFFF; DEAD=0x0000
UNITS=32; BLOCKS=40; SLOTS=15
class Block:
    def __init__(self):
        self.owner=VIRGIN; self.head=0xffff; self.tags=[VIRGIN]*SLOTS; self.data=[False]*SLOTS
blocks=[Block() for _ in range(BLOCKS)]
def owned(i,u): return i>=UNITS and blocks[i].owner==(0xB000|u)
def latest_in(i):
    b=blocks[i]
    for s in range(SLOTS-1,-1,-1):
        if b.head&(1<<s): continue
        if b.tags[s] in (DATA,BLANK): return s,b.tags[s]
    return None
def latest(u):
    for i in range(BLOCKS-1,UNITS-1,-1):
        if owned(i,u) and latest_in(i): return i,*latest_in(i)
    x=latest_in(u); return (u,*x) if x else None
def virgin_in(i):
    b=blocks[i]
    for s in range(SLOTS):
        if not (b.head&(1<<s)): continue
        if b.tags[s]==VIRGIN and not b.data[s]: return s
        if b.tags[s]==VIRGIN: b.tags[s]=DEAD
        b.head &= ~(1<<s)
    return None
def alloc(u):
    spill=None
    for i in range(BLOCKS-1,UNITS-1,-1):
        if owned(i,u): spill=i; break
    if spill is not None:
        s=virgin_in(spill)
        if s is not None:return spill,s
    else:
        s=virgin_in(u)
        if s is not None:return u,s
    for i in range(UNITS,BLOCKS):
        if blocks[i].owner==VIRGIN:
            blocks[i].owner=0xB000|u
            return i,virgin_in(i)
    return None
def commit(u,tag=DATA):
    a=alloc(u); assert a
    i,s=a; blocks[i].data[s]=tag==DATA; blocks[i].tags[s]=tag; blocks[i].head &= ~(1<<s)
    return i,s
# Consume primary then spill without touching other units.
for n in range(20):
    i,s=commit(0)
assert latest(0)==(32,4,DATA), latest(0)
assert all(blocks[u].head==0xffff for u in range(1,UNITS))
# Another unit independently claims the next spill after exhausting primary.
for n in range(16): commit(7)
assert latest(7)==(33,0,DATA), latest(7)
# Owner-before-data power cut remains harmless: owned empty block is reused.
blocks[34].owner=0xB000|3
assert latest(3) is None
assert alloc(3)==(34,0)
print('PASS Phase08 FLASH shared spill pool: primary overflow persists and remains isolated')
print('PASS spill owner-before-data power cut falls back/reuses safely')
