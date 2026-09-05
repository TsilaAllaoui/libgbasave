#pragma once

#include "gbasave/gbabr_plan.h"
#include "gbasave/save_library.h"

namespace gbasave {

// Convert an exact shared GBABR plan into patchable Nintendo save primitives.
// The plan is authoritative only after CRC+ROM-size match; machine-code
// signatures are still validated at every primitive whose ABI depends on them.
SaveLibraryMatch scanSaveLibraryFromExactPlan(
    const RomImage &rom,
    SaveType requested,
    const GbabrExactPlan &plan);

} // namespace gbasave
