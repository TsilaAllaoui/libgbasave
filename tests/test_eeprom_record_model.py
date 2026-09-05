#!/usr/bin/env python3
from __future__ import annotations

def main():
    block=bytearray([0xff])*0x10000
    TAG=0xA55A; lane=0x400; recsz=16; tagoff=14
    dword=17
    base=dword*lane
    def tag(r): return int.from_bytes(block[base+r*recsz+tagoff:base+r*recsz+tagoff+2],'little')
    def virgin():
        for r in range(64):
            a=base+r*recsz
            if tag(r)==0xffff and block[a:a+8]==b'\xff'*8: return r
        return None
    def latest():
        for r in range(63,-1,-1):
            if tag(r)==TAG: return r
        return None
    # torn first record: programmed data without tag
    block[base]=0x7f
    assert virgin()==1 and latest() is None
    # Compare-before-write: repeating the newest logical value consumes no slot.
    def append(value):
        newest=latest()
        encoded=bytes([value])*8
        if newest is not None:
            a=base+newest*recsz
            if block[a:a+8]==encoded: return newest, False
        r=virgin();
        if r is None: return None, False
        a=base+r*recsz; block[a:a+8]=encoded; block[a+tagoff:a+tagoff+2]=TAG.to_bytes(2,'little')
        return r, True
    r,spent=append(1); assert spent and latest()==r
    r2,spent2=append(1); assert r2==r and not spent2 and latest()==r
    for version in range(2,64):
        r,spent=append(version); assert r is not None and spent
        assert latest()==r
    assert virgin() is None
    print('PASS EEPROM lane: dirty record skipped + identical writes consume no record + 63 committed values')
    print('PASS EEPROM lane capacity: 64 records per 8-byte dword')
if __name__=='__main__': main()
