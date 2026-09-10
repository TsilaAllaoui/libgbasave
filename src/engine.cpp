#include "gbasave/engine.h"

#include "gbasave/flash_scanner.h"
#include "gbasave/save_memory_patcher.h"
#include "gbasave/sha256.h"

#include <stdexcept>

namespace gbasave {
namespace {

std::string holeSummary(const RomHoleCatalog &holes)
{
    if (holes.gbabrDatabaseMatched) {
        return "GBABR_v" + std::to_string(holes.gbabrDatabaseVersion) + " MATCH (" +
            std::to_string(holes.databaseErasedHoles.size()) + " verified erased regions)";
    }
    return "GBABR_v" + std::to_string(holes.gbabrDatabaseVersion) +
        " NO_MATCH -> VERIFIED_TRAILING_FF_OR_APPEND";
}

} // namespace

AnalysisResult SaveEngine::analyze(const RomImage &rom, SaveType requestedSaveType) const
{
    AnalysisResult result;
    result.requestedSaveType = requestedSaveType;
    result.inputSha256 = sha256Hex(rom.bytes());
    result.holeCatalog = discoverRomHoles(rom);
    result.effectiveSaveType =
        requestedSaveType == SaveType::Unknown && result.holeCatalog.gbabrSaveType != SaveType::Unknown
            ? result.holeCatalog.gbabrSaveType
            : requestedSaveType;
    result.saveLibrary = SaveLibraryScanner{}.scan(rom, result.effectiveSaveType);

    result.textReport = SaveLibraryScanner::toText(rom, result.saveLibrary);
    result.textReport += "ROM SHA256: " + result.inputSha256 + "\n";
    result.textReport += "Hole database: " + holeSummary(result.holeCatalog) + "\n";

    if (result.saveLibrary.type == SaveType::Flash1M && result.saveLibrary.primitiveHooks.empty()) {
        try {
            const auto flashMap = FlashScanner{}.scan(rom);
            result.textReport += "\n" + FlashScanner::toText(rom, flashMap, result.inputSha256);
        } catch (const std::runtime_error &primitiveError) {
            // A rebuilt ROM may make low-level primitive ownership ambiguous
            // while still exposing a complete, signature/structure-proven
            // public FLASH API.  Keep analysis useful and let target routing
            // choose whether that normalized plan is sufficient.
            try {
                const auto publicPlan = SaveMemoryPatcher{}.analyze(rom);
                if (publicPlan.save_type != SFW_SAVE_FLASH1024K)
                    throw;
                result.textReport +=
                    "\nFLASH primitive map: not unique (" + std::string(primitiveError.what()) + ")\n"
                    "Normalized public FLASH API: COMPLETE; direct/public-ABI-capable targets may use it.\n";
            } catch (...) {
                throw std::runtime_error(primitiveError.what());
            }
        }
    } else if (result.saveLibrary.type == SaveType::Flash1M) {
        result.textReport += "\nExact-plan multi-library FLASH1M: all validated primitive copies will be bridged.\n";
    }

    return result;
}

PatchResult SaveEngine::patch(
    RomImage &rom,
    const AnalysisResult &analysis,
    const PatchOptions &options) const
{
    PatchResult result;
    result.analysis = analysis;
    result.originalRomBytes = rom.size();
    result.inputSha256 = analysis.inputSha256.empty() ? sha256Hex(rom.bytes()) : analysis.inputSha256;
    result.report = SavePatcher{}.patchToTargetStorage(rom, analysis.saveLibrary, options);
    result.outputSha256 = sha256Hex(rom.bytes());
    return result;
}

PatchResult SaveEngine::patch(
    RomImage &rom,
    SaveType requestedSaveType,
    const PatchOptions &options) const
{
    return patch(rom, analyze(rom, requestedSaveType), options);
}

} // namespace gbasave
