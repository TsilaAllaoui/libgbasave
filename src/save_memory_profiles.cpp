#include "gbasave/save_memory_profiles.h"
#include "save_memory_profile_db.h"

namespace {
int text_equal(const char *a,const char *b) {
    if(!a||!b) return 0;
    while(*a&&*b) { if(*a++!=*b++) return 0; }
    return *a==*b;
}
}

extern "C" {
const GbasaveSaveMemoryProfileDescriptor *gbasave_save_memory_profiles_data(void) { return kGbasaveSaveMemoryProfiles; }
size_t gbasave_save_memory_profile_count(void) { return kGbasaveSaveMemoryProfileCount; }
const GbasaveSaveMemoryProfileDescriptor *gbasave_save_memory_profile_find(const char *key) {
    for(size_t i=0;i<kGbasaveSaveMemoryProfileCount;i++) if(text_equal(kGbasaveSaveMemoryProfiles[i].key,key)) return &kGbasaveSaveMemoryProfiles[i];
    return nullptr;
}
int gbasave_validate_compiled_save_memory_profiles(void) {
    if(kGbasaveSaveMemoryProfileCount==0u) return 0;
    for(size_t i=0;i<kGbasaveSaveMemoryProfileCount;i++) {
        const auto &p=kGbasaveSaveMemoryProfiles[i]; const auto &t=p.target;
        if(!p.key||!*p.key||!p.display_name||!*p.display_name||t.total_bytes==0u||t.window_bytes==0u||t.bank_count==0u) return 0;
        if(t.window_bytes*t.bank_count!=t.total_bytes||t.window_bytes>0x10000u) return 0;
        if((t.capabilities&GBASAVE_SAVE_MEMORY_CAP_SRAM_WINDOW)!=0u&&t.gba_window_base!=0x0E000000u) return 0;
        if(t.bank_count>1u&&(t.selector_kind==GBASAVE_SAVE_MEMORY_SELECTOR_NONE||t.selector_gba_address==0u)) return 0;
        for(size_t j=i+1;j<kGbasaveSaveMemoryProfileCount;j++) if(text_equal(p.key,kGbasaveSaveMemoryProfiles[j].key)) return 0;
    }
    return 1;
}
const char *gbasave_save_memory_technology_name(uint8_t technology) {
    switch(technology) {
    case GBASAVE_SAVE_MEMORY_TECH_SRAM: return "SRAM";
    case GBASAVE_SAVE_MEMORY_TECH_FRAM: return "FRAM";
    default: return "UNKNOWN";
    }
}
}
