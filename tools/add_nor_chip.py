#!/usr/bin/env python3
"""Create a GBASaveHandler v1.4 NOR chip profile.

The normal path is chip-only: choose a reusable protocol and supply physical
identity/capacity. `--from-flashgbx` can safely import the subset of FlashGBX
profile metadata needed here and infer a reviewed protocol for known command
shapes. Unknown command sequences fail closed.
"""
from __future__ import annotations

import argparse
import ast
import json
import re
import subprocess
import sys
from pathlib import Path
from typing import Any


def parse_int(value: str | int) -> int:
    if isinstance(value, int):
        return value
    return int(str(value).strip(), 0)


def slug(value: str) -> str:
    text = value.strip().lower()
    text = re.sub(r"[^a-z0-9]+", "-", text).strip("-")
    if not text:
        raise ValueError("empty profile key")
    return text


def load_protocols(root: Path) -> dict[str, dict]:
    out = {}
    for path in sorted((root / "config/nor/protocols").glob("*.json")):
        obj = json.loads(path.read_text(encoding="utf-8"))
        out[obj["key"]] = obj
    return out


def parse_flashgbx_config(path: Path) -> dict:
    text = path.read_text(encoding="utf-8")
    # FlashGBX profile files are JSON-like Python literals: they commonly use
    # hexadecimal integers plus JSON null/true/false. Convert only those tokens
    # then use ast.literal_eval; never exec/eval arbitrary source.
    text = re.sub(r"\bnull\b", "None", text)
    text = re.sub(r"\btrue\b", "True", text, flags=re.I)
    text = re.sub(r"\bfalse\b", "False", text, flags=re.I)
    try:
        obj = ast.literal_eval(text)
    except Exception as exc:
        raise SystemExit(f"cannot parse FlashGBX profile safely: {exc}") from exc
    if not isinstance(obj, dict):
        raise SystemExit("FlashGBX profile must be an object")
    return obj


def _sequence(commands: dict, name: str) -> list[tuple[Any, Any]]:
    out = []
    for row in commands.get(name, []) or []:
        if not isinstance(row, (list, tuple)) or len(row) < 2:
            continue
        out.append((row[0], row[1]))
    return out


def infer_protocol_from_flashgbx(profile: dict) -> str | None:
    commands = profile.get("commands", {}) or {}
    single = _sequence(commands, "single_write")
    erase = _sequence(commands, "sector_erase")
    command_set = str(profile.get("command_set", "")).upper()

    if command_set == "INTEL":
        # D137-like target-relative status/program path.
        if single == [("PA", 0x70), ("PA", 0x40), ("PA", "PD")] and erase == [
            ("SA", 0x60), ("SA", 0xD0), ("SA", 0x20), ("SA", 0xD0)
        ]:
            return "intel-relative-word40"
        # Plain Intel target-local 40/data path used by the frozen M6/M6M
        # backend. Be strict: 70-at-address-0 variants are not silently folded
        # into this driver.
        if single in [[("PA", 0x40), ("PA", "PD")], [("PA", 0x70), ("PA", 0x40), ("PA", "PD")]]:
            return "intel-word40"
        # FlashGBX profiles may describe buffered write differently; only infer
        # M36 when E8 is explicitly present in a write-buffer command.
        for key in ("buffer_write", "write_buffer", "buffered_write"):
            seq = _sequence(commands, key)
            if any(value == 0xE8 for _addr, value in seq):
                return "m36-e8-128k"

    if command_set == "AMD":
        if len(single) >= 4:
            values = [row[1] for row in single[:4]]
            if values == [0xAA, 0x55, 0xA0, "PD"] and not erase:
                return "amd-unlock-word"
    return None


def extract_flashgbx_ids(profile: dict) -> list[dict]:
    raw = profile.get("flash_ids", []) or []
    if not raw:
        return []
    first = raw[0]
    result = []

    # Multi-offset set: [offset, [id bytes...], offset, [id bytes...], ...]
    if isinstance(first, list) and len(first) >= 2 and isinstance(first[0], int) and isinstance(first[1], list):
        for index in range(0, len(first) - 1, 2):
            off, ident = first[index], first[index + 1]
            if not isinstance(off, int) or not isinstance(ident, list):
                break
            if len(ident) >= 3:
                result.append({"offset": off, "manufacturer": int(ident[0]), "device": int(ident[2])})
        return result

    # Standard FlashGBX ID bytes [MFRlo, MFRhi, DEVlo, DEVhi, ...].
    if isinstance(first, list) and len(first) >= 3 and all(isinstance(v, int) for v in first[:3]):
        result.append({"offset": 0, "manufacturer": int(first[0]), "device": int(first[2])})
    return result


def interactive_select_protocol(protocols: dict[str, dict]) -> str:
    keys = sorted(protocols)
    print("Known NOR protocols:")
    for idx, key in enumerate(keys, 1):
        p = protocols[key]
        erase = f"erase {p['erase_block_bytes']} B" if p.get("supports_block_erase") else "program-only"
        print(f"  {idx}. {key:26s} {p['display_name']} ({erase})")
    while True:
        raw = input("Protocol number/key: ").strip()
        if raw.isdigit() and 1 <= int(raw) <= len(keys):
            return keys[int(raw) - 1]
        if raw in protocols:
            return raw
        print("Unknown protocol.")


def build_chip(args, root: Path, protocols: dict[str, dict]) -> dict:
    imported = parse_flashgbx_config(Path(args.from_flashgbx)) if args.from_flashgbx else None

    display = args.display_name
    if not display and imported:
        names = imported.get("names", []) or []
        display = str(names[0]) if names else None
    if not display:
        display = input("Chip/display name: ").strip()
    key = args.key or slug(display)

    capacity = parse_int(args.capacity) if args.capacity is not None else None
    if capacity is None and imported and imported.get("flash_size") is not None:
        capacity = int(imported["flash_size"])
    if capacity is None:
        capacity = parse_int(input("Capacity bytes (e.g. 0x800000): ").strip())

    protocol = args.protocol
    inferred = infer_protocol_from_flashgbx(imported) if imported else None
    if protocol is None:
        if inferred:
            protocol = inferred
            print(f"Inferred reviewed protocol: {protocol}")
        elif args.non_interactive:
            raise SystemExit("command sequence does not match a reviewed protocol; pass --protocol explicitly or add a reusable protocol/driver")
        else:
            protocol = interactive_select_protocol(protocols)
    if protocol not in protocols:
        raise SystemExit(f"unknown protocol {protocol!r}; use --list-protocols")
    if capacity > int(protocols[protocol]["rom_address_space_limit"]):
        raise SystemExit("chip capacity exceeds selected protocol/GBA limit")

    aliases = [a for a in (args.alias or []) if a and a != key]
    obj = {
        "schema_version": 2,
        "kind": "chip",
        "key": key,
        "aliases": aliases,
        "display_name": display,
        "capacity_bytes": capacity,
        "protocol_ref": protocol,
        "qualification": {
            "state": args.qualification,
            "note": args.note or "New profile; hardware qualification required before promotion."
        }
    }

    ids = extract_flashgbx_ids(imported) if imported else []
    if args.manufacturer is not None or args.device is not None:
        if args.manufacturer is None or args.device is None:
            raise SystemExit("--manufacturer and --device must be supplied together")
        ids = [{"offset": parse_int(args.id_offset or "0"),
                "manufacturer": parse_int(args.manufacturer),
                "device": parse_int(args.device)}]
    if ids:
        obj["reference_ids"] = ids

    if imported:
        obj["imported_from_flashgbx"] = Path(args.from_flashgbx).name
        if inferred is None:
            obj["import_note"] = "Protocol selected explicitly; FlashGBX sequence was not auto-recognized."
        sector = imported.get("sector_size")
        if isinstance(sector, list):
            geometry = []
            for row in sector:
                if isinstance(row, (list, tuple)) and len(row) >= 2:
                    geometry.append({"bytes": int(row[0]), "count": int(row[1])})
            if geometry:
                obj["erase_geometry"] = geometry
    return obj


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    ap.add_argument("--list-protocols", action="store_true")
    ap.add_argument("--from-flashgbx", metavar="PROFILE.txt")
    ap.add_argument("--key")
    ap.add_argument("--display-name")
    ap.add_argument("--capacity", help="bytes, decimal or 0x-prefixed")
    ap.add_argument("--protocol")
    ap.add_argument("--alias", action="append")
    ap.add_argument("--manufacturer")
    ap.add_argument("--device")
    ap.add_argument("--id-offset")
    ap.add_argument("--qualification", default="unqualified",
                    choices=["unqualified", "diagnostic-only", "hardware-proven", "compatibility-only"])
    ap.add_argument("--note")
    ap.add_argument("--output", type=Path, help="override output JSON path")
    ap.add_argument("--dry-run", action="store_true", help="print JSON but do not write")
    ap.add_argument("--non-interactive", action="store_true")
    ap.add_argument("--no-generate", action="store_true", help="do not run gen_nor_profiles.py after writing")
    args = ap.parse_args()

    root = args.root.resolve()
    protocols = load_protocols(root)
    if args.list_protocols:
        for key in sorted(protocols):
            p = protocols[key]
            erase = str(p["erase_block_bytes"]) if p.get("supports_block_erase") else "none"
            print(f"{key}\t{p['display_name']}\terase={erase}\tdriver={p.get('driver_enum','auto')}")
        return

    obj = build_chip(args, root, protocols)
    text = json.dumps(obj, indent=2) + "\n"
    if args.dry_run:
        print(text, end="")
        return

    output = args.output or (root / "config/nor/chips" / f"{obj['key']}.json")
    if output.exists():
        raise SystemExit(f"refusing to overwrite existing profile: {output}")
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(text, encoding="utf-8")
    print(f"Created: {output}")

    if not args.no_generate:
        subprocess.run([sys.executable, str(root / "tools/gen_nor_profiles.py"), "--root", str(root)], check=True)
        print("Profile validation/generation: PASS")
    if obj["qualification"]["state"] != "hardware-proven":
        print("Qualification state: unqualified. Run the generic hardware procedure before promotion.")


if __name__ == "__main__":
    main()
