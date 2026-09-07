#!/usr/bin/env python3
"""Validate v1.4 split NOR plug-ins and emit the compiled profile table.

Layout:
  config/nor/protocols/*.json  reusable electrical/runtime protocol recipes
  config/nor/chips/*.json      physical chip/cart identities selecting a protocol

Safety model:
- JSON is declarative and build-time only; patched ROMs never parse writable JSON.
- A recipe must compile to one of the reviewed native runtime drivers.
- Known-driver recipe signatures are checked so a profile cannot silently change
  bus commands while retaining a hardware-proven driver enum.
- Unknown recipes fail closed and require a new reusable native driver.
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any

MAX_RANGES = 4
# Public schema names accepted by the profile compiler.  The E8 driver was
# renamed upstream to describe the actual hardware protocol.  Keep the old
# spelling as a compatibility alias so older profile packs continue to build.
DRIVER_ENUM_ALIASES = {
    "IntelStatusRegisterWord10": "IntelE8BufferedRww",
}
ALLOWED_ENUMS = {
    "IntelStatusRegister",
    "IntelE8BufferedRww",
    "IntelStatusRegisterWord10",
    "IntelRelativeWordProgram",
    "AmdUnlockWordProgram",
}
DIRECT_PROTOCOL_DEFAULTS = {
    "IntelStatusRegister": False,
    "IntelE8BufferedRww": True,
    "IntelRelativeWordProgram": False,
    "AmdUnlockWordProgram": False,
}
ALLOWED_RECIPE_OPS = {
    "WRITE", "POLL_SR7", "POLL_DQ6_DQ5", "CHECK_STATUS", "RESET_ARRAY",
    "VERIFY_EXACT", "BUFFER_PROGRAM",
}
ALLOWED_ADDRESSES = {"TARGET", "SECTOR", "UNLOCK1", "UNLOCK2", "BANK_BASE", "DIE_BASE"}


def _norm_value(value: Any) -> str:
    if isinstance(value, int):
        return f"0x{value:X}"
    return str(value)


def recipe_signature(recipe: Any) -> tuple:
    if recipe is None:
        return tuple()
    out = []
    for step in recipe:
        op = step.get("op")
        address = step.get("address", "")
        value = _norm_value(step["value"]) if "value" in step else ""
        mask = _norm_value(step["mask"]) if "mask" in step else ""
        size = str(step.get("bytes", ""))
        out.append((op, address, value, mask, size))
    return tuple(out)


# These are compiler contracts, not runtime interpreters. A protocol recipe may
# omit driver_enum and be inferred only when it exactly matches one of these
# audited signatures. This gives FlashGBX-like profile convenience without
# allowing arbitrary JSON to become executable NOR traffic.
DRIVER_SIGNATURES = {
    "IntelStatusRegister": recipe_signature([
        {"op":"WRITE","address":"TARGET","value":0x40},
        {"op":"WRITE","address":"TARGET","value":"DATA"},
        {"op":"POLL_SR7","address":"TARGET"},
        {"op":"RESET_ARRAY","address":"TARGET"},
        {"op":"VERIFY_EXACT","address":"TARGET","value":"DATA"},
    ]),
    # The Intel E8 buffered driver is validated structurally below because
    # current profiles may use any reviewed power-of-two chunk up to the
    # 64-byte M36 program-buffer limit.  Do not pin the schema to 32 bytes.
    "IntelRelativeWordProgram": recipe_signature([
        {"op":"WRITE","address":"TARGET","value":0x70},
        {"op":"POLL_SR7","address":"TARGET"},
        {"op":"WRITE","address":"TARGET","value":0x40},
        {"op":"WRITE","address":"TARGET","value":"DATA"},
        {"op":"POLL_SR7","address":"TARGET"},
        {"op":"CHECK_STATUS","mask":0x18},
        {"op":"WRITE","address":"TARGET","value":0x50},
        {"op":"WRITE","address":"TARGET","value":0xFF},
        {"op":"VERIFY_EXACT","address":"TARGET","value":"DATA"},
    ]),
    "AmdUnlockWordProgram": recipe_signature([
        {"op":"WRITE","address":"UNLOCK1","value":0xAA},
        {"op":"WRITE","address":"UNLOCK2","value":0x55},
        {"op":"WRITE","address":"UNLOCK1","value":0xA0},
        {"op":"WRITE","address":"TARGET","value":"DATA"},
        {"op":"POLL_DQ6_DQ5","address":"TARGET"},
        {"op":"WRITE","address":"TARGET","value":0xF0},
        {"op":"VERIFY_EXACT","address":"TARGET","value":"DATA"},
    ]),
}


def _load_json(path: Path) -> dict:
    try:
        obj = json.loads(path.read_text(encoding="utf-8"))
    except Exception as exc:
        raise SystemExit(f"{path}: invalid JSON: {exc}") from exc
    if not isinstance(obj, dict):
        raise SystemExit(f"{path}: top-level object must be an object")
    return obj


def _validate_recipe(path: Path, name: str, recipe: Any) -> None:
    if recipe is None:
        return
    if not isinstance(recipe, list) or not recipe:
        raise SystemExit(f"{path}: {name} must be null or a non-empty list")
    for idx, step in enumerate(recipe):
        if not isinstance(step, dict):
            raise SystemExit(f"{path}: {name}[{idx}] must be an object")
        op = step.get("op")
        if op not in ALLOWED_RECIPE_OPS:
            raise SystemExit(f"{path}: {name}[{idx}] unsupported op {op!r}")
        address = step.get("address")
        if address is not None and address not in ALLOWED_ADDRESSES:
            raise SystemExit(f"{path}: {name}[{idx}] unsupported address token {address!r}")
        if op == "WRITE" and "value" not in step:
            raise SystemExit(f"{path}: {name}[{idx}] WRITE requires value")
        if op == "CHECK_STATUS" and "mask" not in step:
            raise SystemExit(f"{path}: {name}[{idx}] CHECK_STATUS requires mask")
        if op == "BUFFER_PROGRAM" and int(step.get("bytes", 0)) <= 0:
            raise SystemExit(f"{path}: {name}[{idx}] BUFFER_PROGRAM requires positive bytes")


def _canonical_driver(name: str) -> str:
    return DRIVER_ENUM_ALIASES.get(name, name)


def _validate_e8_recipe(path: Path, obj: dict) -> None:
    recipe = obj.get("program_recipe")
    unit = int(obj.get("program_unit_bytes", 0))
    if unit <= 0 or unit > 64 or (unit & (unit - 1)) != 0:
        raise SystemExit(f"{path}: IntelE8BufferedRww program_unit_bytes must be a power of two in 1..64")
    expected = recipe_signature([
        {"op":"BUFFER_PROGRAM","address":"TARGET","bytes":unit},
        {"op":"POLL_SR7","address":"TARGET"},
        {"op":"VERIFY_EXACT","address":"TARGET","value":"DATA"},
    ])
    if recipe_signature(recipe) != expected:
        raise SystemExit(
            f"{path}: program_recipe no longer matches native driver IntelE8BufferedRww; "
            "expected BUFFER_PROGRAM(TARGET, program_unit_bytes), POLL_SR7, VERIFY_EXACT")


def _infer_driver(path: Path, obj: dict) -> str:
    sig = recipe_signature(obj.get("program_recipe"))
    explicit = obj.get("driver_enum")
    if explicit is not None:
        if explicit not in ALLOWED_ENUMS:
            raise SystemExit(f"{path}: unsupported driver_enum {explicit}")
        canonical = _canonical_driver(explicit)
        if canonical == "IntelE8BufferedRww":
            _validate_e8_recipe(path, obj)
            return canonical
        expected = DRIVER_SIGNATURES.get(canonical)
        if expected is not None and sig != expected:
            raise SystemExit(
                f"{path}: program_recipe no longer matches native driver {canonical}; "
                "create a new reusable protocol/driver instead of changing a proven recipe")
        return canonical

    matches = [driver for driver, expected in DRIVER_SIGNATURES.items() if sig == expected]
    # E8 inference is deliberately structural so it remains valid if the
    # reviewed program-buffer chunk changes from 32 to 64 bytes.
    try:
        _validate_e8_recipe(path, obj)
        matches.append("IntelE8BufferedRww")
    except SystemExit:
        pass
    if len(matches) != 1:
        raise SystemExit(
            f"{path}: program_recipe does not map uniquely to a reviewed native driver; "
            "add a reusable runtime driver before enabling this protocol")
    return matches[0]


def load_protocols(protocol_dir: Path) -> dict[str, dict]:
    protocols: dict[str, dict] = {}
    for path in sorted(protocol_dir.glob("*.json")):
        obj = _load_json(path)
        if obj.get("schema_version") != 2 or obj.get("kind") != "protocol":
            raise SystemExit(f"{path}: expected schema_version=2 kind=protocol")
        for key in ("key", "display_name", "description", "program_command", "erase_block_bytes",
                    "program_unit_bytes", "requires_ram_execution", "rom_address_space_limit",
                    "mutable_main_array_end", "runtime_execution_bank_bytes",
                    "supports_block_erase", "program_recipe"):
            if key not in obj:
                raise SystemExit(f"{path}: missing {key}")
        key = obj["key"]
        if key in protocols:
            raise SystemExit(f"duplicate protocol key {key}")
        _validate_recipe(path, "program_recipe", obj.get("program_recipe"))
        _validate_recipe(path, "erase_recipe", obj.get("erase_recipe"))
        obj["driver_enum"] = _infer_driver(path, obj)
        # Direct-protocol capability belongs to the reviewed native driver, not
        # to per-profile JSON.  Older profile packs may still carry the legacy
        # boolean, including stale values.  Accept it as a deprecated hint and
        # always canonicalize to the native-driver contract.
        expected_direct = DIRECT_PROTOCOL_DEFAULTS[obj["driver_enum"]]
        if "supports_direct_protocol_engine" in obj and \
           bool(obj["supports_direct_protocol_engine"]) != expected_direct:
            print(
                f"{path}: warning: deprecated supports_direct_protocol_engine="
                f"{bool(obj['supports_direct_protocol_engine'])} ignored; native driver "
                f"{obj['driver_enum']} requires {expected_direct}",
                file=sys.stderr)
        obj["supports_direct_protocol_engine"] = expected_direct
        if int(obj["capacity_limit"] if "capacity_limit" in obj else obj["rom_address_space_limit"]) <= 0:
            raise SystemExit(f"{path}: invalid capacity limit")
        program_unit = int(obj["program_unit_bytes"])
        if program_unit <= 0 or (program_unit & (program_unit - 1)) != 0:
            raise SystemExit(f"{path}: program_unit_bytes must be a positive power of two")
        erase_bytes = int(obj["erase_block_bytes"])
        if bool(obj["supports_block_erase"]):
            if erase_bytes <= 0 or obj.get("erase_recipe") is None:
                raise SystemExit(f"{path}: erase-capable protocol requires erase_block_bytes and erase_recipe")
        else:
            if erase_bytes != 0 or obj.get("erase_recipe") is not None:
                raise SystemExit(f"{path}: program-only protocol must expose no runtime block erase")
        limit = int(obj["rom_address_space_limit"])
        mutable = int(obj["mutable_main_array_end"])
        if limit <= 0 or limit > 0x02000000 or mutable <= 0 or mutable > limit:
            raise SystemExit(f"{path}: invalid GBA NOR address-space/mutable bounds")
        ranges = obj.get("storage_forbidden_ranges", [])
        if len(ranges) > MAX_RANGES:
            raise SystemExit(f"{path}: too many storage forbidden ranges")
        previous_end = -1
        for r in sorted(ranges, key=lambda x: int(x["offset"])):
            off, size = int(r["offset"]), int(r["bytes"])
            if off < 0 or size <= 0 or off + size > limit:
                raise SystemExit(f"{path}: invalid forbidden range")
            if off < previous_end:
                raise SystemExit(f"{path}: overlapping forbidden ranges")
            previous_end = off + size
        obj["_path"] = path
        protocols[key] = obj
    if not protocols:
        raise SystemExit(f"no NOR protocols found in {protocol_dir}")
    return protocols


def _id_summary(obj: dict) -> str:
    rows = []
    for entry in obj.get("reference_ids", []):
        off = int(entry.get("offset", 0))
        mfr = entry.get("manufacturer")
        dev = entry.get("device", entry.get("device_word"))
        if mfr is None or dev is None:
            continue
        rows.append(f"+0x{off:X}:0x{int(mfr):X}/0x{int(dev):X}")
    return ", ".join(rows) if rows else "unspecified"


def load_chips(chip_dir: Path, protocols: dict[str, dict]) -> list[dict]:
    chips: list[dict] = []
    keys: set[str] = set()
    aliases: set[str] = set()
    defaults: dict[str, str] = {}
    for path in sorted(chip_dir.glob("*.json")):
        obj = _load_json(path)
        if obj.get("schema_version") != 2 or obj.get("kind") != "chip":
            raise SystemExit(f"{path}: expected schema_version=2 kind=chip")
        for key in ("key", "display_name", "capacity_bytes", "protocol_ref", "qualification"):
            if key not in obj:
                raise SystemExit(f"{path}: missing {key}")
        key = str(obj["key"])
        if key in keys or key in aliases:
            raise SystemExit(f"{path}: duplicate chip key/alias {key}")
        keys.add(key)
        for alias in obj.get("aliases", []):
            alias = str(alias)
            if alias in keys or alias in aliases:
                raise SystemExit(f"{path}: duplicate chip alias {alias}")
            aliases.add(alias)
        pkey = obj["protocol_ref"]
        if pkey not in protocols:
            raise SystemExit(f"{path}: unknown protocol_ref {pkey}")
        protocol = protocols[pkey]
        capacity = int(obj["capacity_bytes"])
        if capacity <= 0 or capacity > int(protocol["rom_address_space_limit"]):
            raise SystemExit(f"{path}: capacity exceeds protocol/GBA address-space limit")
        for r in protocol.get("storage_forbidden_ranges", []):
            if int(r["offset"]) + int(r["bytes"]) > capacity:
                raise SystemExit(f"{path}: protocol forbidden range exceeds chip capacity")
        for entry in obj.get("reference_ids", []):
            off = int(entry.get("offset", 0))
            if off < 0 or off >= capacity:
                raise SystemExit(f"{path}: reference ID offset is outside chip capacity")
        qual = obj["qualification"]
        if not isinstance(qual, dict) or qual.get("state") not in {"hardware-proven", "unqualified", "compatibility-only", "diagnostic-only"}:
            raise SystemExit(f"{path}: invalid qualification.state")
        if obj.get("default_for_protocol"):
            if pkey in defaults:
                raise SystemExit(f"{path}: protocol {pkey} already has default target {defaults[pkey]}")
            defaults[pkey] = key
        obj["_path"] = path
        obj["_id_summary"] = _id_summary(obj)
        chips.append(obj)
    if not chips:
        raise SystemExit(f"no NOR chip profiles found in {chip_dir}")
    for pkey in protocols:
        users = [c["key"] for c in chips if c["protocol_ref"] == pkey]
        if not users:
            raise SystemExit(
                f"protocol {pkey}: no chip profile references this protocol; "
                "remove the orphan protocol or migrate a chip profile to it")
        if pkey not in defaults:
            if len(users) == 1:
                defaults[pkey] = users[0]
            else:
                raise SystemExit(f"protocol {pkey}: exactly one chip must set default_for_protocol=true")
    return chips


def q(value: Any) -> str:
    return json.dumps(str(value))


def emit(root: Path, out: Path) -> None:
    protocol_dir = root / "config" / "nor" / "protocols"
    chip_dir = root / "config" / "nor" / "chips"
    protocols = load_protocols(protocol_dir)
    chips = load_chips(chip_dir, protocols)

    protocol_items = list(protocols.items())
    lines = [
        "// Auto-generated by tools/gen_nor_profiles.py. Do not edit.",
        "#pragma once", "", "#include <array>", "", "namespace gbasave {", ""
    ]
    lines.append(f"inline constexpr std::array<NorBackendDescriptor, {len(protocol_items)}> kGeneratedNorBackends = {{")
    for pkey, p in protocol_items:
        ranges = p.get("storage_forbidden_ranges", [])
        ritems = [f'NorStorageForbiddenRange{{{int(r["offset"])}u, {int(r["bytes"])}u}}' for r in ranges]
        ritems += ["NorStorageForbiddenRange{}"] * (MAX_RANGES - len(ritems))
        lines += [
            "    NorBackendDescriptor{",
            f'        NorFlashType::{p["driver_enum"]}, {q(pkey)}, {q(p["display_name"])}, {q(p["description"])},',
            f'        0x{int(p["program_command"]):04X}u, 0x{int(p.get("runtime_program_selector", p["program_command"])):04X}u, {int(p["erase_block_bytes"])}u, {int(p["program_unit_bytes"])}u,',
            f'        {str(bool(p["requires_ram_execution"])).lower()}, {int(p["rom_address_space_limit"])}u, {int(p["mutable_main_array_end"])}u,',
            f'        {int(p["runtime_execution_bank_bytes"])}u, {str(bool(p["supports_direct_protocol_engine"])).lower()}, {str(bool(p["supports_block_erase"])).lower()},',
            "        {" + ", ".join(ritems) + f"}}, {len(ranges)}u",
            "    },"
        ]
    lines += ["};", "", "struct GeneratedNorTargetAlias { const char *alias; std::size_t targetIndex; };", ""]

    enum_by_protocol = {k: v["driver_enum"] for k, v in protocol_items}
    lines.append(f"inline constexpr std::array<NorTargetProfileDescriptor, {len(chips)}> kGeneratedNorTargets = {{")
    for c in chips:
        qual = c["qualification"]
        note = qual.get("note", c.get("notes", ""))
        lines.append(
            "    NorTargetProfileDescriptor{" +
            f'{q(c["key"])}, {q(c["display_name"])}, NorFlashType::{enum_by_protocol[c["protocol_ref"]]}, {int(c["capacity_bytes"])}u, '
            f'{q(c["protocol_ref"])}, {q(qual["state"])}, {q(c["_id_summary"])}, {q(note)}' + "},"
        )
    lines += ["};", ""]

    aliases = []
    for i, c in enumerate(chips):
        aliases.append((c["key"], i))
        aliases.extend((a, i) for a in c.get("aliases", []))
    lines.append(f"inline constexpr std::array<GeneratedNorTargetAlias, {len(aliases)}> kGeneratedNorTargetAliases = {{")
    for alias, index in aliases:
        lines.append(f"    GeneratedNorTargetAlias{{{q(alias)}, {index}u}},")
    lines += ["};", ""]

    defaults = {}
    for i, c in enumerate(chips):
        if c.get("default_for_protocol"):
            defaults[c["protocol_ref"]] = i
    for pkey in protocols:
        if pkey not in defaults:
            users = [i for i, c in enumerate(chips) if c["protocol_ref"] == pkey]
            if len(users) == 1:
                defaults[pkey] = users[0]
    lines.append(f"inline constexpr std::array<std::size_t, {len(protocol_items)}> kGeneratedNorDefaultTargetByBackend = {{")
    for pkey, _ in protocol_items:
        lines.append(f"    {defaults[pkey]}u,")
    lines += ["};", "", "} // namespace gbasave", ""]

    out.parent.mkdir(parents=True, exist_ok=True)
    text = "\n".join(lines)
    out.write_text(text, encoding="utf-8")
    print(f"NOR profiles: {len(protocol_items)} protocols, {len(chips)} chips")
    print(f"generated: {out}")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default=str(Path(__file__).resolve().parents[1]))
    args = ap.parse_args()
    root = Path(args.root).resolve()
    emit(root, root / "generated" / "nor_profile_db.h")


if __name__ == "__main__":
    main()
