#!/usr/bin/env python3
"""Rank GBABR plan entries for hardware-compatibility coverage testing.

The ranking is deliberately capability-oriented. It favors ROMs that exercise
rare save-handler layouts, duplicated handler copies, unusual region/IRQ
complexity, and large ROM sizes. It never encodes title-specific patch rules.
"""

from __future__ import annotations

import argparse
import collections
import json
from pathlib import Path
from typing import Iterable


def operation_signature(plan: dict) -> tuple[int, ...]:
    return tuple(int(op.get("kind", -1)) for op in plan.get("ops", ()))


def load_ready_plans(path: Path) -> list[dict]:
    root = json.loads(path.read_text(encoding="utf-8"))
    return [plan for plan in root["plans"] if plan.get("ready")]


def score_plan(plan: dict, signature_counts: collections.Counter[tuple[str, tuple[int, ...]]]) -> tuple[float, list[str]]:
    signature = operation_signature(plan)
    sig_key = (str(plan["save_type_name"]), signature)
    rarity = max(1, signature_counts[sig_key])

    score = 100.0 / rarity
    reasons = [f"handler signature occurs {rarity} time(s)"]

    op_count = int(plan.get("op_count", 0))
    if op_count >= 4:
        score += min(op_count, 24) * 2.5
        reasons.append(f"{op_count} save primitive patch sites")

    region_count = int(plan.get("region_count", 0))
    if region_count >= 8:
        score += min(region_count, 80) * 0.35
        reasons.append(f"{region_count} GBABR regions")

    irq_count = int(plan.get("irq_count", 0))
    if irq_count >= 4:
        score += min(irq_count, 24) * 1.5
        reasons.append(f"{irq_count} IRQ candidates")

    rom_size = int(plan.get("rom_size", 0))
    if rom_size >= 0x01000000:
        score += 18
        reasons.append(f"large ROM 0x{rom_size:X}")
    elif rom_size >= 0x00800000:
        score += 8

    return score, reasons


def rank(plans: Iterable[dict], tested_codes: set[str], max_rom_size: int | None) -> list[tuple[float, dict, list[str]]]:
    plans = list(plans)
    sig_counts = collections.Counter((str(p["save_type_name"]), operation_signature(p)) for p in plans)
    ranked = []
    for plan in plans:
        if str(plan["game_code"]) in tested_codes:
            continue
        if max_rom_size is not None and int(plan["rom_size"]) > max_rom_size:
            continue
        score, reasons = score_plan(plan, sig_counts)
        ranked.append((score, plan, reasons))
    ranked.sort(key=lambda item: (-item[0], item[1]["save_type_name"], item[1]["file_name"]))
    return ranked


def main() -> int:
    ap = argparse.ArgumentParser(description="Rank GBABRPLANS entries by new hardware-coverage value.")
    ap.add_argument("json", nargs="?", default="data/GBABRPLANS.json")
    ap.add_argument("--tested-code", action="append", default=[], help="game code already hardware-tested; may be repeated")
    ap.add_argument("--max-rom-size", type=lambda value: int(value, 0), help="exclude ROMs larger than this many bytes")
    ap.add_argument("--limit", type=int, default=20)
    ns = ap.parse_args()

    plans = load_ready_plans(Path(ns.json))
    ranked = rank(plans, set(ns.tested_code), ns.max_rom_size)

    print(f"Ready plans: {len(plans)}")
    if ns.max_rom_size is not None:
        print(f"ROM-size ceiling: 0x{ns.max_rom_size:X}")
    print()
    print("score  type       code  size       ops regions irq  file")
    print("-----  ---------  ----  ---------  --- ------- ---  ----")
    for score, plan, reasons in ranked[: max(0, ns.limit)]:
        print(
            f"{score:5.1f}  {plan['save_type_name']:<9}  {plan['game_code']:<4}  "
            f"0x{int(plan['rom_size']):07X}  {int(plan['op_count']):>3} "
            f"{int(plan['region_count']):>7} {int(plan['irq_count']):>3}  {plan['file_name']}"
        )
        print("       " + "; ".join(reasons))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
