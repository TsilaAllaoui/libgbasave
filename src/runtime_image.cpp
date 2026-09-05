#include "gbasave/runtime_image.h"
#include "runtime_blob.h"
#include "runtime_blob_extended.h"

#include <cstring>
#include <stdexcept>

namespace gbasave {
namespace {

std::uint32_t read32(const std::vector<std::uint8_t> &bytes, std::size_t offset)
{
    if (offset + 4u > bytes.size())
        throw std::runtime_error("runtime relocation is outside embedded image");
    return static_cast<std::uint32_t>(bytes[offset]) |
           (static_cast<std::uint32_t>(bytes[offset + 1u]) << 8u) |
           (static_cast<std::uint32_t>(bytes[offset + 2u]) << 16u) |
           (static_cast<std::uint32_t>(bytes[offset + 3u]) << 24u);
}

void write32(std::vector<std::uint8_t> &bytes, std::size_t offset, std::uint32_t value)
{
    if (offset + 4u > bytes.size())
        throw std::runtime_error("runtime relocation is outside embedded image");
    bytes[offset] = static_cast<std::uint8_t>(value);
    bytes[offset + 1u] = static_cast<std::uint8_t>(value >> 8u);
    bytes[offset + 2u] = static_cast<std::uint8_t>(value >> 16u);
    bytes[offset + 3u] = static_cast<std::uint8_t>(value >> 24u);
}

} // namespace

RuntimeImage loadEmbeddedRuntimeImage(NorFlashType norFlashType)
{
    RuntimeImage image;
    const bool useLegacyRuntime =
        norFlashType == NorFlashType::IntelStatusRegister ||
        norFlashType == NorFlashType::IntelStatusRegisterWord10;

    const auto add = [&](const char *name, std::size_t offset, bool thumb) {
        image.symbolOffsets.emplace(name, offset | (thumb ? 1u : 0u));
    };

#define GBASH_ADD_RUNTIME_SYMBOL(NS, SYM) \
    add(#SYM, NS::k_##SYM##_Offset, NS::k_##SYM##_Thumb)
#define GBASH_ADD_ALL_RUNTIME_SYMBOLS(NS) do { \
    GBASH_ADD_RUNTIME_SYMBOL(NS, gbashSwitchFlashBank); \
    GBASH_ADD_RUNTIME_SYMBOL(NS, gbashReadFlashId); \
    GBASH_ADD_RUNTIME_SYMBOL(NS, gbashIdentifyFlash); \
    GBASH_ADD_RUNTIME_SYMBOL(NS, gbashReadFlashByteHelper); \
    GBASH_ADD_RUNTIME_SYMBOL(NS, gbashReadFlashCore); \
    GBASH_ADD_RUNTIME_SYMBOL(NS, gbashReadFlash); \
    GBASH_ADD_RUNTIME_SYMBOL(NS, gbashVerifyFlashCore); \
    GBASH_ADD_RUNTIME_SYMBOL(NS, gbashVerifyFlashSector); \
    GBASH_ADD_RUNTIME_SYMBOL(NS, gbashVerifyFlashSectorNBytes); \
    GBASH_ADD_RUNTIME_SYMBOL(NS, gbashWaitForFlashWrite); \
    GBASH_ADD_RUNTIME_SYMBOL(NS, gbashProgramFlashSector); \
    GBASH_ADD_RUNTIME_SYMBOL(NS, gbashProgramFlashSectorAndVerify); \
    GBASH_ADD_RUNTIME_SYMBOL(NS, gbashProgramFlashSectorAndVerifyNBytes); \
    GBASH_ADD_RUNTIME_SYMBOL(NS, gbashEraseFlashSector); \
    GBASH_ADD_RUNTIME_SYMBOL(NS, gbashEraseFlashChip); \
    GBASH_ADD_RUNTIME_SYMBOL(NS, gbashProgramFlashByte); \
    GBASH_ADD_RUNTIME_SYMBOL(NS, gbashProgramFlashByteByOffset); \
    GBASH_ADD_RUNTIME_SYMBOL(NS, gbashReadEepromDword); \
    GBASH_ADD_RUNTIME_SYMBOL(NS, gbashProgramEepromDword); \
    GBASH_ADD_RUNTIME_SYMBOL(NS, gbashSramBootInit); \
    GBASH_ADD_RUNTIME_SYMBOL(NS, gbashSramBootEntry); \
    GBASH_ADD_RUNTIME_SYMBOL(NS, gbashSramDummyIrq); \
    GBASH_ADD_RUNTIME_SYMBOL(NS, gbashSramReadMirror); \
    GBASH_ADD_RUNTIME_SYMBOL(NS, gbashSramWriteMirror); \
    GBASH_ADD_RUNTIME_SYMBOL(NS, gbashSramVerifyMirror); \
    GBASH_ADD_RUNTIME_SYMBOL(NS, gbashSramHotkeyCommit); \
    GBASH_ADD_RUNTIME_SYMBOL(NS, gbashSramHotkeyIrqEntry); \
    GBASH_ADD_RUNTIME_SYMBOL(NS, gbashSramChainSlotAddressWord); \
    GBASH_ADD_RUNTIME_SYMBOL(NS, gbashSramFallbackDispatcherWord); \
    GBASH_ADD_RUNTIME_SYMBOL(NS, gbashNoOp); \
} while (false)

    if (useLegacyRuntime) {
        image.bytes.assign(
            generated_runtime::kRuntimeBlob,
            generated_runtime::kRuntimeBlob + sizeof(generated_runtime::kRuntimeBlob));
        image.configOffset = generated_runtime::kRuntimeConfigOffset;
        image.linkedBaseAddress = generated_runtime::kRuntimeBaseAddress;
        image.currentBaseAddress = generated_runtime::kRuntimeBaseAddress;
        image.absolute32RelocationOffsets.assign(
            std::begin(generated_runtime::kRuntimeAbsolute32Relocations),
            std::end(generated_runtime::kRuntimeAbsolute32Relocations));
        GBASH_ADD_ALL_RUNTIME_SYMBOLS(generated_runtime);
    } else {
        image.bytes.assign(
            generated_runtime_extended::kRuntimeBlob,
            generated_runtime_extended::kRuntimeBlob + sizeof(generated_runtime_extended::kRuntimeBlob));
        image.configOffset = generated_runtime_extended::kRuntimeConfigOffset;
        image.linkedBaseAddress = generated_runtime_extended::kRuntimeBaseAddress;
        image.currentBaseAddress = generated_runtime_extended::kRuntimeBaseAddress;
        image.absolute32RelocationOffsets.assign(
            std::begin(generated_runtime_extended::kRuntimeAbsolute32Relocations),
            std::end(generated_runtime_extended::kRuntimeAbsolute32Relocations));
        GBASH_ADD_ALL_RUNTIME_SYMBOLS(generated_runtime_extended);
    }

#undef GBASH_ADD_ALL_RUNTIME_SYMBOLS
#undef GBASH_ADD_RUNTIME_SYMBOL
    return image;
}

void patchRuntimeConfig(RuntimeImage &image, const RuntimeConfig &config)
{
    if (image.configOffset + sizeof(RuntimeConfig) > image.bytes.size())
        throw std::runtime_error("embedded GBA runtime config is outside runtime blob");
    std::memcpy(image.bytes.data() + image.configOffset, &config, sizeof(config));
}

void patchRuntimeSymbolWord(RuntimeImage &image, const std::string &symbol, std::uint32_t value)
{
    const auto it = image.symbolOffsets.find(symbol);
    if (it == image.symbolOffsets.end())
        throw std::runtime_error("missing embedded runtime symbol: " + symbol);
    const std::size_t offset = it->second & ~std::size_t(1u);
    write32(image.bytes, offset, value);
}

void relocateRuntimeImage(RuntimeImage &image, std::uint32_t newBaseAddress)
{
    if (image.currentBaseAddress == newBaseAddress)
        return;
    const std::int64_t delta = static_cast<std::int64_t>(newBaseAddress) - image.currentBaseAddress;
    for (const auto offset : image.absolute32RelocationOffsets) {
        const auto oldValue = read32(image.bytes, offset);
        const auto relocated = static_cast<std::uint32_t>(static_cast<std::int64_t>(oldValue) + delta);
        write32(image.bytes, offset, relocated);
    }
    image.currentBaseAddress = newBaseAddress;
}

std::uint32_t runtimeSymbolAddress(const RuntimeImage &image, const std::string &symbol)
{
    const auto it = image.symbolOffsets.find(symbol);
    if (it == image.symbolOffsets.end())
        throw std::runtime_error("missing embedded runtime symbol: " + symbol);
    const auto taggedOffset = it->second;
    const auto thumbBit = static_cast<std::uint32_t>(taggedOffset & 1u);
    const auto byteOffset = static_cast<std::uint32_t>(taggedOffset & ~std::size_t(1u));
    return image.currentBaseAddress + byteOffset + thumbBit;
}

} // namespace gbasave
