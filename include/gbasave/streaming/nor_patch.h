#pragma once

#include "gbasave/streaming/nor_layout.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GBASAVE_NOR_PATCH_MAX_PAYLOAD_BYTES 0x2000u

/*
 * Allocation-free direct-RWW ROM patch composer.
 *
 * libgbasave owns only the bytes that the GBA ROM must contain. A consumer
 * still owns cartridge probing, erase/program algorithms, timing, buffering,
 * mapper transactions, physical placement, transport, verification UI, etc.
 */
typedef enum GbasaveNorPatchRoute {
    GBASAVE_NOR_PATCH_NONE = 0,
    GBASAVE_NOR_PATCH_RWW_SRAM = 1,
    GBASAVE_NOR_PATCH_RWW_EEPROM = 2,
    GBASAVE_NOR_PATCH_RWW_FLASH_COMPACT = 3
} GbasaveNorPatchRoute;

typedef struct GbasaveNorPatchPlan {
    uint8_t route;
    uint8_t save_type;
    uint8_t irq_count;
    uint8_t storage_count;
    uint32_t source_rom_bytes;
    uint32_t output_rom_bytes;
    uint32_t minimum_target_capacity_bytes;
    uint32_t payload_offset;
    uint32_t payload_bytes;
    uint32_t payload_entry_offset;
    uint32_t payload_dispatch_offset;
    uint32_t original_entry_address;
    uint32_t new_entry_word;
    uint32_t storage_block_bytes;
    uint32_t storage[GBASAVE_NOR_SAVE_LAYOUT_MAX_STORAGE];
    uint16_t hotkey_raw;
    uint16_t _reserved0;
} GbasaveNorPatchPlan;

typedef enum GbasaveNorPatchResult {
    GBASAVE_NOR_PATCH_INVALID = -1,
    GBASAVE_NOR_PATCH_UNSUPPORTED = 0,
    GBASAVE_NOR_PATCH_READY = 1
} GbasaveNorPatchResult;

/*
 * Build the configured GBA payload for an already-resolved generic layout.
 * payload_out is caller-owned so this API remains usable on NDS/ESP32.
 */
int gbasave_nor_patch_plan_build(
    const GbasaveExactRomPlan *rom_plan,
    const GbasaveNorSaveLayout *layout,
    uint16_t hotkey_raw,
    uint8_t *payload_out,
    uint32_t payload_capacity,
    GbasaveNorPatchPlan *out);

/*
 * Overlay all in-ROM changes that intersect [range_offset, range_offset+bytes).
 * This is suitable for streaming flashers: read a source chunk, call this,
 * then send/program the transformed chunk. It also works on a synthetic 0xFF
 * range containing an appended payload.
 */
int gbasave_nor_patch_apply_overlay(
    const GbasaveExactRomPlan *rom_plan,
    const GbasaveNorPatchPlan *patch_plan,
    const uint8_t *payload,
    uint32_t range_offset,
    uint8_t *data,
    uint32_t bytes);

#ifdef __cplusplus
}
#endif
