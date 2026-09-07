#pragma once

/* Deprecated compatibility API. New consumers should include
 * <gbasave/embedded/save_memory_patch.h>. */
#include "gbasave/embedded/save_memory_patch.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef GbasaveSaveMemoryTarget GbasaveFramTarget;
typedef GbasaveSaveMemoryPatchPlan GbasaveFramPatchPlan;

enum GbasaveFramCapability {
    GBASAVE_FRAM_CAP_NONVOLATILE           = GBASAVE_SAVE_MEMORY_CAP_NONVOLATILE,
    GBASAVE_FRAM_CAP_BYTE_RW               = GBASAVE_SAVE_MEMORY_CAP_BYTE_RW,
    GBASAVE_FRAM_CAP_SRAM_WINDOW           = GBASAVE_SAVE_MEMORY_CAP_SRAM_WINDOW,
    GBASAVE_FRAM_CAP_BANKED_WINDOW         = GBASAVE_SAVE_MEMORY_CAP_BANKED_WINDOW,
    GBASAVE_FRAM_CAP_DIRECT_SRAM_GAMEPLAY  = GBASAVE_SAVE_MEMORY_CAP_DIRECT_SRAM_GAMEPLAY,
    GBASAVE_FRAM_CAP_EEPROM_TO_RAM         = GBASAVE_SAVE_MEMORY_CAP_EEPROM_TO_RAM,
    GBASAVE_FRAM_CAP_FLASH512_TO_RAM       = GBASAVE_SAVE_MEMORY_CAP_FLASH512_TO_RAM,
    GBASAVE_FRAM_CAP_FLASH1M_TO_BANKED_RAM = GBASAVE_SAVE_MEMORY_CAP_FLASH1M_TO_BANKED_RAM,
};

enum GbasaveFramRoute {
    GBASAVE_FRAM_ROUTE_NONE = GBASAVE_SAVE_MEMORY_ROUTE_NONE,
    GBASAVE_FRAM_ROUTE_DIRECT_SRAM = GBASAVE_SAVE_MEMORY_ROUTE_DIRECT_SRAM,
    GBASAVE_FRAM_ROUTE_EEPROM = GBASAVE_SAVE_MEMORY_ROUTE_EEPROM,
    GBASAVE_FRAM_ROUTE_FLASH512 = GBASAVE_SAVE_MEMORY_ROUTE_FLASH512,
    GBASAVE_FRAM_ROUTE_FLASH1M_BANKED = GBASAVE_SAVE_MEMORY_ROUTE_FLASH1M_BANKED,
};

enum GbasaveFramPlanResult {
    GBASAVE_FRAM_PLAN_OK = GBASAVE_SAVE_MEMORY_PLAN_OK,
    GBASAVE_FRAM_PLAN_NO_SAVE = GBASAVE_SAVE_MEMORY_PLAN_NO_SAVE,
    GBASAVE_FRAM_PLAN_UNSUPPORTED = GBASAVE_SAVE_MEMORY_PLAN_UNSUPPORTED,
    GBASAVE_FRAM_PLAN_CAPACITY = GBASAVE_SAVE_MEMORY_PLAN_CAPACITY,
    GBASAVE_FRAM_PLAN_INVALID = GBASAVE_SAVE_MEMORY_PLAN_INVALID,
};

int gbasave_fram_patch_plan_build(
    GbasaveFramPatchPlan *out,
    const SfwSavePlan *save_plan,
    const GbasaveFramTarget *target,
    uint32_t original_size,
    uint32_t base_virtual_size,
    uint32_t rom_capacity,
    int trailing_ff_available,
    uint32_t trailing_ff_slot);
void gbasave_fram_patch_apply_overlay(const GbasaveFramPatchPlan *plan,uint32_t chunk_offset,uint8_t *chunk,uint32_t chunk_bytes);
int gbasave_fram_patch_trailing_ff_probe_needed(const SfwSavePlan *save_plan,uint32_t base_virtual_size,uint32_t rom_capacity);
const char *gbasave_fram_route_name(uint8_t route);
uint32_t gbasave_fram_runtime_size(void);

#ifdef __cplusplus
}
#endif
