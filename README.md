# libgbasave 1.0.0

`libgbasave` is the canonical GBA save-analysis and ROM save-patching library shared by GBASaveHandler, GBABR, OpenFlash, and future frontends.

The central rule is simple:

> **Consumers own transport/UI/filesystem. libgbasave owns save semantics and patch planning.**

Do not create a second EEPROM/FLASH/SRAM/FRAM patch engine in a consumer.

## What libgbasave owns

- GBA save-type discovery and exact-plan lookup;
- EEPROM, FLASH512, FLASH1M, and SRAM patch semantics;
- NOR storage planning and runtime composition;
- bounded-memory streaming analysis/patch planning for NDS/ESP32;
- SRAM/FRAM capability profiles and save-memory patch planning;
- NOR chip identities, aliases, protocol definitions, and reviewed native-driver mapping;
- shared M36 direct-RWW runtime assets;
- compatibility/runtime databases and generators;
- patch reports and reproducibility helpers;
- regression models protecting hardware-proven behavior.

Consumers own file I/O, FAT/SD/LittleFS, Wi-Fi/USB/serial/link transport, cart probing, mapper control, physical erase/program operations, menus, progress UI, and save-manager policy.

## Repository layout

```text
libgbasave/
├── CMakeLists.txt
├── README.md
├── include/gbasave/
│   ├── gbasave.h                 # preferred desktop/high-level umbrella
│   ├── streaming.h               # bounded-memory embedded umbrella
│   ├── engine.h
│   ├── save_memory_patcher.h
│   ├── save_memory_profiles.h
│   └── ...
├── src/
│   ├── streaming/                # allocation-free planner/composer
│   └── ...                       # normal desktop/high-level engine
├── config/
│   ├── nor/chips/                # physical chip identity/capacity/qualification
│   ├── nor/protocols/            # reusable electrical protocols
│   └── save_memory/profiles/     # SRAM/FRAM capabilities
├── data/                          # generated-source inputs/runtime assets
├── generated/                     # generated headers checked/validated at build
├── runtime/                       # hardware runtime source
├── tools/                         # generators/build helpers
└── tests/                         # regression models
```

## Public APIs

### Desktop/high-level API

Prefer:

```cpp
#include <gbasave/gbasave.h>
```

NOR patching:

```cpp
using namespace gbasave;

auto &profile = parseNorTargetProfile("m36");
auto options = makePatchOptionsForNorProfile(profile);
auto result = SaveEngine{}.patch(rom, SaveType::Unknown, options);
```

SRAM/FRAM patching:

```cpp
using namespace gbasave;

const auto *profile = gbasave_save_memory_profile_find(
    "fram_dual64_romwrite_bit0");
if (!profile) throw std::runtime_error("unknown profile");

auto result = SaveMemoryPatcher{}.patch(rom, *profile, 32u * 1024u * 1024u);
```

`SaveMemoryPatcher` is a library-owned convenience adapter. It delegates to the same bounded-memory planner/composer used by embedded consumers; desktop frontends must not reimplement those semantics.

### Embedded/bounded-memory API

Prefer:

```cpp
#include <gbasave/streaming.h>
```

The streaming frontend is allocation-free and intended for NDS/ESP32 integration. It owns analysis and patch-plan semantics while the consumer supplies random-access reads/writes and physical flash transport.

## CMake targets

A normal build produces:

```text
gbasave::shared      libgbasave.so     desktop/reference consumers
gbasave::static      libgbasave.a      static high-level consumers
gbasave::streaming   libgbasave_streaming.a bounded-memory embedded consumers
```

The normal shared/static library internally links the streaming core because `SaveMemoryPatcher` uses that canonical implementation. Consumers never compile private libgbasave `.cpp` files.

Build:

```bash
rm -rf build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

As a submodule:

```cmake
add_subdirectory(externals/libgbasave)
target_link_libraries(MyDesktopTool PRIVATE gbasave::shared)
# or
target_link_libraries(MyEmbeddedTool PRIVATE gbasave::streaming)
```

## NOR profile model

Physical chip identity and electrical protocol are separate.

Current canonical protocols include:

```text
intel-word40
intel-e8-rww-128k
intel-relative-word40
amd-unlock-word
```

`m36-e8-128k` is retained only as a user-facing M36 alias. There is intentionally **no second `m36-e8-128k.json` protocol file**.

The canonical M36 protocol uses:

```text
driver enum: IntelE8BufferedRww
```

Historical `IntelStatusRegisterWord10` remains a source/ABI-compatible enum alias.

`supports_direct_protocol_engine` is deprecated JSON metadata. Native reviewed driver capability is authoritative. If an old profile carries a conflicting legacy value, the generator warns and canonicalizes it instead of making consumers maintain duplicate policy.

Unknown protocol recipes and orphan protocol files fail closed during generation.

## Save-memory profiles

Current generated profiles describe capabilities rather than cart brands. They include generic SRAM/FRAM forms plus the hardware-proven dual-bank FRAM mapping.

A profile describes properties such as:

- SRAM vs FRAM technology;
- total backing bytes;
- visible GBA SRAM window;
- bank count;
- bank-selector semantics;
- supported direct SRAM / EEPROM / FLASH conversion routes.

GBASaveHandler/GBABR/OpenFlash should select a profile and call libgbasave, not branch on cartridge names to implement patch logic themselves.

## Runtime rules

Hardware-proven runtime behavior is frozen unless a change is intentionally reviewed and requalified.

Rebuild runtime blobs with:

```bash
cmake --build build --target gbasave_runtime
```

or:

```bash
python3 tools/build_runtime.py --root . --variant all
```

Never promote changed runtime bytes accidentally during a cleanup/refactor.

## Tests

Many historical tests use the reference GBASaveHandler consumer. `tests/test_support.py` now supports separate sibling repositories:

```text
workspace/
├── libgbasave/
└── GBASaveHandler/
    └── build/GBASaveHandler
```

You can override the executable explicitly:

```bash
GBASAVEHANDLER_BIN=/path/to/GBASaveHandler python3 tests/test_v150_libgbasave_extraction.py
```

Useful focused checks:

```bash
python3 tests/test_v151_nor_schema_compat.py
python3 tests/test_nor_protocol_coverage.py
python3 tests/test_v131_nor_profile_plugins.py
python3 tests/test_v140_nor_plugin_framework.py
python3 tests/test_v150_libgbasave_extraction.py
python3 tests/test_multisave_profiles.py
python3 tests/test_v130_compact_eeprom.py
```

Some historical tests require private/non-distributable ROM fixtures and will report `SKIP` when those fixtures are not present.

## Hardware/compatibility invariants

- M6/M6M, M36, M6MGD137 and MX26 hardware-proven runtime paths must not change during architectural cleanup.
- The LeafGreen M36 exact layout/audio-safe runtime work remains part of the shared exact-plan/runtime assets.
- Protocol choice must be capability/profile driven, not game-title driven.
- Unknown electrical behavior fails closed.
- Physical flashing remains a consumer responsibility.

## Current limitations

- `SaveEngine`/`RomImage` still holds a complete ROM in memory; this is appropriate for desktop tools.
- Embedded consumers should use `gbasave::streaming` and their own random-access I/O adapter instead of `RomImage`.
- The public/internal header split can still be tightened as consumers stabilize.
- C++ binary ABI across arbitrary compiler/toolchain combinations is not guaranteed; submodule/source builds are the preferred integration model.

## TODO

- Add a formal random-access adapter facade on top of the streaming planner for common NDS/ESP32 use.
- Add machine-readable plan/report serialization for cross-consumer parity checks.
- Continue moving implementation-only headers out of the public include surface.
- Add `find_package(libgbasave CONFIG)` packaging if consumers stop using submodules.
- Modernize remaining historical tests that assume legacy workspace/build layouts.
- Keep GBABR/OpenFlash migrations thin: delete duplicated patch policy only after parity + hardware qualification.

## Maintenance notes for the 2026-09-07 upstream migration

- Added the bounded-memory `gbasave::streaming` frontend without replacing the normal `SaveEngine` API.
- Added save-memory capability profiles and shared SRAM/FRAM planner/runtime assets.
- Added `SaveMemoryPatcher` as a public desktop adapter using the same streaming semantics.
- Fixed the CMake public boundary so `gbasave::shared/static` actually provide the declared save-memory API instead of requiring consumers to compile private source files.
- Added save-memory headers to `gbasave/gbasave.h`.
- Canonicalized M36 to the generic `intel-e8-rww-128k` protocol and `IntelE8BufferedRww` driver while preserving historical aliases.
- Removed the duplicate orphan `m36-e8-128k.json` protocol.
- Updated test consumer discovery for separate sibling repositories.
