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

// Shared GBABR metadata is the only authority for in-ROM replacement/storage.
// Runtime caves may be structurally safe 00 or FF regions. Persistent erase
// storage is stricter and consumes only SAFE_CODE+FF ranges that contain a
// complete backend erase unit. Database misses append instead of running an
// independent byte-pattern safety oracle, so no fallback can bypass GBABR.
struct RomHoleCatalog {
    std::uint32_t gbabrDatabaseVersion{};
    bool gbabrDatabaseMatched{};
    SaveType gbabrSaveType{SaveType::Unknown};
    std::size_t gbabrSaveBytes{};
    std::vector<GbabrRegion> databaseRegions;
    std::vector<ByteRange> databaseRuntimeHoles;
    std::vector<ByteRange> databaseErasedHoles;
};

RomHoleCatalog discoverRomHoles(const RomImage &rom);
std::string toString(HoleDiscoverySource source);

} // namespace gbasave
