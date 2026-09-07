# libgbasave 1.0.0 — GBA Save Core

`libgbasave` is the canonical GBA ROM save-patching engine extracted from the hardware-proven **GBASaveHandler v1.4 NOR Plug-in Framework** milestone.

The library exists so GBASaveHandler, GBABR, OpenFlash, and future tools can use one save engine instead of maintaining separate EEPROM/FLASH/SRAM/NOR/FRAM patchers.

## README maintenance rule

**This README is a living engineering document. Every change to libgbasave must update it in the same change.** Enrich the relevant sections with:

- what changed and why;
- new/changed public structures, classes, functions, protocols, profiles, or storage formats;
- build/usage changes;
- hardware qualification state;
- compatibility or migration notes;
- known quirks/limitations;
- TODOs created or completed;
- any invariant a future maintainer must not accidentally break.

Do not let implementation knowledge exist only in chat history or commit messages.

---

## 1. Current milestone and compatibility contract

`libgbasave 1.0.0` is a **behavior-preserving extraction** of GBASaveHandler v1.4. It is not a new save algorithm.

The extraction is accepted only because freshly patched Super Mario Advance 3 outputs remain byte-for-byte identical to the already hardware-proven v1.4 outputs:

| Target | Output SHA-256 | Extraction result |
|---|---|---|
| M6/M6M | `a7288ebae7b72a7eed2f9420723eeb3d714251be41688026976b8d2a597e0e4b` | byte-identical |
| M36 | `cb7ad458ae39c24474cf41703127e45fe2ca85dc04fbfb7a40251e22d5a5c323` | byte-identical |
| M6MGD137 | `61c7c49fa51d2080d06cf9c169a546207a7c3c6d5c0cede4f3809185c621c5ca` | byte-identical |
| MX26L6420MC-90 | `97464138a54b55f45cf6dba55bbd502aa2edb9087e0d83755b4ea9b9eeee0bf5` | byte-identical |

Hardware-proven transaction engines remain frozen unless a change is explicitly qualified on real hardware.

### Runtime byte baselines

There are deliberately two runtime source variants:

- `runtime/legacy/` — the exact v1.3 M6/M6M + M36 runtime source and stack/IRQ workers.
- `runtime/` — the v1.4 extended source used by D137/MX26-capable targets.

Rebuilt runtime binaries currently hash to:

```text
legacy   e1031e63e15385a0ad1e32edf93689be7d3dbb0f54713e3e2816843b9a90b1f0  16964 bytes
extended 4addfc123fa266193065bed407ba8f2ba1179a30e92516c089500221a6f864b8  18116 bytes
```

The namespace/text of generated C++ headers may change during refactors; tests should freeze the **runtime blob bytes**, not incidental header formatting.

---

## 2. Ownership boundary

### libgbasave owns

- GBA ROM structure access (`RomImage`);
- save type/geometry definitions;
- exact shared GBABR plan lookup;
- save-library/signature/primitive discovery;
- executable-reference-safe hole discovery;
- storage layout/allocation;
- EEPROM record lanes and EEPROM8K COMPACT_V2;
- FLASH versioned-slot persistence;
- SRAM live-shadow/retention patching;
- runtime configuration, relocation, and injection;
- NOR electrical/runtime backend descriptors;
- physical NOR chip profiles and aliases;
- M36 compatibility patching;
- patch verification/report domain data;
- runtime/profile generation tools;
- compatibility regression tests.

### Consumers own

- command-line parsing or UI;
- desktop/NDS/ESP32 filesystem APIs;
- SD/FAT/LittleFS/etc.;
- USB, Wi-Fi, serial, GBA link, Slot-2 transport;
- cart power control and hardware probing;
- ROM flashing itself;
- cart/menu/save-manager UI;
- temporary-file policy and user-facing progress.

A consumer must describe the target and ask libgbasave for a patch. It must not implement a parallel EEPROM/FLASH/SRAM compatibility engine.

---

## 3. Source tree

```text
libgbasave/
├── CMakeLists.txt
├── README.md
├── include/gbasave/
│   ├── gbasave.h              # preferred consumer umbrella header
│   ├── version.h
│   ├── engine.h               # high-level orchestration API
│   ├── rom_image.h
│   ├── save_patcher.h
│   ├── nor_backends.h
│   └── ...                    # lower-level domain headers
├── src/
│   ├── engine.cpp
│   ├── rom_image.cpp
│   ├── save_patcher.cpp
│   ├── storage_layout.cpp
│   ├── runtime_image.cpp
│   └── ...
├── runtime/
│   ├── legacy/                # exact M6/M36 runtime baseline
│   ├── gba_runtime.cpp        # extended D137/MX26 runtime
│   ├── sram_hotkey_irq.S
│   ├── sram_stack_worker.S
│   └── linker.ld
├── config/nor/
│   ├── chips/                 # physical identity/capacity/qualification
│   └── protocols/             # reusable reviewed electrical protocols
├── generated/
├── data/
├── tools/
└── tests/
```

---

## 4. Public API model

Consumers should normally include only:

```cpp
#include <gbasave/gbasave.h>
```

All public C++ API is under:

```cpp
namespace gbasave
```

### `RomImage`

`RomImage` is the in-memory representation of a GBA ROM.

```cpp
RomImage rom = RomImage::fromBytes(std::move(bytes));
```

It deliberately has **no `load(path)` or `save(path)` methods**. File I/O was removed during extraction so the core engine does not depend on desktop filesystem APIs. GBASaveHandler supplies its own file adapter; future NDS/ESP32 consumers will supply theirs.

### `SaveEngine`

`SaveEngine` is the high-level entry point.

```cpp
SaveEngine engine;
AnalysisResult analysis = engine.analyze(rom, SaveType::Unknown);
PatchResult result = engine.patch(rom, analysis, options);
```

`AnalysisResult` contains:

- requested and effective save type;
- validated `SaveLibraryMatch`;
- `RomHoleCatalog`;
- input SHA-256;
- reference text analysis report.

`PatchResult` contains:

- the analysis used for the patch;
- structured `PatchReport`;
- original ROM size;
- input/output SHA-256.

The patched bytes are in the `RomImage` supplied to `patch()`.

### `PatchOptions`

`PatchOptions` describes the physical target/runtime/storage policy. Consumers should not manually duplicate profile capacity/backend fields. Resolve a profile and create options with:

```cpp
const auto &profile = parseNorTargetProfile("m6mgd137");
PatchOptions options = makePatchOptionsForNorProfile(profile);
```

Then override only user-requested policy values such as `forcedFlashId`, `flashSpillBlockCount`, `storagePolicy`, or an exact storage-preservation image.

### `SavePatcher`

`SavePatcher` is the lower-level transformation component used by `SaveEngine`:

```cpp
PatchReport patchToTargetStorage(
    RomImage &rom,
    const SaveLibraryMatch &saveLibrary,
    const PatchOptions &options) const;
```

Most consumers should use `SaveEngine`, not call scanners and `SavePatcher` separately.

---

## 5. Build products

A normal CMake build creates both forms from the same object code:

```text
libgbasave.so      desktop/shared consumer
libgbasave.a       static/embedded consumer
```

Build standalone:

```bash
cmake -S libgbasave -B build-libgbasave -DCMAKE_BUILD_TYPE=Release
cmake --build build-libgbasave -j
```

Or as a subdirectory/submodule:

```cmake
add_subdirectory(external/libgbasave)
target_link_libraries(MyFrontend PRIVATE gbasave::static)
```

Desktop consumers may instead link:

```cmake
target_link_libraries(MyFrontend PRIVATE gbasave::shared)
```

### ABI note

Version 1.0 exposes a C++ API. Source/API ownership is now clean, but a C++ `.so` is not promised to be binary-compatible across arbitrary compiler/toolchain combinations. GBABR/OpenFlash will normally consume the static library built by their own toolchain. A small stable C ABI can be added later if a non-C++ consumer actually needs it.

---

## 6. Runtime rebuilds

Rebuild both runtime variants:

```bash
python3 tools/build_runtime.py --root . --variant all
```

Or separately:

```bash
python3 tools/build_runtime.py --root . --variant legacy
python3 tools/build_runtime.py --root . --variant extended
```

Generated files:

```text
generated/runtime_blob.h
generated/runtime_blob_extended.h
```

`build_runtime/runtime.bin` and `.elf` remain compatibility copies of the legacy build for historical regression scripts. Canonical variant outputs live under:

```text
build_runtime/legacy/
build_runtime/extended/
```

Never silently rebuild/promote changed runtime bytes. A runtime byte change requires explicit review and hardware qualification of every affected backend.

---

## 7. NOR plug-in model

Physical chips and electrical protocols are separate:

```text
config/nor/chips/*.json
config/nor/protocols/*.json
```

Current reviewed protocols:

- `intel-word40`
- `m36-e8-128k`
- `intel-relative-word40`
- `amd-unlock-word`

Current hardware-proven physical profiles:

- `m6m`
- `m36`
- `m6mgd137` (`m6mjd137` alias retained)
- `mx26l6420mc-90`

Generate/validate after profile edits:

```bash
python3 tools/gen_nor_profiles.py --root .
```

Create/import a chip profile:

```bash
python3 tools/add_nor_chip.py --help
```

Unknown electrical recipes fail closed; do not guess a native driver from family naming.

---

## 8. Storage policy summary

`compact-v2` is the current default.

- EEPROM512: proven record-lane engine.
- EEPROM8K with bounded erase: COMPACT_V2 A/B lane engine.
- EEPROM8K without bounded erase: automatic program-only record-lane fallback.
- SRAM: proven live RAM/EWRAM + NOR retention engine.
- FLASH512/FLASH1M: proven versioned-slot engine.

No target cartridge SRAM/FRAM is assumed unless a future explicit target/backend says otherwise.

The storage engine is capability-driven. Do not add `if (chip == ...)` logic where `supportsBlockErase`, program unit, execution mode, geometry, forbidden ranges, or another capability can express the distinction.

---

## 9. Tests and hard gates

Run the historical regression scripts from `tests/` after building the workspace consumer. `tests/test_support.py` locates the external GBASaveHandler binary and accepts `GBASAVEHANDLER_BIN` as an override.

Preferred runner:

```bash
python3 tools/run_tests.py --root . --gbasavehandler-bin ../build/GBASaveHandler/GBASaveHandler
```

The runner executes each historical script in its own process because the suite predates a single pytest/unittest convention.

The extraction hard gates are:

1. library and consumer compile warning-free;
2. `nor list/show/validate` work through the library;
3. legacy runtime rebuild is exact;
4. extended runtime rebuild is exact;
5. M6M SMA3 output is byte-identical to v1.4;
6. M36 SMA3 output is byte-identical to v1.4;
7. D137 SMA3 output is byte-identical to v1.4;
8. MX26 SMA3 output is byte-identical to v1.4;
9. no game-title/game-code-specific production routing is introduced;
10. unknown profile/recipe ambiguity fails closed.

---

## 10. Current limitations / quirks

### Full ROM is still held in memory

Version 1.0 inherits GBASaveHandler's `std::vector<uint8_t>` ROM model. This is acceptable for the desktop CLI but is **not the final GBABR/OpenFlash integration API**.

Before migrating the NDS and memory-constrained ESP32 frontends, add a random-access/streaming abstraction so analysis and patch application can operate in bounded chunks without allocating the entire ROM.

The intended direction is an interface roughly equivalent to:

```text
read_at(offset, buffer, size)
write_at(offset, buffer, size)
size()
resize()
flush()
```

Do this without changing patch plans/output bytes.

### Shared analyzer is still workspace-level

`gba_static_analyzer/` remains next to the projects instead of being fully owned/packaged by `libgbasave`. Generated plan data is already consumed by the library. Decide later whether the Python analyzer becomes a sibling tool package or an explicitly versioned source-data generator.

### Public/internal header split is not complete

The preferred consumer include is `gbasave/gbasave.h`, but several low-level headers remain installed/public because they were inherited from GBASaveHandler. Future cleanup may move implementation-only declarations under `src/internal/` after GBABR/OpenFlash integration requirements are known.

---

## 11. TODO roadmap

### Required before GBABR migration

- [ ] Add bounded-memory random-access/streaming ROM I/O.
- [ ] Add an explicit target-capability structure that can represent NOR **and FRAM** without a frontend-specific cart name.
- [ ] Move/define FRAM patch backend in libgbasave based on the proven GBABR behavior.
- [ ] Add plan serialization/hash so desktop/NDS/ESP32 can prove they selected the same patch plan.
- [ ] Add library API tests independent of the GBASaveHandler executable.
- [ ] Decide stable allocation/error API for no-exception embedded builds if devkitARM/ESP-IDF constraints require it.

### GBABR migration

- [ ] Link `libgbasave.a` from devkitARM.
- [ ] Add libfat/random-access adapter.
- [ ] Map GBABR cart detection to libgbasave target capabilities/profile IDs.
- [ ] Run legacy-vs-library dual patch comparison before deleting GBABR's native patcher.
- [ ] Hardware-qualify NOR and FRAM targets.
- [ ] Remove duplicate GBABR patch/analyzer implementation only after parity gates pass.

### OpenFlash migration

- [ ] Link `libgbasave.a` in ESP32 build.
- [ ] Add SD/stream adapter with bounded RAM.
- [ ] Keep GBA-link transport/flashing outside libgbasave.
- [ ] Compare output/plan hashes against desktop and GBABR.
- [ ] Remove OpenFlash's duplicate save patch engine after parity/hardware qualification.

### Later cleanup

- [ ] Add optional stable C ABI only if a real consumer needs it.
- [ ] Split public headers from internal implementation headers.
- [ ] Add semantic version checks for profile DB/runtime ABI.
- [ ] Add install/package config (`find_package(libgbasave CONFIG)`) if external projects stop using submodules.

---

## 12. Update log

### 1.0.0 — initial extraction

- Extracted GBASaveHandler v1.4 save engine into standalone `libgbasave`.
- Renamed engine namespace from `gbasavehandler` to `gbasave`.
- Renamed the in-memory ROM type to `RomImage`.
- Renamed `RomPatcher` to `SavePatcher` and `patchToRomStorage()` to `patchToTargetStorage()`.
- Added high-level `SaveEngine`, `AnalysisResult`, and `PatchResult`.
- Added `gbasave/gbasave.h` umbrella header.
- Removed file loading/saving from the library ROM object.
- Added `makePatchOptionsForNorProfile()` so consumers do not duplicate target-profile geometry/backend mapping.
- Builds both `libgbasave.so` and `libgbasave.a` from common object code.
- Split reproducible legacy vs extended runtime build sources while preserving exact runtime bytes.
- Updated profile generators to emit `namespace gbasave`.
- GBASaveHandler moved to a separate consumer folder and links `libgbasave.so` by default.
- Verified four hardware-proven SMA3 outputs remain byte-identical.
- Clean-room source extraction rebuilds both runtime variants, both library forms, and the GBASaveHandler consumer; full script regression suite passes from the extracted handoff.

### Current NOR profile schema compatibility

The NOR profile compiler accepts the current canonical M36 driver name
`IntelE8BufferedRww` and the historical public spelling
`IntelStatusRegisterWord10`. Both map to ABI value 2; generated code uses the
canonical name.

`supports_direct_protocol_engine` is now a compatibility field rather than a
required per-profile policy knob. When omitted, the generator derives it from
the reviewed native driver contract (`true` for Intel E8 buffered RWW and
`false` for the other current drivers). If a JSON profile specifies the field
explicitly with a conflicting value, generation still fails closed.

Intel E8 recipe validation follows `program_unit_bytes` and accepts reviewed
power-of-two buffered chunks up to the 64-byte M36 program-buffer limit. This
avoids tying the declarative schema to the historical 32-byte chunk while
keeping the actual command sequence strict.
