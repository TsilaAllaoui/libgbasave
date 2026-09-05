#include "gbasave/patch_report.h"

#include "gbasave/nor_backends.h"
#include "gbasave/rom_image.h"

#include <algorithm>
#include <sstream>

namespace gbasave {
namespace {

std::size_t appendedStorageBytes(const StorageLayout &layout)
{
    std::size_t total = 0u;
    for (const auto &block : layout.blocks)
        if (block.source == StoragePlacementSource::AppendedTail)
            total += block.blockBytes;
    for (const auto &span : layout.programOnlySpans)
        if (span.source == StoragePlacementSource::AppendedTail)
            total += span.spanBytes;
    return total;
}

std::size_t internalStorageBytes(const StorageLayout &layout)
{
    std::size_t total = 0u;
    for (const auto &block : layout.blocks)
        if (block.source != StoragePlacementSource::AppendedTail)
            total += block.blockBytes;
    for (const auto &span : layout.programOnlySpans)
        if (span.source != StoragePlacementSource::AppendedTail)
            total += span.spanBytes;
    return total;
}

} // namespace

std::string formatPatchReport(
    const std::string &version,
    const std::string &inputHash,
    const std::string &outputHash,
    std::size_t originalRomBytes,
    const SaveLibraryMatch &saveMatch,
    const PatchOptions &options,
    const PatchReport &patchReport)
{
    const auto &nor = norBackendDescriptor(options.norFlashType);
    std::ostringstream out;
    out << "GBASaveHandler " << version << " patch complete\n";
    out << "Input SHA256:  " << inputHash << "\n";
    out << "Output SHA256: " << outputHash << "\n";
    out << "Save library:  " << saveMatch.libraryName << "\n";
    out << "Save type:     " << toString(saveMatch.type) << " / " << saveMatch.geometry.totalBytes << " bytes\n";

    out << "\n============================================================\n";
    const std::string targetDisplay = options.targetProfileDisplayName.empty() ? nor.displayName : options.targetProfileDisplayName;
    const std::string targetKey = options.targetProfileKey.empty() ? nor.profileKey : options.targetProfileKey;
    out << "TARGET CART/NOR PROFILE: " << targetDisplay << " (" << targetKey << ")\n";
    out << "IMPORTANT: backend-specific patched ROMs are NOT interchangeable.\n";
    out << "============================================================\n";
    out << "NOR backend:     " << nor.protocolDescription << "\n";
    out << "NOR program cmd: " << hex(nor.wordProgramCommand, 2) << "\n";
    if (nor.supportsBlockErase)
        out << "NOR erase unit:  " << nor.eraseBlockBytes << " bytes (erase-capable paths only)\n";
    else
        out << "NOR erase unit:  NONE (program-only runtime; chip erase is forbidden)\n";
    out << "Hole database: "
        << (patchReport.gbabrDatabaseMatched
                ? "GBABR_v" + std::to_string(patchReport.gbabrDatabaseVersion) + " MATCH (" +
                    std::to_string(patchReport.gbabrErasedHoleCount) + " verified erased regions)"
                : "GBABR_v" + std::to_string(patchReport.gbabrDatabaseVersion) + " NO_MATCH -> APPEND_ONLY")
        << "\n";
    if (patchReport.forcedFlashId != 0u)
        out << "Forced ID:     " << hex(patchReport.forcedFlashId, 4) << "\n";

    out << "Runtime:       " << hex(patchReport.runtimeOffset, 6)
        << ".." << hex(patchReport.runtimeOffset + patchReport.runtimeSize - 1u, 6)
        << " " << toString(patchReport.runtimePlacementSource);
    if (patchReport.runtimePlacementSource == RuntimePlacementSource::InternalSafeHole)
        out << " via " << toString(patchReport.runtimeHoleSource);
    out << "\n";
    if (nor.runtimeExecutionBankBytes != 0u)
        out << "M36 RWW runtime bank:  " << (patchReport.runtimeOffset / nor.runtimeExecutionBankBytes)
            << " (" << nor.runtimeExecutionBankBytes << "-byte bank reserved from mutable storage)\n";

    out << "Storage policy:" << (options.storagePolicy == StoragePolicy::CompactV2 ? " COMPACT_V2" : " SAFE_V1");
    if (patchReport.storagePolicyFallbackProgramOnly)
        out << " -> PROGRAM_ONLY_FALLBACK (backend has no sector erase)";
    out << "\n";
    out << "Storage mode:  " << toString(patchReport.storage.kind) << "\n";
    if (patchReport.flashVersionsPerSector != 0u) {
        out << "FLASH versions/block:  " << patchReport.flashVersionsPerSector << "\n";
        out << "FLASH spill blocks:    " << patchReport.flashSpillBlockCount << "\n";
    }
    if (patchReport.eepromRecordsPerDword != 0u)
        out << "EEPROM records/dword:  " << patchReport.eepromRecordsPerDword << "\n";

    if (patchReport.saveType == SaveType::Sram) {
        out << "SRAM live state:       RAM/EWRAM only during gameplay\n";
        out << "SRAM shadow:           " << hex(patchReport.sramShadowAddress, 8)
            << (patchReport.sramUsesGameOwnedShadowSnapshot ? " GAME_OWNED_PROVEN" : " PRIVATE_COMPATIBILITY") << "\n";
        if (patchReport.sramRuntimeStateAddress != 0u)
            out << "SRAM runtime state:    " << hex(patchReport.sramRuntimeStateAddress, 8)
                << " GUARDED_EWRAM_CHAIN_STATE\n";
        out << "SRAM NOR retention:    restore/reload + changed hotkey commit only\n";
        if (patchReport.sramUsesProgramOnlyJournal) {
            out << "SRAM snapshot format:  PROGRAM_ONLY_JOURNAL commit-last; no sector erase\n";
            out << "SRAM journal records:  " << patchReport.sramJournalRecordCount
                << " across " << patchReport.sramJournalSpanCount << " safe FF spans\n";
        } else if (patchReport.sramUsesGameOwnedShadowSnapshot) {
            out << "SRAM snapshot format:  A/B commit-last 32KiB; 4KiB program chunks; "
                << (nor.supportsDirectProtocolEngine ? "DIRECT_RWW worker" : "stack NOR worker") << "\n";
        }
        out << "SRAM direct literals:  " << patchReport.rewrittenSramLiterals.size() << "\n";
        if (patchReport.sramRefreshTriggerOffset != 0xFFFFFFFFu)
            out << "SRAM preload trigger:  " << hex(patchReport.sramRefreshTriggerOffset, 4) << " STRUCTURAL_PROOF\n";
    }

    const std::size_t outputBytes = patchReport.storage.outputSizeBytes;
    const std::size_t outputGrowth = outputBytes > originalRomBytes ? outputBytes - originalRomBytes : 0u;
    const std::size_t physicalStorage = patchReport.storage.persistentByteCount();
    const std::size_t appendedStorage = appendedStorageBytes(patchReport.storage);
    const std::size_t internalStorage = internalStorageBytes(patchReport.storage);
    const std::size_t appendedRuntime = patchReport.runtimePlacementSource == RuntimePlacementSource::AppendedTail
        ? patchReport.runtimeSize : 0u;
    const std::size_t accountedPayload = appendedRuntime + appendedStorage;
    const std::size_t alignmentPadding = outputGrowth > accountedPayload ? outputGrowth - accountedPayload : 0u;

    out << "\nSize/layout summary:\n";
    out << "  Original ROM:        " << originalRomBytes << " bytes (" << hex(originalRomBytes, 6) << ")\n";
    out << "  Runtime code:        " << patchReport.runtimeSize << " bytes; "
        << (appendedRuntime != 0u ? "appended" : "inside verified ROM hole") << "\n";
    out << "  Persistent storage:  " << physicalStorage << " physical bytes"
        << " (appended " << appendedStorage << ", internal " << internalStorage << ")\n";
    out << "  Alignment/reserve:   " << alignmentPadding << " appended bytes\n";
    out << "  Output growth:       " << outputGrowth << " bytes\n";
    out << "  Output size:         " << outputBytes << " bytes (" << hex(outputBytes, 6)
        << "; profile ceiling " << hex(patchReport.storage.romAddressSpaceBytes, 6) << ")\n";
    if (outputGrowth != 0u)
        out << "  Why it grows:        NOR-safe version/record storage is part of the generated ROM image; "
            << "it is appended when GBABR has no verified internal storage holes.\n";

    for (const auto &block : patchReport.storage.blocks) {
        out << "  block " << block.index << " -> " << hex(block.offset, 6)
            << ".." << hex(block.offset + block.blockBytes - 1u, 6) << " " << toString(block.source);
        if (block.source == StoragePlacementSource::InternalFfHole)
            out << " via " << toString(block.holeSource);
        out << "\n";
    }
    for (const auto &span : patchReport.storage.programOnlySpans) {
        out << "  journal span " << span.index << " -> " << hex(span.offset, 6)
            << ".." << hex(span.offset + span.spanBytes - 1u, 6) << " " << toString(span.source);
        if (span.source != StoragePlacementSource::AppendedTail)
            out << " via " << toString(span.holeSource);
        out << "\n";
    }

    out << "Patched routines: " << patchReport.routines.size() << "\n";
    if (patchReport.storagePreserveApplied)
        out << "Storage image: EXACT_PRESERVE_" << patchReport.storagePreserveMode
            << " (allocator-selected blocks copied byte-for-byte)\n";
    out << "\nNOR wear policy: ";
    switch (patchReport.saveType) {
    case SaveType::Sram:
        out << "normal gameplay is RAM/EWRAM-only; NOR changes only during restore/explicit changed-save retention commits.";
        break;
    case SaveType::Eeprom512:
        out << "EEPROM writes are append/program-only records; identical dwords consume no new record.";
        break;
    case SaveType::Eeprom8K:
        if (patchReport.storagePolicyFallbackProgramOnly)
            out << "backend has no sector erase, so EEPROM8K uses append/program-only record lanes; no erase command is ever issued.";
        else if (options.storagePolicy == StoragePolicy::CompactV2)
            out << "compact A/B EEPROM records are program-only until a bounded lane fills; GC erases only the inactive generation, publishes the replacement last, and preserves the old committed generation until publication succeeds.";
        else
            out << "SAFE_V1 EEPROM writes are append/program-only records; identical dwords consume no new record.";
        break;
    case SaveType::Flash512:
    case SaveType::Flash1M:
        out << "FLASH uses commit-last append-only versioned NOR slots and spill blocks; current runtime does not recycle them with sector erase.";
        break;
    default:
        out << "backend writes are commit-last and capability/geometry constrained.";
        break;
    }
    out << "\n";
    return out.str();
}

} // namespace gbasave
