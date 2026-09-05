#include "gbasave/storage_layout.h"

#include <algorithm>
#include <set>
#include <stdexcept>
#include <utility>

namespace gbasave {
namespace {

std::size_t alignUp(std::size_t value, std::size_t alignment)
{
    if (alignment == 0u || (alignment & (alignment - 1u)) != 0u)
        throw std::runtime_error("storage alignment must be a power of two");
    return (value + alignment - 1u) & ~(alignment - 1u);
}

bool overlaps(const ByteRange &left, const ByteRange &right)
{
    return left.offset < right.end() && right.offset < left.end();
}

bool overlapsAny(const ByteRange &candidate, const std::vector<ByteRange> &reservedRanges)
{
    return std::any_of(reservedRanges.begin(), reservedRanges.end(), [&](const auto &reserved) {
        return overlaps(candidate, reserved);
    });
}

RuntimePlacement runtimeFromHoles(
    const std::vector<ByteRange> &ranges,
    HoleDiscoverySource holeSource,
    std::size_t runtimeBytes,
    std::size_t alignment,
    std::size_t executionBankBytes)
{
    struct Candidate {
        std::size_t offset{};
        std::size_t availableBytes{};
    };

    std::vector<Candidate> candidates;
    for (const auto &range : ranges) {
        if (executionBankBytes == 0u) {
            const std::size_t start = alignUp(range.offset, alignment);
            if (start <= range.end() && runtimeBytes <= range.end() - start)
                candidates.push_back({start, range.end() - start});
            continue;
        }

        // Some NORs provide bank-local RWW. The complete executable runtime
        // must fit inside one such bank so its worker can mutate another bank.
        std::size_t bankStart = range.offset & ~(executionBankBytes - 1u);
        while (bankStart < range.end()) {
            const std::size_t segmentStart = std::max(range.offset, bankStart);
            const std::size_t segmentEnd = std::min(range.end(), bankStart + executionBankBytes);
            const std::size_t start = alignUp(segmentStart, alignment);
            if (start <= segmentEnd && runtimeBytes <= segmentEnd - start)
                candidates.push_back({start, segmentEnd - start});
            bankStart += executionBankBytes;
        }
    }

    if (candidates.empty())
        return {};

    // Preserve the established placement policy: choose the highest-address
    // cave, then the smallest fitting run at that address.
    std::sort(candidates.begin(), candidates.end(), [](const auto &left, const auto &right) {
        if (left.offset != right.offset)
            return left.offset > right.offset;
        return left.availableBytes < right.availableBytes;
    });

    return {
        candidates.front().offset,
        runtimeBytes,
        RuntimePlacementSource::InternalSafeHole,
        holeSource,
    };
}

std::vector<StorageBlockPlacement> blocksFromHoles(
    const std::vector<ByteRange> &ranges,
    HoleDiscoverySource holeSource,
    std::size_t physicalEraseBlockBytes,
    const std::vector<ByteRange> &reservedRanges)
{
    std::vector<StorageBlockPlacement> blocks;
    std::set<std::size_t> seenOffsets;

    for (const auto &range : ranges) {
        std::size_t offset = alignUp(range.offset, physicalEraseBlockBytes);
        while (offset <= range.end() && physicalEraseBlockBytes <= range.end() - offset) {
            const ByteRange candidate{offset, physicalEraseBlockBytes};
            if (!overlapsAny(candidate, reservedRanges) && seenOffsets.insert(offset).second) {
                blocks.push_back({
                    0u,
                    offset,
                    physicalEraseBlockBytes,
                    StoragePlacementSource::InternalFfHole,
                    holeSource,
                });
            }
            offset += physicalEraseBlockBytes;
        }
    }

    std::sort(blocks.begin(), blocks.end(), [](const auto &left, const auto &right) {
        return left.offset < right.offset;
    });
    return blocks;
}


std::vector<ByteRange> subtractReservedRanges(
    const std::vector<ByteRange> &ranges,
    const std::vector<ByteRange> &reservedRanges)
{
    std::vector<ByteRange> out;
    for (const auto &range : ranges) {
        std::vector<ByteRange> pieces{range};
        for (const auto &reserved : reservedRanges) {
            std::vector<ByteRange> next;
            for (const auto &piece : pieces) {
                if (!overlaps(piece, reserved)) {
                    next.push_back(piece);
                    continue;
                }
                if (reserved.offset > piece.offset)
                    next.push_back({piece.offset, reserved.offset - piece.offset});
                if (reserved.end() < piece.end())
                    next.push_back({reserved.end(), piece.end() - reserved.end()});
            }
            pieces.swap(next);
        }
        out.insert(out.end(), pieces.begin(), pieces.end());
    }
    std::sort(out.begin(), out.end(), [](const auto &a, const auto &b) { return a.offset < b.offset; });
    std::vector<ByteRange> merged;
    for (const auto &range : out) {
        if (range.size == 0u)
            continue;
        if (!merged.empty() && range.offset <= merged.back().end()) {
            merged.back().size = std::max(merged.back().end(), range.end()) - merged.back().offset;
        } else {
            merged.push_back(range);
        }
    }
    return merged;
}

std::vector<ByteRange> programOnlyFfRanges(const RomHoleCatalog &holes)
{
    std::vector<ByteRange> ranges;
    if (!holes.gbabrDatabaseMatched)
        return ranges;
    for (const auto &region : holes.databaseRegions) {
        if (region.programOnlyFfSafe())
            ranges.push_back(region.range);
    }
    return ranges;
}

} // namespace

std::uint32_t StorageBlockPlacement::gbaAddress() const
{
    return kGbaRomBaseAddress + static_cast<std::uint32_t>(offset);
}

std::uint32_t StorageSpanPlacement::gbaAddress() const
{
    return kGbaRomBaseAddress + static_cast<std::uint32_t>(offset);
}

std::size_t StorageLayout::persistentByteCount() const
{
    std::size_t total = 0u;
    for (const auto &block : blocks)
        total += block.blockBytes;
    for (const auto &span : programOnlySpans)
        total += span.spanBytes;
    return total;
}

const StorageBlockPlacement &StorageLayout::block(std::size_t index) const
{
    const auto it = std::find_if(blocks.begin(), blocks.end(), [index](const auto &entry) {
        return entry.index == index;
    });
    if (it == blocks.end())
        throw std::runtime_error("storage block index has no physical placement");
    return *it;
}

RuntimePlacement createRuntimePlacement(
    const RomImage &rom,
    const RomHoleCatalog &holes,
    std::size_t runtimeBytes,
    std::size_t romAddressSpaceBytes,
    std::size_t alignment,
    std::size_t executionBankBytes)
{
    if (runtimeBytes == 0u)
        throw std::runtime_error("runtime image is empty");
    if (alignment == 0u || (alignment & (alignment - 1u)) != 0u)
        throw std::runtime_error("runtime alignment must be a power of two");
    if (executionBankBytes != 0u && (executionBankBytes & (executionBankBytes - 1u)) != 0u)
        throw std::runtime_error("runtime execution bank size must be zero or a power of two");

    // Shared GBABR metadata is the preferred hole oracle for an exact ROM fingerprint.
    // Every database range is still byte-verified against this ROM.
    if (holes.gbabrDatabaseMatched) {
        const auto placement = runtimeFromHoles(
            holes.databaseRuntimeHoles, HoleDiscoverySource::GbabrDatabase, runtimeBytes, alignment, executionBankBytes);
        if (placement.size != 0u)
            return placement;
    }

    // No independent byte-pattern fallback exists here. On a database miss,
    // append the runtime instead of treating a run of FF/00 as structural proof.

    std::size_t tail = alignUp(rom.size(), alignment);
    if (executionBankBytes != 0u &&
        (tail / executionBankBytes) != ((tail + runtimeBytes - 1u) / executionBankBytes))
        tail = alignUp(tail, executionBankBytes);
    if (tail > romAddressSpaceBytes || runtimeBytes > romAddressSpaceBytes - tail)
        throw std::runtime_error("no verified runtime cave and appended runtime exceeds the 32 MiB GBA ROM address space");
    return {tail, runtimeBytes, RuntimePlacementSource::AppendedTail, HoleDiscoverySource::None};
}

std::optional<StorageLayout> tryCreateInternalEraseStorageLayout(
    const RomImage &originalRom,
    const RomHoleCatalog &holes,
    std::size_t minimumTailOffset,
    std::size_t romAddressSpaceBytes,
    std::size_t physicalEraseBlockBytes,
    std::size_t requiredBlockCount,
    StorageLayoutKind kind,
    const std::vector<ByteRange> &reservedRanges)
{
    if (!holes.gbabrDatabaseMatched || requiredBlockCount == 0u)
        return std::nullopt;
    auto blocks = blocksFromHoles(
        holes.databaseErasedHoles, HoleDiscoverySource::GbabrDatabase,
        physicalEraseBlockBytes, reservedRanges);
    if (blocks.size() < requiredBlockCount)
        return std::nullopt;
    blocks.resize(requiredBlockCount);
    for (std::size_t index = 0u; index < blocks.size(); ++index)
        blocks[index].index = index;

    StorageLayout layout;
    layout.kind = kind;
    layout.romAddressSpaceBytes = romAddressSpaceBytes;
    layout.physicalEraseBlockBytes = physicalEraseBlockBytes;
    layout.blocks = std::move(blocks);
    layout.outputSizeBytes = std::max(originalRom.size(), minimumTailOffset);
    return layout;
}

std::optional<StorageLayout> tryCreateProgramOnlyJournalLayout(
    const RomImage &originalRom,
    const RomHoleCatalog &holes,
    std::size_t minimumTailOffset,
    std::size_t romAddressSpaceBytes,
    std::size_t recordBytes,
    std::size_t minimumRecordCount,
    std::size_t maximumSpanCount,
    const std::vector<ByteRange> &reservedRanges)
{
    if (!holes.gbabrDatabaseMatched || recordBytes == 0u || minimumRecordCount < 2u || maximumSpanCount == 0u)
        return std::nullopt;
    if (recordBytes > static_cast<std::size_t>(-1) / minimumRecordCount)
        return std::nullopt;
    const std::size_t needed = recordBytes * minimumRecordCount;
    auto ranges = subtractReservedRanges(programOnlyFfRanges(holes), reservedRanges);

    // Word-align the logical arena so every runtime program operation can be
    // split safely into halfword-compatible physical fragments.
    std::vector<StorageSpanPlacement> spans;
    std::size_t available = 0u;
    for (const auto &range : ranges) {
        const std::size_t start = alignUp(range.offset, 4u);
        const std::size_t end = range.end() & ~std::size_t{3u};
        if (end <= start)
            continue;
        spans.push_back({spans.size(), start, end - start,
            StoragePlacementSource::InternalFfProgramSpan, HoleDiscoverySource::GbabrDatabase});
        available += end - start;
        if (available >= needed)
            break;
        if (spans.size() >= maximumSpanCount)
            break;
    }
    if (available < needed || spans.empty() || spans.size() > maximumSpanCount)
        return std::nullopt;

    // Trim the final span to exactly the complete-record capacity we expose.
    const std::size_t recordCount = available / recordBytes;
    const std::size_t exposedBytes = recordCount * recordBytes;
    std::size_t keep = exposedBytes;
    std::vector<StorageSpanPlacement> trimmed;
    for (auto span : spans) {
        if (keep == 0u)
            break;
        span.spanBytes = std::min(span.spanBytes, keep);
        span.index = trimmed.size();
        trimmed.push_back(span);
        keep -= span.spanBytes;
    }

    StorageLayout layout;
    layout.kind = StorageLayoutKind::SramProgramOnlyJournal;
    layout.romAddressSpaceBytes = romAddressSpaceBytes;
    layout.physicalEraseBlockBytes = 0u; // journal is explicitly erase-free
    layout.outputSizeBytes = std::max(originalRom.size(), minimumTailOffset);
    layout.programOnlySpans = std::move(trimmed);
    layout.journalRecordBytes = recordBytes;
    layout.journalRecordCount = exposedBytes / recordBytes;
    return layout;
}

std::optional<StorageLayout> tryCreateAppendedProgramOnlyJournalLayout(
    const RomImage &originalRom,
    std::size_t minimumTailOffset,
    std::size_t romAddressSpaceBytes,
    std::size_t recordBytes,
    std::size_t minimumRecordCount,
    std::size_t preferredRecordCount,
    const std::vector<ByteRange> &reservedRanges)
{
    if (recordBytes == 0u || minimumRecordCount < 2u || preferredRecordCount < minimumRecordCount)
        return std::nullopt;
    if (recordBytes > static_cast<std::size_t>(-1) / preferredRecordCount)
        return std::nullopt;
    const std::size_t minimumBytes = recordBytes * minimumRecordCount;
    const std::size_t preferredBytes = recordBytes * preferredRecordCount;
    const std::size_t first = alignUp(std::max(originalRom.size(), minimumTailOffset), 4u);
    if (first >= romAddressSpaceBytes || minimumBytes > romAddressSpaceBytes - first)
        return std::nullopt;

    std::vector<ByteRange> reservations = reservedRanges;
    std::sort(reservations.begin(), reservations.end(), [](const auto &a, const auto &b) {
        return a.offset < b.offset;
    });

    struct Gap { std::size_t start{}, bytes{}; };
    std::optional<Gap> fallback;
    std::size_t cursor = first;
    for (const auto &reserved : reservations) {
        if (reserved.end() <= cursor)
            continue;
        const std::size_t gapEnd = std::min(reserved.offset, romAddressSpaceBytes);
        if (gapEnd > cursor) {
            const std::size_t bytes = gapEnd - cursor;
            if (bytes >= preferredBytes) {
                fallback = Gap{cursor, preferredBytes};
                break;
            }
            const std::size_t complete = (bytes / recordBytes) * recordBytes;
            if (complete >= minimumBytes && (!fallback || complete > fallback->bytes))
                fallback = Gap{cursor, complete};
        }
        if (reserved.end() > cursor)
            cursor = alignUp(reserved.end(), 4u);
        if (cursor >= romAddressSpaceBytes)
            break;
    }
    if ((!fallback || fallback->bytes < preferredBytes) && cursor < romAddressSpaceBytes) {
        const std::size_t bytes = romAddressSpaceBytes - cursor;
        if (bytes >= preferredBytes)
            fallback = Gap{cursor, preferredBytes};
        else {
            const std::size_t complete = (bytes / recordBytes) * recordBytes;
            if (complete >= minimumBytes && (!fallback || complete > fallback->bytes))
                fallback = Gap{cursor, complete};
        }
    }
    if (!fallback)
        return std::nullopt;

    StorageLayout layout;
    layout.kind = StorageLayoutKind::SramProgramOnlyJournal;
    layout.romAddressSpaceBytes = romAddressSpaceBytes;
    layout.physicalEraseBlockBytes = 0u;
    layout.outputSizeBytes = std::max(std::max(originalRom.size(), minimumTailOffset), fallback->start + fallback->bytes);
    layout.programOnlySpans.push_back({0u, fallback->start, fallback->bytes,
        StoragePlacementSource::AppendedTail, HoleDiscoverySource::None});
    layout.journalRecordBytes = recordBytes;
    layout.journalRecordCount = fallback->bytes / recordBytes;
    return layout;
}

StorageLayout createStorageLayout(
    const RomImage &originalRom,
    const RomHoleCatalog &holes,
    std::size_t minimumTailOffset,
    std::size_t romAddressSpaceBytes,
    std::size_t physicalEraseBlockBytes,
    std::size_t requiredBlockCount,
    StorageLayoutKind kind,
    bool allowInternalAnalyzerStorage,
    const std::vector<ByteRange> &reservedRanges,
    bool preferInternalAnalyzerStorage)
{
    if (requiredBlockCount == 0u)
        throw std::runtime_error("storage layout requires at least one physical block");
    if (physicalEraseBlockBytes == 0u || (physicalEraseBlockBytes & (physicalEraseBlockBytes - 1u)) != 0u)
        throw std::runtime_error("physical erase block must be a power of two");

    const auto allocateTail = [&](std::size_t firstIndex, std::vector<StorageBlockPlacement> existing) {
        std::size_t nextIndex = firstIndex;
        std::size_t tailOffset = alignUp(std::max(originalRom.size(), minimumTailOffset), physicalEraseBlockBytes);
        while (nextIndex < requiredBlockCount) {
            if (tailOffset > romAddressSpaceBytes || physicalEraseBlockBytes > romAddressSpaceBytes - tailOffset)
                break;
            const ByteRange candidate{tailOffset, physicalEraseBlockBytes};
            if (!overlapsAny(candidate, reservedRanges)) {
                existing.push_back({
                    nextIndex++,
                    tailOffset,
                    physicalEraseBlockBytes,
                    StoragePlacementSource::AppendedTail,
                    HoleDiscoverySource::None,
                });
            }
            tailOffset += physicalEraseBlockBytes;
        }
        return existing;
    };

    StorageLayout layout;
    layout.kind = kind;
    layout.romAddressSpaceBytes = romAddressSpaceBytes;
    layout.physicalEraseBlockBytes = physicalEraseBlockBytes;

    const auto completeInternalAllocation = [&]() {
        std::vector<StorageBlockPlacement> internalBlocks;
        if (holes.gbabrDatabaseMatched) {
            internalBlocks = blocksFromHoles(
                holes.databaseErasedHoles,
                HoleDiscoverySource::GbabrDatabase,
                physicalEraseBlockBytes,
                reservedRanges);
        }
        if (internalBlocks.size() < requiredBlockCount)
            return std::vector<StorageBlockPlacement>{};
        internalBlocks.resize(requiredBlockCount);
        for (std::size_t index = 0u; index < internalBlocks.size(); ++index)
            internalBlocks[index].index = index;
        return internalBlocks;
    };

    // When the runtime itself fits internally, prefer a complete internal
    // storage allocation if one exists. This keeps full ROM images compact and
    // preserves established internal layouts. If the runtime had to append,
    // keep the historical tail-first policy so legacy small-ROM layouts remain
    // stable.
    if (allowInternalAnalyzerStorage && preferInternalAnalyzerStorage)
        layout.blocks = completeInternalAllocation();

    if (layout.blocks.empty())
        layout.blocks = allocateTail(0u, {});

    if (layout.blocks.size() != requiredBlockCount && allowInternalAnalyzerStorage) {
        layout.blocks = completeInternalAllocation();
        if (layout.blocks.empty())
            layout.blocks = allocateTail(0u, {});
    }

    if (layout.blocks.size() != requiredBlockCount)
        throw std::runtime_error("save storage cannot be placed: not enough verified erased holes or GBA ROM address space");

    layout.outputSizeBytes = std::max(originalRom.size(), minimumTailOffset);
    for (const auto &entry : layout.blocks)
        layout.outputSizeBytes = std::max(layout.outputSizeBytes, entry.offset + entry.blockBytes);

    if (layout.outputSizeBytes > romAddressSpaceBytes)
        throw std::runtime_error("generated ROM exceeds the GBA ROM address space");
    return layout;
}

std::string toString(RuntimePlacementSource source)
{
    switch (source) {
    case RuntimePlacementSource::InternalSafeHole: return "INTERNAL_SAFE_HOLE";
    case RuntimePlacementSource::AppendedTail: return "APPENDED_TAIL";
    }
    return "UNKNOWN";
}

std::string toString(StoragePlacementSource source)
{
    switch (source) {
    case StoragePlacementSource::InternalFfHole: return "INTERNAL_FF_HOLE";
    case StoragePlacementSource::InternalFfProgramSpan: return "INTERNAL_FF_PROGRAM_SPAN";
    case StoragePlacementSource::AppendedTail: return "APPENDED_TAIL";
    }
    return "UNKNOWN";
}

std::string toString(StorageLayoutKind kind)
{
    switch (kind) {
    case StorageLayoutKind::FlashVersionedSlots: return "FLASH_VERSIONED_SLOTS";
    case StorageLayoutKind::EepromRecordLanes: return "EEPROM_RECORD_LANES";
    case StorageLayoutKind::EepromCompactAB: return "EEPROM_COMPACT_AB";
    case StorageLayoutKind::FixedSramMirror: return "FIXED_SRAM_MIRROR";
    case StorageLayoutKind::SramProgramOnlyJournal: return "SRAM_PROGRAM_ONLY_JOURNAL";
    }
    return "UNKNOWN";
}

} // namespace gbasave
