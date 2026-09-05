#pragma once

#include "gbasave/hole_finder.h"
#include "gbasave/rom_image.h"
#include "gbasave/save_library.h"
#include "gbasave/save_patcher.h"

#include <cstddef>
#include <string>

namespace gbasave {

// Structured result of ROM/save-library analysis. Consumers can render their own
// UI from these fields; textReport preserves the reference CLI's detailed report.
struct AnalysisResult {
    SaveType requestedSaveType{SaveType::Unknown};
    SaveType effectiveSaveType{SaveType::Unknown};
    SaveLibraryMatch saveLibrary;
    RomHoleCatalog holeCatalog;
    std::string inputSha256;
    std::string textReport;
};

// Result of applying a save patch. The output image is the RomImage passed to
// SaveEngine::patch; this object carries reproducibility/report metadata only.
struct PatchResult {
    AnalysisResult analysis;
    PatchReport report;
    std::size_t originalRomBytes{};
    std::string inputSha256;
    std::string outputSha256;
};

class SaveEngine {
public:
    AnalysisResult analyze(
        const RomImage &rom,
        SaveType requestedSaveType = SaveType::Unknown) const;

    PatchResult patch(
        RomImage &rom,
        const AnalysisResult &analysis,
        const PatchOptions &options) const;

    PatchResult patch(
        RomImage &rom,
        SaveType requestedSaveType,
        const PatchOptions &options) const;
};

} // namespace gbasave
