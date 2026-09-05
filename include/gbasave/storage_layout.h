#pragma once

#include "gbasave/hole_finder.h"
#include "gbasave/rom_image.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace gbasave {

inline constexpr std::uint32_t kGbaRomBaseAddress = 0x08000000u;

enum class RuntimePlacementSource {
    InternalSafeHole,
    AppendedTail,
};

struct RuntimePlacement {
    std::size_t offset{};
    std::size_t size{};
    RuntimePlacementSource source{RuntimePlacementSource::AppendedTail};
    HoleDiscoverySource holeSource{HoleDiscoverySource::None};

    std::size_t end() const { return offset + size; }
    std::uint32_t gbaAddress() const { return kGbaRomBaseAddress + static_cast<std::uint32_t>(offset); }
};

enum class StoragePlacementSource {
    InternalFfHole,
    InternalFfProgramSpan,
    AppendedTail,
};

enum class StorageLayoutKind {
    FlashVersionedSlots,
    EepromRecordLanes,
    EepromCompactAB,
    FixedSramMirror,
    SramProgramOnlyJournal,
};

struct StorageBlockPlacement {
    std::size_t index{};
    std::size_t offset{};
    std::size_t blockBytes{};
    StoragePlacementSource source{StoragePlacementSource::AppendedTail};
    HoleDiscoverySource holeSource{HoleDiscoverySource::None};

    std::uint32_t gbaAddress() const;
};


struct StorageSpanPlacement {
    std::size_t index{};
    std::size_t offset{};
    std::size_t spanBytes{};
    StoragePlacementSource source{StoragePlacementSource::InternalFfProgramSpan};
    HoleDiscoverySource holeSource{HoleDiscoverySource::GbabrDatabase};

    std::size_t end() const { return offset + spanBytes; }
    std::uint32_t gbaAddress() const;
};

struct StorageLayout {
    StorageLayoutKind kind{StorageLayoutKind::FlashVersionedSlots};
    std::size_t romAddressSpaceBytes{};
    std::size_t physicalEraseBlockBytes{};
    std::size_t outputSizeBytes{};
    std::vector<StorageBlockPlacement> blocks;
    std::vector<StorageSpanPlacement> programOnlySpans;
    std::size_t journalRecordBytes{};
    std::size_t journalRecordCount{};

    const StorageBlockPlacement &block(std::size_t index) const;
    std::size_t persistentByteCount() const;
};

RuntimePlacement createRuntimePlacement(
    const RomImage &rom,
    const RomHoleCatalog &holes,
    std::size_t runtimeBytes,
    std::size_t romAddressSpaceBytes,
    std::size_t alignment = 4u,
    std::size_t executionBankBytes = 0u);

std::optional<StorageLayout> tryCreateInternalEraseStorageLayout(
    const RomImage &originalRom,
    const RomHoleCatalog &holes,
    std::size_t minimumTailOffset,
    std::size_t romAddressSpaceBytes,
    std::size_t physicalEraseBlockBytes,
    std::size_t requiredBlockCount,
    StorageLayoutKind kind,
    const std::vector<ByteRange> &reservedRanges = {});

std::optional<StorageLayout> tryCreateProgramOnlyJournalLayout(
    const RomImage &originalRom,
    const RomHoleCatalog &holes,
    std::size_t minimumTailOffset,
    std::size_t romAddressSpaceBytes,
    std::size_t recordBytes,
    std::size_t minimumRecordCount,
    std::size_t maximumSpanCount,
    const std::vector<ByteRange> &reservedRanges = {});

std::optional<StorageLayout> tryCreateAppendedProgramOnlyJournalLayout(
    const RomImage &originalRom,
    std::size_t minimumTailOffset,
    std::size_t romAddressSpaceBytes,
    std::size_t recordBytes,
    std::size_t minimumRecordCount,
    std::size_t preferredRecordCount,
    const std::vector<ByteRange> &reservedRanges = {});

StorageLayout createStorageLayout(
    const RomImage &originalRom,
    const RomHoleCatalog &holes,
    std::size_t minimumTailOffset,
    std::size_t romAddressSpaceBytes,
    std::size_t physicalEraseBlockBytes,
    std::size_t requiredBlockCount,
    StorageLayoutKind kind,
    bool allowInternalAnalyzerStorage = true,
    const std::vector<ByteRange> &reservedRanges = {},
    bool preferInternalAnalyzerStorage = false);

std::string toString(RuntimePlacementSource source);
std::string toString(StoragePlacementSource source);
std::string toString(StorageLayoutKind kind);

} // namespace gbasave
