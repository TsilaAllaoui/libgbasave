#pragma once

#include "gbasave/save_library.h"
#include "gbasave/save_types.h"
#include "gbasave/nor_backends.h"
#include "gbasave/storage_layout.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace gbasave {

enum class StoragePolicy {
    SafeV1,
    CompactV2,
};

struct PatchOptions {
    // Effective physical capacity for this patch invocation. CLI target profiles
    // set this explicitly; programmatic callers default to the GBA 32 MiB ceiling.
    std::size_t romAddressSpaceBytes{32u * 1024u * 1024u};
    std::string targetProfileKey;
    std::string targetProfileDisplayName;
    // Program-only FLASH/EEPROM lanes never erase at runtime, so their compact
    // 64 KiB packing is independent of the cartridge's physical erase geometry.
    std::size_t programStorageBlockBytes{64u * 1024u};
    std::uint16_t forcedFlashId{}; // 0 = profile default
    std::size_t flashSpillBlockCount{8u}; // shared overflow pool for FLASH versioned slots
    StoragePolicy storagePolicy{StoragePolicy::CompactV2};
    NorFlashType norFlashType{NorFlashType::IntelStatusRegister};
    // Optional exact preservation source. The patcher overlays only the physical
    // storage blocks selected by its own allocation plan. The image may be either
    // (a) the selected blocks packed in block order or (b) a full cartridge/ROM
    // image large enough to address every selected block at its physical offset.
    // No save format, game code, title, or save type is assumed here.
    std::vector<std::uint8_t> storagePreserveImage;
};

struct PatchedRoutine {
    std::string name;
    std::size_t stockOffset{};
    std::uint32_t runtimeAddress{};
};

struct PatchReport {
    SaveType saveType{SaveType::Unknown};
    StorageLayout storage;
    std::size_t runtimeOffset{};
    std::size_t runtimeSize{};
    RuntimePlacementSource runtimePlacementSource{RuntimePlacementSource::AppendedTail};
    HoleDiscoverySource runtimeHoleSource{HoleDiscoverySource::None};
    std::uint32_t gbabrDatabaseVersion{};
    bool gbabrDatabaseMatched{};
    std::size_t gbabrErasedHoleCount{};
    std::uint16_t forcedFlashId{};
    std::uint16_t flashVersionsPerSector{};
    std::uint16_t flashSpillBlockCount{};
    std::uint16_t eepromRecordsPerDword{};
    bool storagePolicyFallbackProgramOnly{};
    bool sramUsesEraseRebuild{};
    bool sramHotkeyOnly{};
    bool sramUsesGameOwnedShadowSnapshot{};
    bool sramUsesProgramOnlyJournal{};
    std::uint16_t sramJournalRecordCount{};
    std::uint16_t sramJournalSpanCount{};
    std::uint32_t sramShadowAddress{};
    std::uint32_t sramRuntimeStateAddress{};
    std::uint32_t sramIrqEntryAddress{};
    std::uint32_t sramRefreshTriggerOffset{0xFFFFFFFFu};
    bool storagePreserveApplied{};
    std::string storagePreserveMode;
    std::vector<PatchedRoutine> routines;
    std::vector<std::size_t> rewrittenSramLiterals;
};

PatchOptions makePatchOptionsForNorProfile(const NorTargetProfileDescriptor &target);

class SavePatcher {
public:
    PatchReport patchToTargetStorage(
        RomImage &rom,
        const SaveLibraryMatch &saveLibrary,
        const PatchOptions &options) const;
};

} // namespace gbasave
