#pragma once

#include "gbasave/embedded/exact_rom_plan.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GBASAVE_NOR_SAVE_LAYOUT_MAX_STORAGE 16u

/*
 * Consumer-neutral NOR save-layout planner.
 *
 * This API describes only the address-space constraints needed by the GBA
 * save runtime embedded in the patched ROM. It deliberately does NOT describe
 * how a host flashes the cartridge (buffer programming, timings, mapper
 * transactions, command quirks, USB/link transport, etc.). Those remain the
 * responsibility of GBABR/OpenFlash/FlashGBX.
 */
typedef struct GbasaveNorSaveTarget {
    uint32_t capacity_bytes;
    uint32_t erase_block_bytes;
    uint32_t rww_bank_bytes;
} GbasaveNorSaveTarget;

typedef enum GbasaveNorSaveLayoutRoute {
    GBASAVE_NOR_SAVE_LAYOUT_NONE = 0,
    GBASAVE_NOR_SAVE_LAYOUT_RWW_SRAM = 1,
    GBASAVE_NOR_SAVE_LAYOUT_RWW_EEPROM = 2,
    GBASAVE_NOR_SAVE_LAYOUT_RWW_FLASH_COMPACT = 3
} GbasaveNorSaveLayoutRoute;

typedef enum GbasaveNorSaveLayoutFlag {
    GBASAVE_NOR_SAVE_LAYOUT_PAYLOAD_INTERNAL_FF = 1u << 0,
    GBASAVE_NOR_SAVE_LAYOUT_PAYLOAD_APPENDED = 1u << 1,
    GBASAVE_NOR_SAVE_LAYOUT_PAYLOAD_ERASE_BLOCK = 1u << 2,
    GBASAVE_NOR_SAVE_LAYOUT_STORAGE_SPARSE = 1u << 3,
    GBASAVE_NOR_SAVE_LAYOUT_RWW_SEPARATE_BANK = 1u << 4,
    GBASAVE_NOR_SAVE_LAYOUT_SOURCE_SUPERFW = 1u << 5,
    GBASAVE_NOR_SAVE_LAYOUT_STORAGE_INTERNAL_FF = 1u << 6
} GbasaveNorSaveLayoutFlag;

typedef struct GbasaveNorSaveLayout {
    uint8_t route;
    uint8_t storage_count;
    uint16_t _reserved0;
    uint32_t flags;
    uint32_t payload_offset;
    uint32_t payload_reserved_bytes;
    uint32_t storage_block_bytes;
    uint32_t storage[GBASAVE_NOR_SAVE_LAYOUT_MAX_STORAGE];
    uint32_t output_bytes;
    uint32_t minimum_capacity_bytes;
    uint32_t selected_ff_region_start;
    uint32_t selected_ff_region_bytes;
} GbasaveNorSaveLayout;

typedef enum GbasaveNorSaveLayoutResult {
    GBASAVE_NOR_SAVE_LAYOUT_INVALID = -1,
    GBASAVE_NOR_SAVE_LAYOUT_UNSUPPORTED = 0,
    GBASAVE_NOR_SAVE_LAYOUT_READY = 1
} GbasaveNorSaveLayoutResult;

/*
 * Resolve a save-runtime/storage layout using only analyzer facts and target
 * geometry. No chip name, cart name, host flasher strategy, or I/O callback is
 * consulted. The result is deterministic and allocation-free.
 */
int gbasave_nor_save_layout_resolve(
    const GbasaveExactRomPlan *rom_plan,
    const GbasaveNorSaveTarget *target,
    GbasaveNorSaveLayout *out);

#ifdef __cplusplus
}
#endif
