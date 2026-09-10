#pragma once

#include "gbasave/rom_image.h"
#include "gbasave/save_memory_profiles.h"
#include "gbasave/streaming/save_memory.h"

#include <cstddef>
#include <string>

namespace gbasave {

enum class SavePlanAnalysisSource {
    ExactDatabase,
    SignatureScan,
    SemanticFlashStructure,
};

const char *toString(SavePlanAnalysisSource source);

struct SaveMemoryPatchResult {
    SfwSavePlan savePlan{};
    GbasaveSaveMemoryPatchPlan patchPlan{};
    std::string profileKey;
    std::string profileDisplayName;
    SavePlanAnalysisSource analysisSource{SavePlanAnalysisSource::SignatureScan};
    std::size_t originalRomBytes{};
    std::size_t outputRomBytes{};
};

/* High-level in-memory adapter for desktop/tests. Embedded consumers should use
 * the allocation-free C planner/composer directly with their random-access I/O.
 * The patch semantics are identical. */
class SaveMemoryPatcher {
public:
    SfwSavePlan analyze(const RomImage &rom) const;
    SaveMemoryPatchResult patch(
        RomImage &rom,
        const GbasaveSaveMemoryProfileDescriptor &profile,
        std::size_t romCapacityBytes = 32u * 1024u * 1024u) const;
};

} // namespace gbasave
