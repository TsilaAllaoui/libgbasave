#include "gbasave/hole_finder.h"

#include "gbabr_region_db.h"

#include <algorithm>
#include <iterator>

namespace gbasave {
namespace {

SaveType decodeGbabrSaveType(std::uint8_t value)
{
    switch (value) {
    case 1u: return SaveType::Sram;
    case 2u: return SaveType::Eeprom512;
    case 3u: return SaveType::Eeprom8K;
    case 4u: return SaveType::Flash512;
    case 5u: return SaveType::Flash1M;
    default: return SaveType::Unknown;
    }
}

std::uint32_t crc32(const std::vector<std::uint8_t> &bytes)
{
    std::uint32_t crc = 0xFFFFFFFFu;
    for (const std::uint8_t byte : bytes) {
        crc ^= byte;
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1u) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

bool allValue(const RomImage &rom, const ByteRange &range, std::uint8_t fill)
{
    if (range.offset > rom.size() || range.size > rom.size() - range.offset)
        return false;
    const auto first = rom.bytes().begin() + static_cast<std::ptrdiff_t>(range.offset);
    const auto last = first + static_cast<std::ptrdiff_t>(range.size);
    return std::all_of(first, last, [fill](std::uint8_t value) { return value == fill; });
}

std::size_t alignUp(std::size_t value, std::size_t alignment)
{
    return (value + alignment - 1u) & ~(alignment - 1u);
}

} // namespace

bool GbabrRegion::containsAlignedEraseBlock(std::size_t blockBytes) const
{
    if (!programOnlyFfSafe() || blockBytes == 0u || (blockBytes & (blockBytes - 1u)) != 0u)
        return false;
    const std::size_t start = alignUp(range.offset, blockBytes);
    return start <= range.end() && blockBytes <= range.end() - start;
}

RomHoleCatalog discoverRomHoles(const RomImage &rom)
{
    RomHoleCatalog result;
    result.gbabrDatabaseVersion = generated_gbabr::kFormatVersion;
    const std::uint32_t romCrc32 = crc32(rom.bytes());

    const auto *begin = std::begin(generated_gbabr::kEntries);
    const auto *end = std::end(generated_gbabr::kEntries);
    const auto *entry = std::lower_bound(begin, end, romCrc32, [](const auto &candidate, std::uint32_t crc) {
        return candidate.crc32 < crc;
    });

    for (; entry != end && entry->crc32 == romCrc32; ++entry) {
        if (entry->romSize != rom.size())
            continue;
        result.gbabrDatabaseMatched = true;
        result.gbabrSaveType = decodeGbabrSaveType(entry->saveType);
        result.gbabrSaveBytes = entry->saveBytes;
        for (std::size_t index = 0u; index < entry->regionCount; ++index) {
            const auto &raw = generated_gbabr::kRegions[entry->firstRegion + index];
            GbabrRegion region{{raw.offset, raw.size}, raw.fill, raw.flags, raw.source};
            // Shared analysis is authority for structure, but exact ROM bytes
            // are still verified at consumption time to reject stale/corrupt DBs.
            if (!region.runtimeSafe() || !allValue(rom, region.range, region.fill))
                continue;
            result.databaseRegions.push_back(region);
            result.databaseRuntimeHoles.push_back(region.range);
            if (region.programOnlyFfSafe())
                result.databaseErasedHoles.push_back(region.range);
        }
        break;
    }

    // A contiguous FF run that reaches the file end is different from an
    // arbitrary internal fill run: there is provably no later source content it
    // can overlap. Expose it as a structural placement fact. Consumers still
    // apply target erase/RWW/reserved-range constraints before using it.
    std::size_t tailStart = rom.size();
    while (tailStart != 0u && rom.bytes()[tailStart - 1u] == 0xFFu)
        --tailStart;
    if (tailStart < rom.size())
        result.trailingFfPadding = {tailStart, rom.size() - tailStart};
    return result;
}

std::string toString(HoleDiscoverySource source)
{
    switch (source) {
    case HoleDiscoverySource::None: return "NONE";
    case HoleDiscoverySource::GbabrDatabase: return "GBABR_DB";
    case HoleDiscoverySource::TrailingFfPadding: return "TRAILING_FF_PADDING";
    }
    return "UNKNOWN";
}

} // namespace gbasave
