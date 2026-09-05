#include "gbasave/engine.h"

#include "gbasave/flash_scanner.h"
#include "gbasave/sha256.h"

namespace gbasave {
namespace {

std::string holeSummary(const RomHoleCatalog &holes)
{
    if (holes.gbabrDatabaseMatched) {
        return "GBABR_v" + std::to_string(holes.gbabrDatabaseVersion) + " MATCH (" +
            std::to_string(holes.databaseErasedHoles.size()) + " verified erased regions)";
    }
    return "GBABR_v" + std::to_string(holes.gbabrDatabaseVersion) + " NO_MATCH -> APPEND_ONLY";
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
        const auto flashMap = FlashScanner{}.scan(rom);
        result.textReport += "\n" + FlashScanner::toText(rom, flashMap, result.inputSha256);
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
