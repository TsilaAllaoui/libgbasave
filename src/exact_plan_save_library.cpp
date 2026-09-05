#include "gbasave/exact_plan_save_library.h"

#include "gbasave/save_marker.h"
#include "gbasave/save_signature.h"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>

namespace gbasave {
namespace {

SavePrimitiveHook thumbHook(SavePrimitiveRole role, std::string name, std::size_t offset)
{
    return {role, std::move(name), offset, offset, 8u, PatchInstructionSet::Thumb};
}

std::size_t planKindCount(const GbabrExactPlan &plan, std::uint8_t kind)
{
    return static_cast<std::size_t>(std::count_if(plan.operations.begin(), plan.operations.end(),
        [kind](const auto &op) { return op.kind == kind; }));
}

std::vector<std::size_t> planOffsets(const GbabrExactPlan &plan, std::uint8_t kind)
{
    std::vector<std::size_t> offsets;
    for (const auto &op : plan.operations)
        if (op.kind == kind)
            offsets.push_back(op.offset);
    return offsets;
}

SaveLibraryMatch scanEeprom(const RomImage &rom, SaveType requested, const GbabrExactPlan &plan)
{
    if (plan.saveType != requested)
        throw std::runtime_error("GBABR exact EEPROM plan save type mismatch");
    const auto markers = findSaveFamilyMarkers(rom, SaveLibraryFamily::Eeprom);
    if (markers.empty())
        throw std::runtime_error("GBABR exact EEPROM plan has no Nintendo EEPROM marker");
    for (const auto &marker : markers) {
        if (marker.marker == "EEPROM_V111")
            throw std::runtime_error("EEPROM_V111 uses an unsupported transfer ABI");
    }
    const auto reads = planKindCount(plan, 1u);
    const auto writes = planKindCount(plan, 2u);
    if (reads == 0u || reads != writes || reads + writes != plan.operations.size())
        throw std::runtime_error("GBABR exact EEPROM plan does not contain balanced read/write primitives");

    SaveLibraryMatch result;
    result.type = requested;
    result.libraryName = "Nintendo " + markers.front().marker +
        (markers.size() > 1u ? " (exact-plan multi-library)" : " (exact-plan)");
    result.marker = markers.front().marker;
    result.markerOffset = markers.front().offset;
    const std::size_t totalBytes = requested == SaveType::Eeprom512 ? 512u : 8u * 1024u;
    result.geometry = {requested, totalBytes, totalBytes, 512u, 1u, totalBytes / 512u};
    for (const auto &op : plan.operations) {
        if (op.offset + 8u > rom.size())
            throw std::runtime_error("GBABR EEPROM primitive lies outside ROM");
        if (op.kind == 1u)
            result.primitiveHooks.push_back(thumbHook(SavePrimitiveRole::EepromReadDword, "ReadEepromDword[plan]", op.offset));
        else if (op.kind == 2u)
            result.primitiveHooks.push_back(thumbHook(SavePrimitiveRole::EepromProgramDword, "ProgramEepromDword[plan]", op.offset));
        else
            throw std::runtime_error("GBABR exact EEPROM plan contains a non-EEPROM operation");
    }
    return result;
}

SaveLibraryMatch scanFlash512(const RomImage &rom, const GbabrExactPlan &plan)
{
    if (plan.saveType != SaveType::Flash512)
        throw std::runtime_error("GBABR exact plan is not FLASH512");
    const auto markers = findSaveFamilyMarkers(rom, SaveLibraryFamily::Flash512);
    const std::size_t copies = planKindCount(plan, 3u);
    if (copies == 0u || markers.size() != copies)
        throw std::runtime_error("GBABR FLASH512 library copy count disagrees with Nintendo markers");
    if (planKindCount(plan, 4u) != copies || planKindCount(plan, 5u) != copies)
        throw std::runtime_error("GBABR FLASH512 plan lacks one erase primitive per library copy");

    const auto readId = expectedPatternMatches(rom, exactPattern("30B591B0684600F0"), copies, "FLASH512 ReadFlashId");
    const auto readCore = expectedPatternMatches(rom, exactPattern("10B5041C531E002A"), copies, "FLASH512 ReadFlash_Core");
    const auto verifyCore = expectedPatternMatches(rom, exactPattern("30B5051C0B1C541E"), copies, "FLASH512 VerifyFlashSector_Core");
    const auto eraseChip = planOffsets(plan, 4u);
    for (const auto offset : eraseChip)
        requirePatternAt(rom, offset, exactPattern("70B590B0154D2988"), "FLASH512 EraseFlashChip");
    const auto eraseSector = planOffsets(plan, 5u);
    for (const auto offset : eraseSector)
        requirePatternAt(rom, offset, exactPattern("70B5464640B490B0"), "FLASH512 EraseFlashSector");
    const auto programSector = expectedPatternMatches(rom, exactPattern("F0B590B00F1C0004040C"), copies, "FLASH512 ProgramFlashSector");

    SaveLibraryMatch result;
    result.type = SaveType::Flash512;
    result.libraryName = "Nintendo " + markers.front().marker +
        (copies > 1u ? " (exact-plan multi-library)" : " (exact-plan)");
    result.marker = markers.front().marker;
    result.markerOffset = markers.front().offset;
    result.geometry = {SaveType::Flash512, 64u * 1024u, 64u * 1024u, 4u * 1024u, 1u, 16u};
    for (std::size_t i = 0; i < copies; ++i) {
        result.primitiveHooks.push_back(thumbHook(SavePrimitiveRole::FlashReadId, "ReadFlashId[copy]", readId[i]));
        result.primitiveHooks.push_back(thumbHook(SavePrimitiveRole::FlashReadCore, "ReadFlash_Core[copy]", readCore[i]));
        result.primitiveHooks.push_back(thumbHook(SavePrimitiveRole::FlashVerifyCore, "VerifyFlashSector_Core[copy]", verifyCore[i]));
        result.primitiveHooks.push_back(thumbHook(SavePrimitiveRole::FlashEraseChip, "EraseFlashChip[copy]", eraseChip[i]));
        result.primitiveHooks.push_back(thumbHook(SavePrimitiveRole::FlashEraseSector, "EraseFlashSector[copy]", eraseSector[i]));
        result.primitiveHooks.push_back(thumbHook(SavePrimitiveRole::FlashProgramSector, "ProgramFlashSector[copy]", programSector[i]));
    }
    return result;
}

SaveLibraryMatch scanFlash1M(const RomImage &rom, const GbabrExactPlan &plan)
{
    if (plan.saveType != SaveType::Flash1M)
        throw std::runtime_error("GBABR exact plan is not FLASH1M");
    const auto markers = findSaveFamilyMarkers(rom, SaveLibraryFamily::Flash1M);
    const std::size_t copies = planKindCount(plan, 3u);
    if (copies == 0u || markers.size() != copies)
        throw std::runtime_error("GBABR FLASH1M library copy count disagrees with Nintendo markers");
    if (planKindCount(plan, 4u) != copies || planKindCount(plan, 5u) != copies || planKindCount(plan, 6u) != copies)
        throw std::runtime_error("GBABR FLASH1M plan lacks one erase/program primitive per library copy");

    const auto switchBank = expectedPatternMatches(rom, exactPattern("0006000E054BAA21"), copies, "FLASH1M SwitchFlashBank");
    const auto readId = expectedPatternMatches(rom, exactPattern("30B591B0684600F0"), copies, "FLASH1M ReadFlashId");
    const auto readCore = expectedPatternMatches(rom, exactPattern("10B5041C531E002A"), copies, "FLASH1M ReadFlash_Core");
    const auto verifyCore = expectedPatternMatches(rom, exactPattern("30B5051C0B1C541E"), copies, "FLASH1M VerifyFlashSector_Core");
    const auto eraseChip = expectedPatternMatches(rom, exactPattern("70B590B0154D2988"), copies, "FLASH1M EraseFlashChip");
    const auto eraseSector = expectedPatternMatches(rom, exactPattern("F0B590B00004060C"), copies, "FLASH1M EraseFlashSector");
    const auto programSector = expectedPatternMatches(rom, exactPattern("F0B590B00F1C0004"), copies, "FLASH1M ProgramFlashSector");

    SaveLibraryMatch result;
    result.type = SaveType::Flash1M;
    result.libraryName = "Nintendo " + markers.front().marker +
        (copies > 1u ? " (exact-plan multi-library)" : " (exact-plan)");
    result.marker = markers.front().marker;
    result.markerOffset = markers.front().offset;
    result.geometry = {SaveType::Flash1M, 128u * 1024u, 64u * 1024u, 4u * 1024u, 2u, 16u};
    for (std::size_t i = 0; i < copies; ++i) {
        result.primitiveHooks.push_back(thumbHook(SavePrimitiveRole::FlashSwitchBank, "SwitchFlashBank[copy]", switchBank[i]));
        result.primitiveHooks.push_back(thumbHook(SavePrimitiveRole::FlashReadId, "ReadFlashId[copy]", readId[i]));
        result.primitiveHooks.push_back(thumbHook(SavePrimitiveRole::FlashReadCore, "ReadFlash_Core[copy]", readCore[i]));
        result.primitiveHooks.push_back(thumbHook(SavePrimitiveRole::FlashVerifyCore, "VerifyFlashSector_Core[copy]", verifyCore[i]));
        result.primitiveHooks.push_back(thumbHook(SavePrimitiveRole::FlashEraseChip, "EraseFlashChip[copy]", eraseChip[i]));
        result.primitiveHooks.push_back(thumbHook(SavePrimitiveRole::FlashEraseSector, "EraseFlashSector[copy]", eraseSector[i]));
        result.primitiveHooks.push_back(thumbHook(SavePrimitiveRole::FlashProgramSector, "ProgramFlashSector[copy]", programSector[i]));
    }
    return result;
}

SaveLibraryMatch scanFastSram(const RomImage &rom, const GbabrExactPlan &plan)
{
    if (plan.saveType != SaveType::Sram)
        throw std::runtime_error("GBABR exact plan is not SRAM");
    const auto markers = findSaveFamilyMarkers(rom, SaveLibraryFamily::Sram);
    if (markers.empty())
        throw std::runtime_error("GBABR SRAM plan has no Nintendo SRAM marker");
    const bool allFast = std::all_of(markers.begin(), markers.end(), [](const auto &m) {
        return m.marker.rfind("SRAM_F_V", 0u) == 0u;
    });
    if (!allFast)
        throw std::runtime_error("unsupported exact-plan SRAM layout without standard wrappers");

    SaveLibraryMatch result;
    result.type = SaveType::Sram;
    result.libraryName = "Nintendo " + markers.front().marker +
        (markers.size() > 1u ? " (exact-plan fast multi-library)" : " (exact-plan fast)");
    result.marker = markers.front().marker;
    result.markerOffset = markers.front().offset;
    result.geometry = {SaveType::Sram, 32u * 1024u, 32u * 1024u, 32u * 1024u, 1u, 1u};
    result.requiresDirectSramProtocolEngine = true;
    return result;
}

} // namespace

SaveLibraryMatch scanSaveLibraryFromExactPlan(const RomImage &rom, SaveType requested, const GbabrExactPlan &plan)
{
    switch (requested) {
    case SaveType::Flash1M: return scanFlash1M(rom, plan);
    case SaveType::Flash512: return scanFlash512(rom, plan);
    case SaveType::Eeprom512:
    case SaveType::Eeprom8K: return scanEeprom(rom, requested, plan);
    case SaveType::Sram: return scanFastSram(rom, plan);
    default: throw std::runtime_error("unsupported exact-plan save type");
    }
}

} // namespace gbasave
