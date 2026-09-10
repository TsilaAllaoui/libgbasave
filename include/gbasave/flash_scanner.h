#pragma once

#include "gbasave/flash_library_profile.h"
#include "gbasave/rom_image.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace gbasave {

struct FunctionInfo {
    FlashRoutineRole role{};
    std::string name;
    std::size_t offset{};
    // Conservative contiguous bytes available from the entry for a replacement
    // stub. This may stop at the first return path and is intentionally not the
    // same thing as the whole routine body.
    std::size_t size{};
    // Conservative span covering all locally reachable basic blocks. Used for
    // ownership/safety proofs only; never as overwrite capacity.
    std::size_t bodySize{};
    std::vector<std::uint8_t> signature;
    std::string evidence;
    std::vector<std::size_t> directCallsites;
};

struct SetupProfile {
    std::size_t offset{};
    std::uint32_t programFlashByte{};
    std::uint32_t programFlashSector{};
    std::uint32_t eraseFlashChip{};
    std::uint32_t eraseFlashSector{};
    std::uint32_t waitForFlashWrite{};
    std::uint32_t maxTime{};
    std::uint32_t romSize{};
    std::uint32_t sectorSize{};
    std::uint16_t sectorShift{};
    std::uint16_t sectorCount{};
    std::uint16_t flashId{};
    std::string chipName;
};

enum class FlashSetupDiscoverySource {
    MarkerPointerTable,
    GlobalStructuralScan,
};

struct FlashMap {
    const FlashLibraryProfile *libraryProfile{};
    std::string detectedMarker;
    std::size_t markerOffset{};
    std::size_t setupTableOffset{static_cast<std::size_t>(-1)};
    FlashSetupDiscoverySource setupDiscoverySource{FlashSetupDiscoverySource::MarkerPointerTable};
    std::vector<SetupProfile> setupProfiles;
    std::vector<FunctionInfo> functions;
    bool allSignaturesUnique{};

    const FunctionInfo &function(FlashRoutineRole role) const;
};

std::string toString(FlashSetupDiscoverySource source);

class FlashScanner {
public:
    FlashMap scan(const RomImage &rom) const;
    static std::string toJson(const RomImage &rom, const FlashMap &map, const std::string &sha256);
    static std::string toText(const RomImage &rom, const FlashMap &map, const std::string &sha256);
};

} // namespace gbasave
