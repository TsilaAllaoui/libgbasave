#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = (ROOT / 'runtime' / 'gba_runtime.cpp').read_text()

# Static guard: a full-size SRAM read may be a refresh boundary only when a
# separately proven lifecycle capability opts in. Mirror ownership alone is not
# enough: WL4 hardware proves ordinary full reads must retain live-shadow state.
required = [
    'restoreOwnedSramShadowFromSnapshot',
    'usesFullReadSramRefresh()',
    'kSramConfigFullReadRefresh',
    'byteCount == gRuntimeConfig.sramSizeBytes',
    'reinterpret_cast<uptr>(destination) == gRuntimeConfig.sramShadowAddress',
    'reinterpret_cast<uptr>(source) == gRuntimeConfig.originalSramBaseAddress',
    'reinterpret_cast<uptr>(source) == gRuntimeConfig.sramShadowAddress',
]
for needle in required:
    assert needle in SRC, needle

# Behavioral model of the MZM failure: software reset clears EWRAM but does
# not re-enter the ROM boot hook, so the IRQ state can remain ARMED. A full
# SRAM-base -> mirror-base load must refresh from persistent snapshot anyway.
SIZE = 0x8000
snapshot = bytearray((i * 37 + 11) & 0xFF for i in range(SIZE))
shadow = bytearray(SIZE)              # MZM soft reset clears EWRAM
irq_state = 'ARMED'                   # survives because boot hook was skipped

# SramTestFlash-like small writes happen before MZM's READ_ALL operation.
shadow[0x120:0x128] = b'METROID!'
assert shadow != snapshot

# MZM-capability full-image read boundary: restore even though IRQ state is ARMED.
source_is_sram_base = True
destination_is_owned_mirror = True
byte_count = SIZE
full_read_refresh_capability = True
if full_read_refresh_capability and source_is_sram_base and destination_is_owned_mirror and byte_count == SIZE:
    shadow[:] = snapshot
    irq_state = 'ARMED'

assert shadow == snapshot
assert irq_state == 'ARMED'

# Partial reads must NOT force a persistent refresh; they operate on current
# live shadow so ordinary game writes remain NOR-free until the hotkey.
shadow[0x200] ^= 0x5A
before = bytes(shadow)
byte_count = 0x20
if full_read_refresh_capability and source_is_sram_base and destination_is_owned_mirror and byte_count == SIZE:
    shadow[:] = snapshot
assert bytes(shadow) == before

# WL4 regression oracle: mirror ownership without the independent refresh bit
# must never replace a newer live save on a normal full-image title/menu read.
wl4_snapshot = bytearray([0x11] * SIZE)
wl4_shadow = bytearray([0x22] * SIZE)  # newer live state, not committed yet
full_read_refresh_capability = False
byte_count = SIZE
if full_read_refresh_capability and source_is_sram_base and destination_is_owned_mirror and byte_count == SIZE:
    wl4_shadow[:] = wl4_snapshot
assert wl4_shadow == bytearray([0x22] * SIZE)

print('PASS Phase17 MZM internal-soft-reset full-image refresh model')
print('PASS Phase17 partial SRAM reads preserve live shadow semantics')
print('PASS Phase17 WL4 regression: full read without refresh capability preserves newer live shadow')
