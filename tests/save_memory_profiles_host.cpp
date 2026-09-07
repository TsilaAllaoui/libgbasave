#include "gbasave/save_memory_profiles.h"
#include "gbasave/embedded/save_memory_patch.h"
#include <cstdio>
#include <cstring>

static SfwSavePlan plan_for(uint8_t type) {
    SfwSavePlan p{}; sfw_saveplan_init(&p,0x400000u); p.save_type=type; return p;
}

int main() {
    if(!gbasave_validate_compiled_save_memory_profiles()) return 1;
    if(gbasave_save_memory_profile_count()<3u) return 2;
    const auto *linear=gbasave_save_memory_profile_find("fram_linear_64k");
    const auto *banked=gbasave_save_memory_profile_find("fram_dual64_romwrite_bit0");
    const auto *sram=gbasave_save_memory_profile_find("sram_linear_32k");
    if(!linear||!banked||!sram) return 3;
    if(linear->target.technology!=GBASAVE_SAVE_MEMORY_TECH_FRAM||banked->target.bank_count!=2u||sram->target.technology!=GBASAVE_SAVE_MEMORY_TECH_SRAM) return 4;
    if(std::strcmp(gbasave_save_memory_technology_name(banked->target.technology),"FRAM")) return 5;

    GbasaveSaveMemoryPatchPlan out{};
    auto flash1m=plan_for(SFW_SAVE_FLASH1024K);
    if(gbasave_save_memory_patch_plan_build(&out,&flash1m,&linear->target,0x400000u,0x400000u,0x1000000u,0,0u)!=GBASAVE_SAVE_MEMORY_PLAN_UNSUPPORTED) return 6;
    if(gbasave_save_memory_patch_plan_build(&out,&flash1m,&banked->target,0x400000u,0x400000u,0x1000000u,0,0u)!=GBASAVE_SAVE_MEMORY_PLAN_OK) return 7;
    if(out.route!=GBASAVE_SAVE_MEMORY_ROUTE_FLASH1M_BANKED||out.payload_bytes==0u) return 8;

    auto native=plan_for(SFW_SAVE_SRAM);
    if(gbasave_save_memory_patch_plan_build(&out,&native,&sram->target,0x400000u,0x400000u,0x800000u,0,0u)!=GBASAVE_SAVE_MEMORY_PLAN_OK) return 9;
    if(out.route!=GBASAVE_SAVE_MEMORY_ROUTE_DIRECT_SRAM||out.overlay_active) return 10;

    auto flash512=plan_for(SFW_SAVE_FLASH512K);
    if(gbasave_save_memory_patch_plan_build(&out,&flash512,&linear->target,0x400000u,0x400000u,0x800000u,0,0u)!=GBASAVE_SAVE_MEMORY_PLAN_OK) return 11;
    if(out.route!=GBASAVE_SAVE_MEMORY_ROUTE_FLASH512||!out.overlay_active) return 12;

    std::puts("PASS save-memory profiles: SRAM/FRAM technology-neutral capability routing and fail-closed capacity rules");
    return 0;
}
