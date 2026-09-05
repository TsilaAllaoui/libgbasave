#include "gbasave/storage_image.h"

#include <algorithm>
#include <cstddef>
#include <stdexcept>

namespace gbasave {
namespace {

enum class PreserveMode {
    None,
    PackedBlocks,
    FullImage,
};

PreserveMode choosePreserveMode(
    const StorageLayout &storage,
    const std::vector<std::uint8_t> &preserveImage)
{
    if (preserveImage.empty())
        return PreserveMode::None;

    std::size_t packedBytes = 0u;
    std::size_t highestBlockEnd = 0u;
    for (const auto &block : storage.blocks) {
        packedBytes += block.blockBytes;
        highestBlockEnd = std::max(highestBlockEnd, block.offset + block.blockBytes);
    }
    for (const auto &span : storage.programOnlySpans) {
        packedBytes += span.spanBytes;
        highestBlockEnd = std::max(highestBlockEnd, span.offset + span.spanBytes);
    }

    const bool isPackedImage = preserveImage.size() == packedBytes;
    const bool isFullImage = preserveImage.size() >= highestBlockEnd;
    if (isPackedImage && isFullImage) {
        throw std::runtime_error(
            "storage preservation image is ambiguous (matches packed size and full-image addressing); "
            "use a larger full cartridge dump or a packed dump whose size differs from the highest storage end");
    }
    if (isPackedImage)
        return PreserveMode::PackedBlocks;
    if (isFullImage)
        return PreserveMode::FullImage;

    throw std::runtime_error(
        "storage preservation image size mismatch: expected exactly " + hex(packedBytes, 0) +
        " packed bytes or at least " + hex(highestBlockEnd, 0) +
        " bytes for a full physical image; got " + hex(preserveImage.size(), 0));
}

void initialiseBlankStorageBlock(RomImage &rom, const StorageLayout &storage, const StorageBlockPlacement &block)
{
    std::fill(
        rom.bytes().begin() + static_cast<std::ptrdiff_t>(block.offset),
        rom.bytes().begin() + static_cast<std::ptrdiff_t>(block.offset + block.blockBytes),
        0xFFu);

    // The ignored 0-bit anchor forces compare-aware flashers to process a
    // freshly allocated block instead of leaving stale data from an older ROM.
    std::size_t anchorOffset = block.offset + block.blockBytes - 4u;
    if (storage.kind == StorageLayoutKind::EepromRecordLanes ||
        storage.kind == StorageLayoutKind::EepromCompactAB)
        anchorOffset = block.offset + block.blockBytes - 8u;
    rom.write16(anchorOffset, 0xAAAAu);
}

} // namespace

StorageImageResult initialiseStorageImage(
    RomImage &rom,
    const StorageLayout &storage,
    const std::vector<std::uint8_t> &preserveImage)
{
    rom.resize(storage.outputSizeBytes, 0x00u);
    const PreserveMode preserveMode = choosePreserveMode(storage, preserveImage);

    StorageImageResult result;
    result.exactPreservationApplied = preserveMode != PreserveMode::None;
    if (preserveMode == PreserveMode::PackedBlocks)
        result.preservationMode = "PACKED_BLOCKS";
    else if (preserveMode == PreserveMode::FullImage)
        result.preservationMode = "FULL_IMAGE";

    std::size_t packedOffset = 0u;
    for (const auto &block : storage.blocks) {
        if (preserveMode == PreserveMode::PackedBlocks) {
            rom.write(block.offset, preserveImage.data() + packedOffset, block.blockBytes);
            packedOffset += block.blockBytes;
        } else if (preserveMode == PreserveMode::FullImage) {
            rom.write(block.offset, preserveImage.data() + block.offset, block.blockBytes);
        } else {
            initialiseBlankStorageBlock(rom, storage, block);
        }
    }
    for (const auto &span : storage.programOnlySpans) {
        if (preserveMode == PreserveMode::PackedBlocks) {
            rom.write(span.offset, preserveImage.data() + packedOffset, span.spanBytes);
            packedOffset += span.spanBytes;
        } else if (preserveMode == PreserveMode::FullImage) {
            rom.write(span.offset, preserveImage.data() + span.offset, span.spanBytes);
        } else {
            // Internal journal spans were byte-verified as FF against the exact
            // source ROM. Appended spans are newly owned bytes, so initialise
            // them to FF explicitly. Never write an anchor: every 0-bit consumes
            // one-way journal capacity and there is intentionally no erase.
            if (span.source == StoragePlacementSource::AppendedTail) {
                std::fill(
                    rom.bytes().begin() + static_cast<std::ptrdiff_t>(span.offset),
                    rom.bytes().begin() + static_cast<std::ptrdiff_t>(span.offset + span.spanBytes),
                    0xFFu);
            } else {
                const auto first = rom.bytes().begin() + static_cast<std::ptrdiff_t>(span.offset);
                const auto last = first + static_cast<std::ptrdiff_t>(span.spanBytes);
                if (!std::all_of(first, last, [](std::uint8_t value) { return value == 0xFFu; }))
                    throw std::runtime_error("program-only journal span is not erased in source image");
            }
        }
    }
    return result;
}

} // namespace gbasave
