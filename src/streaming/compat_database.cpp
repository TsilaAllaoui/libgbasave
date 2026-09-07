#include "gbasave/streaming/compat_database.h"
#include "superfw_patch_db.h"

extern "C" int gbasave_superfw_save_plan_lookup(SfwSavePlan *plan,
                                                  uint32_t rom_size,
                                                  const uint8_t gamecode_revision[5])
{
    if (!plan || !gamecode_revision) return 0;
    sfw_saveplan_init(plan, rom_size);
    return sfw_saveplan_db_lookup(plan,
                                  gbasave::generated::kSuperfwPatchDb,
                                  gbasave::generated::kSuperfwPatchDbSize,
                                  gamecode_revision);
}

extern "C" int gbasave_superfw_waitcnt_lookup(SfwCompatPlan *plan,
                                                const uint8_t gamecode_revision[5])
{
    if (!plan || !gamecode_revision) return 0;
    return sfw_waitcnt_db_lookup(plan,
                                 gbasave::generated::kSuperfwPatchDb,
                                 gbasave::generated::kSuperfwPatchDbSize,
                                 gamecode_revision);
}

extern "C" uint32_t gbasave_superfw_database_size(void)
{
    return gbasave::generated::kSuperfwPatchDbSize;
}
