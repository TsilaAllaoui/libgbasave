#pragma once

#include "gbasave/save_patcher.h"
#include "gbasave/save_library.h"
#include "gbasave/save_memory_patcher.h"
#include "gbasave/save_memory_profiles.h"

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

std::string formatSaveMemoryPatchReport(
    const std::string &version,
    const std::string &inputHash,
    const std::string &outputHash,
    const SaveMemoryPatchResult &result,
    const GbasaveSaveMemoryProfileDescriptor &profile,
    std::size_t romCapacityBytes);

} // namespace gbasave
