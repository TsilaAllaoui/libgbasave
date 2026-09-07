#include "gbasave/embedded/fram_patch.h"

extern "C" {
int gbasave_fram_patch_plan_build(GbasaveFramPatchPlan *out,const SfwSavePlan *plan,const GbasaveFramTarget *target,
                                  uint32_t original,uint32_t virtual_size,uint32_t capacity,int trailing,uint32_t slot)
{
    return gbasave_save_memory_patch_plan_build(out,plan,target,original,virtual_size,capacity,trailing,slot);
}
void gbasave_fram_patch_apply_overlay(const GbasaveFramPatchPlan *plan,uint32_t off,uint8_t *dst,uint32_t n)
{
    gbasave_save_memory_patch_apply_overlay(plan,off,dst,n);
}
int gbasave_fram_patch_trailing_ff_probe_needed(const SfwSavePlan *plan,uint32_t virtual_size,uint32_t capacity)
{
    return gbasave_save_memory_patch_trailing_ff_probe_needed(plan,virtual_size,capacity);
}
const char *gbasave_fram_route_name(uint8_t route)
{
    /* Preserve historical strings for existing log parsers. */
    switch(route) {
    case GBASAVE_FRAM_ROUTE_DIRECT_SRAM: return "DIRECT_FRAM";
    case GBASAVE_FRAM_ROUTE_EEPROM: return "EEPROM_TO_FRAM";
    case GBASAVE_FRAM_ROUTE_FLASH512: return "FLASH512_TO_FRAM";
    case GBASAVE_FRAM_ROUTE_FLASH1M_BANKED: return "FLASH1M_TO_BANKED_FRAM";
    default: return "NONE";
    }
}
uint32_t gbasave_fram_runtime_size(void) { return gbasave_save_memory_banked_runtime_size(); }
}
