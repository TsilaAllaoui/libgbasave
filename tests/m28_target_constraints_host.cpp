#include "gbasave/storage_layout.h"
#include "gbasave/rom_image.h"

#include <cassert>
#include <cstdint>
#include <vector>

int main()
{
    using namespace gbasave;
    const std::vector<ByteRange> reserved = {
        {0x00000u, 0x10000u},
        {0x40000u, 0x20000u},
    };
    RomHoleCatalog holes{};

    // Empty/tiny logical ROM: lib-owned runtime must not enter the mixed
    // bottom parameter region.
    RomImage empty{};
    const auto bottom = createRuntimePlacement(empty, holes, 0x4000u, 0x800000u, 4u, 0u, reserved);
    assert(bottom.offset == 0x10000u);

    // Appended runtime that would intersect the physical NVRP read hole must
    // jump to the first safe byte after it.
    RomImage nearProtected(std::vector<std::uint8_t>(0x3F000u, 0xFFu));
    const auto protectedSkip = createRuntimePlacement(
        nearProtected, holes, 0x5000u, 0x800000u, 4u, 0u, reserved);
    assert(protectedSkip.offset == 0x60000u);

    return 0;
}
