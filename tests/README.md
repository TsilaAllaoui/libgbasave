# Regression tests

The suite contains current behavior tests plus several historically named tests retained because they protect specific hardware lessons. Production source does not key behavior from those phase/game names.

Run:

```sh
python3 tools/build_runtime.py --root .
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
for test in tests/test_*.py; do python3 "$test" || exit 1; done
```

Exact-ROM tests for F-Zero and SMA4 report `SKIP` when those historical fixtures are not present. MZM and WL4 tests use `/mnt/data/rom_inputs/` when available.

Key current guards include:

- RAM-only ordinary SRAM behavior and hotkey-only NOR writes;
- A/B snapshot power-cut and generation rollover models;
- SRAM RAM-mirror inference and ambiguity fail-closed behavior;
- structural preload/self-test reload detection;
- generic exact storage preservation;
- SuperFW-first hole selection with local ROM-scan fallback;
- runtime relocation/undefined-helper audit;
- FLASH/EEPROM append-only models.
For recompiled/relocated FLASH1M libraries that keep Nintendo setup/ABI semantics but not stock byte signatures, run the generic semantic fallback regression with any legal fixture:

```sh
GBASAVE_SEMANTIC_FLASH_FIXTURE=/path/to/recompiled_flash1m.gba \
GBASAVEHANDLER_BIN=/path/to/GBASaveHandler \
python3 tests/test_semantic_flash_fallback.py
```

The regression deliberately contains no game title, game code, hash, or fixed ROM offset. It verifies structural FLASH discovery plus both the banked FRAM route and a NOR-only route.

