#pragma once

#include "gbasave/rom_image.h"
#include "gbasave/save_types.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace gbasave {

struct GbabrPlanOperation {
    std::size_t offset{};
    std::uint8_t kind{};
    std::vector<std::uint8_t> raw;
};

struct GbabrExactPlan {
    SaveType saveType{SaveType::Unknown};
    std::vector<GbabrPlanOperation> operations;
    std::vector<std::size_t> irqOffsets;
};

// Returns the shared GBABR v6 plan only when its ROM-size + CRC fingerprint
// exactly matches this ROM. The plan describes ROM structure/ABI only; it is
// intentionally independent of NOR family and physical save placement.
bool findGbabrExactPlan(const RomImage &rom, GbabrExactPlan &plan);

} // namespace gbasave
