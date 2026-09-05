#pragma once

#include "gbasave/save_patcher.h"
#include "gbasave/save_library.h"

#include <cstddef>
#include <string>

namespace gbasave {

std::string formatPatchReport(
    const std::string &version,
    const std::string &inputHash,
    const std::string &outputHash,
    std::size_t originalRomBytes,
    const SaveLibraryMatch &saveMatch,
    const PatchOptions &options,
    const PatchReport &patchReport);

} // namespace gbasave
