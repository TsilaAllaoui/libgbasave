#pragma once

#include "gbasave/streaming/save_memory.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GbasaveSaveMemoryProfileDescriptor {
    const char *key;
    const char *display_name;
    GbasaveSaveMemoryTarget target;
    const char *qualification_state;
    const char *qualification_note;
} GbasaveSaveMemoryProfileDescriptor;

const GbasaveSaveMemoryProfileDescriptor *gbasave_save_memory_profiles_data(void);
size_t gbasave_save_memory_profile_count(void);
const GbasaveSaveMemoryProfileDescriptor *gbasave_save_memory_profile_find(const char *key);
int gbasave_validate_compiled_save_memory_profiles(void);
const char *gbasave_save_memory_technology_name(uint8_t technology);

#ifdef __cplusplus
}
#endif
