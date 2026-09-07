#pragma once

#include "gbasave/embedded/superfw_savepatch_port.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Portable, allocation-free patch composer for cartridges that expose
 * byte-addressable save memory through the GBA SRAM window.
 *
 * The backing technology may be SRAM or FRAM.  libgbasave only cares about
 * the electrical/runtime semantics described by GbasaveSaveMemoryTarget.
 * Consumers own cartridge probing, filesystem I/O, mapper setup, UI, and any
 * host-side save backup/restore operations.
 */

enum GbasaveSaveMemoryCapability {
    GBASAVE_SAVE_MEMORY_CAP_NONVOLATILE           = 1u << 0,
    GBASAVE_SAVE_MEMORY_CAP_BYTE_RW               = 1u << 1,
    GBASAVE_SAVE_MEMORY_CAP_SRAM_WINDOW           = 1u << 2,
    GBASAVE_SAVE_MEMORY_CAP_BANKED_WINDOW         = 1u << 3,
    GBASAVE_SAVE_MEMORY_CAP_DIRECT_SRAM_GAMEPLAY  = 1u << 4,
    GBASAVE_SAVE_MEMORY_CAP_EEPROM_TO_RAM         = 1u << 5,
    GBASAVE_SAVE_MEMORY_CAP_FLASH512_TO_RAM       = 1u << 6,
    GBASAVE_SAVE_MEMORY_CAP_FLASH1M_TO_BANKED_RAM = 1u << 7,
};

enum GbasaveSaveMemoryTechnology {
    GBASAVE_SAVE_MEMORY_TECH_UNKNOWN = 0,
    GBASAVE_SAVE_MEMORY_TECH_SRAM = 1,
    GBASAVE_SAVE_MEMORY_TECH_FRAM = 2,
};

enum GbasaveSaveMemorySelectorKind {
    GBASAVE_SAVE_MEMORY_SELECTOR_NONE = 0,
    GBASAVE_SAVE_MEMORY_SELECTOR_ROM_WRITE_DATA_BITS = 1,
};

enum GbasaveSaveMemoryRoute {
    GBASAVE_SAVE_MEMORY_ROUTE_NONE = 0,
    GBASAVE_SAVE_MEMORY_ROUTE_DIRECT_SRAM = 1,
    GBASAVE_SAVE_MEMORY_ROUTE_EEPROM = 2,
    GBASAVE_SAVE_MEMORY_ROUTE_FLASH512 = 3,
    GBASAVE_SAVE_MEMORY_ROUTE_FLASH1M_BANKED = 4,
};

typedef struct GbasaveSaveMemoryTarget {
    uint32_t capabilities;
    uint32_t total_bytes;
    uint32_t window_bytes;
    uint32_t gba_window_base;
    uint32_t selector_gba_address;
    uint16_t selector_fixed_value;
    uint8_t technology;
    uint8_t bank_count;
    uint8_t selector_kind;
    uint16_t selector_mask;
    uint8_t selector_shift;
    uint8_t selector_write_width;
} GbasaveSaveMemoryTarget;

typedef struct GbasaveSaveMemoryPatchPlan {
    uint8_t active;
    uint8_t save_type;
    uint8_t route;
    uint8_t overlay_active;
    uint32_t original_size;
    uint32_t virtual_size;
    uint32_t payload_base;
    uint32_t payload_bytes;
    uint32_t logical_save_bytes;
    GbasaveSaveMemoryTarget target;
    SfwSavePlan save_plan;
} GbasaveSaveMemoryPatchPlan;

enum GbasaveSaveMemoryPlanResult {
    GBASAVE_SAVE_MEMORY_PLAN_OK = 1,
    GBASAVE_SAVE_MEMORY_PLAN_NO_SAVE = 0,
    GBASAVE_SAVE_MEMORY_PLAN_UNSUPPORTED = -1,
    GBASAVE_SAVE_MEMORY_PLAN_CAPACITY = -2,
    GBASAVE_SAVE_MEMORY_PLAN_INVALID = -3,
};

int gbasave_save_memory_patch_plan_build(
    GbasaveSaveMemoryPatchPlan *out,
    const SfwSavePlan *save_plan,
    const GbasaveSaveMemoryTarget *target,
    uint32_t original_size,
    uint32_t base_virtual_size,
    uint32_t rom_capacity,
    int trailing_ff_available,
    uint32_t trailing_ff_slot);

void gbasave_save_memory_patch_apply_overlay(
    const GbasaveSaveMemoryPatchPlan *plan,
    uint32_t chunk_offset,
    uint8_t *chunk,
    uint32_t chunk_bytes);

int gbasave_save_memory_patch_trailing_ff_probe_needed(
    const SfwSavePlan *save_plan,
    uint32_t base_virtual_size,
    uint32_t rom_capacity);

const char *gbasave_save_memory_route_name(uint8_t route);
uint32_t gbasave_save_memory_banked_runtime_size(void);

#ifdef __cplusplus
}
#endif
