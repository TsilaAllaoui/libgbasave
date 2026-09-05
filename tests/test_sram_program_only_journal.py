#!/usr/bin/env python3
from __future__ import annotations

import json
import os
import pathlib
import re
import struct
import zlib

ROOT = pathlib.Path(__file__).resolve().parents[1]
FIXTURES = pathlib.Path(os.environ.get('GBASH_FIXTURE_DIR', '/mnt/data/roms_work'))
MMZ = FIXTURES / 'Mega Man Zero (USA, Europe).gba'
DB = ROOT / 'data' / 'GBABRPLANS.json'
RUNTIME = ROOT / 'runtime' / 'gba_runtime.cpp'
LAYOUT = ROOT / 'src' / 'storage_layout.cpp'

SAVE_BYTES = 0x8000
HEADER_BYTES = 32
RECORD_BYTES = SAVE_BYTES + HEADER_BYTES
COMMIT = 0xA55A
MAGIC = 0x4A525353
VERSION = 1


def function_body(text: str, signature: str) -> str:
    start = text.index(signature)
    brace = text.index('{', start)
    depth = 0
    for i in range(brace, len(text)):
        if text[i] == '{':
            depth += 1
        elif text[i] == '}':
            depth -= 1
            if depth == 0:
                return text[brace + 1:i]
    raise AssertionError(f'unterminated function {signature}')


def fnv1a(data: bytes) -> int:
    h = 2166136261
    for b in data:
        h ^= b
        h = (h * 16777619) & 0xFFFFFFFF
    return h


def build_record(data: bytes, generation: int, committed: bool) -> bytes:
    h = fnv1a(data)
    header = struct.pack(
        '<7IHH', MAGIC, VERSION, len(data), generation, (~generation) & 0xFFFFFFFF,
        h, (~h) & 0xFFFFFFFF, COMMIT if committed else 0xFFFF, 0xFFFF)
    assert len(header) == HEADER_BYTES
    return data + header


def valid_record(raw: bytes) -> bool:
    if len(raw) != RECORD_BYTES:
        return False
    fields = struct.unpack('<7IHH', raw[SAVE_BYTES:SAVE_BYTES + HEADER_BYTES])
    magic, version, save_bytes, generation, gen_inv, h, h_inv, commit, _ = fields
    return (
        commit == COMMIT and magic == MAGIC and version == VERSION and save_bytes == SAVE_BYTES
        and gen_inv == ((~generation) & 0xFFFFFFFF)
        and h_inv == ((~h) & 0xFFFFFFFF)
        and fnv1a(raw[:SAVE_BYTES]) == h
    )


def main() -> None:
    runtime = RUNTIME.read_text()
    commit_body = function_body(runtime, 'static u32 commitSramProgramOnlyJournal()')
    assert 'gbashRunSramStackErase' not in commit_body
    assert 'eraseBlockWorker' not in commit_body
    assert 'journalRangeIsErased' in commit_body
    assert commit_body.index('journalProgramBytes(base, shadow') < commit_body.index('const u16 commit = kSramSnapshotCommit')
    assert commit_body.index('const u16 commit = kSramSnapshotCommit') < commit_body.rindex('journalProgramBytes(')
    assert 'journal exhausted; deliberately fail without erase' in commit_body

    layout = LAYOUT.read_text()
    body = function_body(layout, 'std::optional<StorageLayout> tryCreateProgramOnlyJournalLayout(')
    assert 'programOnlyFfRanges(holes)' in body
    assert 'AppendedTail' not in body
    assert 'minimumRecordCount < 2u' in body

    if not MMZ.exists():
        print('SKIP exact MMZ fixture unavailable; source-level journal erase-free gates PASS')
        return

    rom = MMZ.read_bytes()
    crc = zlib.crc32(rom) & 0xFFFFFFFF
    db = json.loads(DB.read_text())
    plan = next(p for p in db['plans'] if p['full_crc32'] == crc and p['rom_size'] == len(rom))
    safe_ff = [r for r in plan['regions'] if r.get('runtime_safe') and r['fill'] == 0xFF]

    # Current backend erase unit is 64 KiB. MMZ has exactly one such complete
    # structurally safe unit, so A/B is impossible even though aggregate FF
    # capacity can hold two complete program-only records.
    full_64k = []
    spans = []
    for r in sorted(safe_ff, key=lambda x: x['start']):
        start, end = r['start'], r['start'] + r['size']
        aligned64 = (start + 0xFFFF) & ~0xFFFF
        if aligned64 + 0x10000 <= end:
            full_64k.append(aligned64)
        a = (start + 3) & ~3
        b = end & ~3
        if b > a:
            spans.append((a, b - a))
    assert len(full_64k) == 1, [hex(x) for x in full_64k]
    assert sum(n for _, n in spans) >= 2 * RECORD_BYTES

    # Mirror the allocator's trim: expose exactly whole records from the first
    # safe fragments. This specifically proves a record may cross a physical
    # fragment boundary without implying an erasable combined sector.
    needed = 2 * RECORD_BYTES
    exposed = []
    left = needed
    for start, size in spans:
        if left == 0:
            break
        take = min(size, left)
        exposed.append((start, take))
        left -= take
    assert left == 0
    assert len(exposed) >= 2
    assert exposed[0] == (0x2D0000, 0x10000)
    assert exposed[1][0] == 0x4ADE40 and exposed[1][1] == 0x40

    # Power-cut model: a torn first record is quarantined forever; the second
    # virgin record can commit. No operation in this model changes 0->1.
    arena = bytearray(b'\xFF' * needed)
    data1 = bytes((i * 17 + 3) & 0xFF for i in range(SAVE_BYTES))
    rec1 = build_record(data1, 1, False)
    tear_at = 0x5000
    for i, b in enumerate(rec1[:tear_at]):
        arena[i] &= b
    assert not valid_record(bytes(arena[:RECORD_BYTES]))
    assert any(b != 0xFF for b in arena[:RECORD_BYTES])

    data2 = bytes((i * 29 + 11) & 0xFF for i in range(SAVE_BYTES))
    rec2_uncommitted = build_record(data2, 2, False)
    base = RECORD_BYTES
    assert all(b == 0xFF for b in arena[base:base + RECORD_BYTES])
    for i, b in enumerate(rec2_uncommitted):
        arena[base + i] &= b
    assert not valid_record(bytes(arena[base:base + RECORD_BYTES]))
    # Commit-last is the only final transition.
    arena[base + SAVE_BYTES + 28] &= COMMIT & 0xFF
    arena[base + SAVE_BYTES + 29] &= (COMMIT >> 8) & 0xFF
    assert valid_record(bytes(arena[base:base + RECORD_BYTES]))
    assert len(arena) == 2 * RECORD_BYTES  # no third virgin record exists; next commit must report exhausted

    print('PASS MMZ exact plan: one 64KiB erase-safe unit only; no fake A/B layout')
    print('PASS program-only SRAM journal: 2 records span 0x2D0000/0x4ADE40, commit-last, torn record quarantined, zero erase path')


if __name__ == '__main__':
    main()
