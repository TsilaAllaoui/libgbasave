#pragma once
#include "gbasave/rom_image.h"
#include "gbasave/save_patcher.h"
#include "gbasave/save_library.h"

namespace gbasave {

bool canUseM36HardwareProvenRoute(const RomImage &rom, const SaveLibraryMatch &saveLibrary);
PatchReport patchM36HardwareProvenRoute(
    RomImage &rom,
    const SaveLibraryMatch &saveLibrary,
    const PatchOptions &options);

} // namespace gbasave
