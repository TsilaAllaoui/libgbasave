#pragma once

#include <stdint.h>
#include "gbasave/embedded/superfw_savepatch_port.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Canonical SuperFW compatibility/save-plan database owned by libgbasave.
 * Consumers never carry or parse their own copy. */
int gbasave_superfw_save_plan_lookup(SfwSavePlan *plan,
                                     uint32_t rom_size,
                                     const uint8_t gamecode_revision[5]);
int gbasave_superfw_waitcnt_lookup(SfwCompatPlan *plan,
                                   const uint8_t gamecode_revision[5]);
uint32_t gbasave_superfw_database_size(void);

#ifdef __cplusplus
}
#endif
