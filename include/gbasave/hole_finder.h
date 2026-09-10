#pragma once

#include "gbasave/rom_image.h"
#include "gbasave/save_types.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace gbasave {

struct ByteRange {
    std::size_t offset{};
    std::size_t size{};
    std::size_t end() const { return offset + size; }
};

enum class HoleDiscoverySource {
    None,
    GbabrDatabase,
    TrailingFfPadding,
};

struct GbabrRegion {
    ByteRange range;
    std::uint8_t fill{};
    std::uint8_t flags{};
    std::uint8_t source{};

    bool runtimeSafe() const { return (flags & (1u << 2)) != 0u; }
    bool programOnlyFfSafe() const { return runtimeSafe() && fill == 0xFFu; }
    bool containsAlignedEraseBlock(std::size_t blockBytes) const;
};

// Exact GBABR metadata remains the preferred authority for arbitrary internal
// replacement/storage regions.  A contiguous FF run that reaches the source
// image end is also a generic structural fact and may be consumed safely: no
// later ROM bytes can be hidden behind it.  Persistent NOR storage remains
// stricter and must additionally satisfy backend erase/alignment rules.
// Arbitrary internal FF/00 runs are never trusted on a database miss because
// they may be referenced game data.
struct RomHoleCatalog {
    std::uint32_t gbabrDatabaseVersion{};
    bool gbabrDatabaseMatched{};
    SaveType gbabrSaveType{SaveType::Unknown};
    std::size_t gbabrSaveBytes{};
    std::vector<GbabrRegion> databaseRegions;
    std::vector<ByteRange> databaseRuntimeHoles;
    std::vector<ByteRange> databaseErasedHoles;
    // Contiguous erased bytes reaching the physical end of the source image.
    // Unlike an arbitrary internal FF run this is a structural placement fact:
    // consuming it cannot overwrite later ROM content.
    ByteRange trailingFfPadding{};
};

RomHoleCatalog discoverRomHoles(const RomImage &rom);
std::string toString(HoleDiscoverySource source);

} // namespace gbasave
