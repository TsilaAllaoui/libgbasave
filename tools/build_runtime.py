#!/usr/bin/env python3
"""Rebuild the hardware-proven GBA runtime blobs embedded by libgbasave.

legacy   -> v1.3 M6/M6M + M36 runtime (must remain byte-stable)
extended -> v1.4 D137 + MX26-capable runtime
all      -> both (default)
"""
from __future__ import annotations

import argparse
import pathlib
import re
import shutil
import subprocess

RUNTIME_BASE = 0x08400000
EXPORTED_SYMBOLS = [
    "gRuntimeConfig",
    "gbashSwitchFlashBank",
    "gbashReadFlashId",
    "gbashIdentifyFlash",
    "gbashReadFlashByteHelper",
    "gbashReadFlashCore",
    "gbashReadFlash",
    "gbashVerifyFlashCore",
    "gbashVerifyFlashSector",
    "gbashVerifyFlashSectorNBytes",
    "gbashWaitForFlashWrite",
    "gbashProgramFlashSector",
    "gbashProgramFlashSectorAndVerify",
    "gbashProgramFlashSectorAndVerifyNBytes",
    "gbashEraseFlashSector",
    "gbashEraseFlashChip",
    "gbashProgramFlashByte",
    "gbashProgramFlashByteByOffset",
    "gbashReadEepromDword",
    "gbashProgramEepromDword",
    "gbashSramBootInit",
    "gbashSramBootEntry",
    "gbashSramDummyIrq",
    "gbashSramReadMirror",
    "gbashSramWriteMirror",
    "gbashSramVerifyMirror",
    "gbashSramHotkeyCommit",
    "gbashSramHotkeyIrqEntry",
    "gbashSramChainSlotAddressWord",
    "gbashSramFallbackDispatcherWord",
    "gbashNoOp",
]


def run(command: list[str], cwd: pathlib.Path) -> str:
    result = subprocess.run(
        command,
        cwd=cwd,
        check=True,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )
    return result.stdout


def tool(name: str, fallbacks: list[str]) -> str:
    found = shutil.which(name)
    if found:
        return found
    for candidate in fallbacks:
        if pathlib.Path(candidate).exists():
            return candidate
    raise SystemExit(f"required tool not found: {name}")


def build_variant(root: pathlib.Path, variant: str) -> tuple[pathlib.Path, pathlib.Path, int]:
    if variant == "legacy":
        source = root / "runtime/legacy/gba_runtime.cpp"
        irq_source = root / "runtime/legacy/sram_hotkey_irq.S"
        stack_source = root / "runtime/legacy/sram_stack_worker.S"
        header_name = "runtime_blob.h"
        namespace = "generated_runtime"
    elif variant == "extended":
        source = root / "runtime/gba_runtime.cpp"
        irq_source = root / "runtime/sram_hotkey_irq.S"
        stack_source = root / "runtime/sram_stack_worker.S"
        header_name = "runtime_blob_extended.h"
        namespace = "generated_runtime_extended"
    else:
        raise ValueError(variant)

    build = root / "build_runtime" / variant
    generated = root / "generated"
    build.mkdir(parents=True, exist_ok=True)
    generated.mkdir(exist_ok=True)

    clang = tool("clang++", ["/usr/local/swift/usr/bin/clang++"])
    objcopy = tool("llvm-objcopy", ["/usr/local/swift/usr/bin/llvm-objcopy"])
    nm = tool("nm", ["/usr/bin/nm"])
    readelf = tool("readelf", ["/usr/bin/readelf"])

    elf = build / "runtime.elf"
    binary = build / "runtime.bin"
    map_file = build / "runtime.map"

    compile_command = [
        clang,
        "--target=armv4t-none-eabi",
        "-mcpu=arm7tdmi",
        "-mthumb",
        "-std=c++17",
        "-Os",
        "-ffreestanding",
        "-fno-builtin",
        "-fno-exceptions",
        "-fno-rtti",
        "-fno-unwind-tables",
        "-fno-asynchronous-unwind-tables",
        "-fno-stack-protector",
        "-fno-pic",
        "-fno-pie",
        "-nostdlib",
        f"-Wl,-T,{root / 'runtime/linker.ld'}",
        "-Wl,--gc-sections",
        "-Wl,--emit-relocs",
        f"-Wl,-Map,{map_file}",
        str(source),
        str(irq_source),
        str(stack_source),
        "-o",
        str(elf),
    ]
    run(compile_command, root)
    run([objcopy, "-O", "binary", str(elf), str(binary)], root)

    relocation_output = run([readelf, "-r", str(elf)], root)
    symbol_output = run([nm, "-n", str(elf)], root)
    symbols: dict[str, int] = {}
    for line in symbol_output.splitlines():
        match = re.match(r"^([0-9A-Fa-f]+)\s+\S\s+(\S+)$", line)
        if match:
            symbols[match.group(2)] = int(match.group(1), 16)

    missing = [name for name in EXPORTED_SYMBOLS if name not in symbols]
    if missing:
        raise SystemExit(f"{variant}: missing runtime symbols: {', '.join(missing)}")

    blob = binary.read_bytes()
    config_offset = symbols["gRuntimeConfig"] - RUNTIME_BASE
    runtime_end = RUNTIME_BASE + len(blob)

    absolute_relocation_offsets: list[int] = []
    for line in relocation_output.splitlines():
        match = re.match(r"^\s*([0-9A-Fa-f]+)\s+[0-9A-Fa-f]+\s+R_ARM_ABS32\s+([0-9A-Fa-f]+)", line)
        if not match:
            continue
        relocation_address = int(match.group(1), 16)
        target_value = int(match.group(2), 16)
        if not (RUNTIME_BASE <= relocation_address < runtime_end):
            raise SystemExit(f"{variant}: ABS32 relocation outside runtime image: 0x{relocation_address:08X}")
        if not (RUNTIME_BASE <= (target_value & ~1) <= runtime_end):
            raise SystemExit(f"{variant}: ABS32 relocation targets outside runtime image: 0x{target_value:08X}")
        absolute_relocation_offsets.append(relocation_address - RUNTIME_BASE)
    if not absolute_relocation_offsets:
        raise SystemExit(f"{variant}: runtime relocation table unexpectedly contains no R_ARM_ABS32 entries")

    unsupported = []
    for line in relocation_output.splitlines():
        if "R_ARM_" not in line:
            continue
        if "R_ARM_ABS32" in line or "R_ARM_THM_CALL" in line:
            continue
        unsupported.append(line.strip())
    if unsupported:
        raise SystemExit(f"{variant}: unsupported runtime relocations: " + "; ".join(unsupported))

    header = generated / header_name
    with header.open("w", encoding="utf-8") as stream:
        stream.write("#pragma once\n\n#include <cstddef>\n#include <cstdint>\n\n")
        stream.write(f"namespace gbasave::{namespace} {{\n\n")
        stream.write(f"inline constexpr std::uint32_t kRuntimeBaseAddress = 0x{RUNTIME_BASE:08X}u;\n")
        stream.write(f"inline constexpr std::size_t kRuntimeConfigOffset = 0x{config_offset:X}u;\n")
        stream.write("inline constexpr std::size_t kRuntimeAbsolute32Relocations[] = {")
        for offset in absolute_relocation_offsets:
            stream.write(f"0x{offset:X}u, ")
        stream.write("};\n")
        stream.write("inline constexpr std::uint8_t kRuntimeBlob[] = {\n    ")
        for index, value in enumerate(blob):
            stream.write(f"0x{value:02X}, ")
            if (index + 1) % 16 == 0 and index + 1 != len(blob):
                stream.write("\n    ")
        stream.write("\n};\n\n")
        for name in EXPORTED_SYMBOLS:
            offset = (symbols[name] & ~1) - RUNTIME_BASE
            thumb = bool(symbols[name] & 1)
            stream.write(f"inline constexpr std::size_t k_{name}_Offset = 0x{offset:X}u;\n")
            stream.write(f"inline constexpr bool k_{name}_Thumb = {'true' if thumb else 'false'};\n")
        stream.write(f"\n}} // namespace gbasave::{namespace}\n")

    # Legacy compatibility paths used by historical regression scripts.
    if variant == "legacy":
        compat = root / "build_runtime"
        shutil.copy2(elf, compat / "runtime.elf")
        shutil.copy2(binary, compat / "runtime.bin")
        shutil.copy2(map_file, compat / "runtime.map")

    return binary, header, len(blob)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=pathlib.Path, default=pathlib.Path(__file__).resolve().parents[1])
    parser.add_argument("--variant", choices=("legacy", "extended", "all"), default="all")
    args = parser.parse_args()
    root = args.root.resolve()

    variants = ("legacy", "extended") if args.variant == "all" else (args.variant,)
    for variant in variants:
        binary, header, size = build_variant(root, variant)
        print(f"{variant}: {size} bytes -> {binary}")
        print(f"generated: {header}")


if __name__ == "__main__":
    main()
