#!/usr/bin/env python3
"""Ensure the historical M36 protocol key is an alias, not a duplicate protocol."""
from pathlib import Path
import json

ROOT = Path(__file__).resolve().parents[1]
old_protocol = ROOT / "config/nor/protocols/m36-e8-128k.json"
canonical_protocol = ROOT / "config/nor/protocols/intel-e8-rww-128k.json"
m36_profile = ROOT / "config/nor/chips/m36.json"

assert not old_protocol.exists(), (
    "legacy config/nor/protocols/m36-e8-128k.json must be removed; "
    "m36-e8-128k is a target alias, not a second protocol"
)
assert canonical_protocol.exists(), "canonical Intel E8 protocol is missing"

proto = json.loads(canonical_protocol.read_text(encoding="utf-8"))
chip = json.loads(m36_profile.read_text(encoding="utf-8"))

assert proto["key"] == "intel-e8-rww-128k"
assert proto["driver_enum"] == "IntelE8BufferedRww"
assert chip["protocol_ref"] == "intel-e8-rww-128k"
assert "m36-e8-128k" in chip.get("aliases", [])

print("PASS M36 protocol canonicalization: one Intel E8 protocol; legacy M36 key retained only as target alias")
