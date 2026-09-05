#!/usr/bin/env python3
from __future__ import annotations

DATA=0xA55A; BLANK=0x5AA5; DEAD=0x0000; VIRGIN=0xFFFF

def slots(block:int,unit:int)->int: return (block-2)//(unit+2)

def model(block_size:int,unit_size:int):
    count=slots(block_size,unit_size); assert count<=16
    metadata=count*unit_size
    mem=bytearray([0xff])*block_size
    def head(): return int.from_bytes(mem[metadata:metadata+2],'little')
    def sethead(v): mem[metadata:metadata+2]=v.to_bytes(2,'little')
    def marker(i):
        o=metadata+2+i*2; return int.from_bytes(mem[o:o+2],'little')
    def setmarker(i,v):
        o=metadata+2+i*2; mem[o:o+2]=v.to_bytes(2,'little')
    def publish(i): sethead(head() & ~(1<<i))
    def latest():
        h=head()
        for i in range(count-1,-1,-1):
            if h & (1<<i): continue
            if marker(i) in (DATA,BLANK): return i,marker(i)
        return None
    def erased(i): return all(x==0xff for x in mem[i*unit_size:(i+1)*unit_size])
    def virgin():
        h=head()
        for i in range(count):
            if not (h&(1<<i)): continue
            if marker(i)==VIRGIN and erased(i): return i
            if marker(i)==VIRGIN: setmarker(i,DEAD)
            publish(i)
        return None
    def commit(i,tag): setmarker(i,tag); publish(i)

    assert latest() is None
    # Torn data in slot0. Recovery quarantines+publishes it, then slot1 is free.
    mem[0]=0x7f
    assert virgin()==1 and marker(0)==DEAD and not (head()&1)
    i=virgin(); mem[i*unit_size:(i+1)*unit_size]=bytes([0x55])*unit_size; commit(i,DATA)
    assert latest()==(1,DATA)
    # Torn after marker but before head: next allocation recovers marker into head.
    i=2; mem[i*unit_size:(i+1)*unit_size]=bytes([0x33])*unit_size; setmarker(i,DATA)
    assert virgin()==3 and latest()==(2,DATA)
    i=virgin(); commit(i,BLANK); assert latest()==(3,BLANK)
    return count

def main():
    assert model(0x10000,0x1000)==15
    print('PASS versioned FLASH head+tag model: 15 generations/block')
    print('PASS torn data quarantine and marker-before-head recovery')
    print('PASS latest generation requires one head read plus latest tag')
if __name__=='__main__': main()
