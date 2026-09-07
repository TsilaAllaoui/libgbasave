#include "gbasave/embedded/nor_save_layout.h"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>

static GbasaveExactRomPlan basePlan(std::uint32_t bytes, std::uint8_t saveType)
{
    GbasaveExactRomPlan p{};
    p.rom_size = bytes;
    p.save_type = saveType;
    p.ready = 1u;
    p.save_plan.filesize = bytes;
    p.save_plan.save_type = saveType;
    p.save_plan.irq_count = 1u;
    return p;
}

int main()
{
    const GbasaveNorSaveTarget target{0x01000000u, 0x00020000u, 0x00100000u};
    GbasaveNorSaveLayout layout{};

    {
        auto p = basePlan(0x00400000u, SFW_SAVE_SRAM);
        assert(gbasave_nor_save_layout_resolve(&p, &target, &layout) == GBASAVE_NOR_SAVE_LAYOUT_READY);
        assert(layout.route == GBASAVE_NOR_SAVE_LAYOUT_RWW_SRAM);
        assert(layout.payload_offset == 0x00400000u);
        assert(layout.payload_reserved_bytes == 0x4000u);
        assert(layout.storage_count == 1u);
        assert(layout.storage[0] == 0x00500000u);
        assert(layout.storage_block_bytes == 0x00100000u);
        assert(layout.output_bytes == 0x00600000u);
        assert((layout.flags & GBASAVE_NOR_SAVE_LAYOUT_PAYLOAD_APPENDED) != 0u);
    }

    {
        auto p = basePlan(0x00400000u, SFW_SAVE_EEPROM64K);
        assert(gbasave_nor_save_layout_resolve(&p, &target, &layout) == GBASAVE_NOR_SAVE_LAYOUT_READY);
        assert(layout.route == GBASAVE_NOR_SAVE_LAYOUT_RWW_EEPROM);
        assert(layout.payload_offset == 0x00400000u);
        assert(layout.payload_reserved_bytes == 0x1400u);
        assert(layout.storage_count == 2u);
        assert(layout.storage[0] == 0x00500000u);
        assert(layout.storage[1] == 0x00520000u);
        assert(layout.minimum_capacity_bytes == 0x00540000u);
        assert(layout.output_bytes == 0x00400000u);
        assert((layout.flags & GBASAVE_NOR_SAVE_LAYOUT_PAYLOAD_ERASE_BLOCK) != 0u);
    }

    {
        auto p = basePlan(0x00400000u, SFW_SAVE_FLASH1024K);
        assert(gbasave_nor_save_layout_resolve(&p, &target, &layout) == GBASAVE_NOR_SAVE_LAYOUT_READY);
        assert(layout.route == GBASAVE_NOR_SAVE_LAYOUT_RWW_FLASH_COMPACT);
        assert(layout.storage_count == 6u);
        assert(layout.storage[0] == 0x00500000u);
        assert(layout.storage[5] == 0x005A0000u);
        assert(layout.minimum_capacity_bytes == 0x005C0000u);
    }

    {
        auto p = basePlan(0x00800000u, SFW_SAVE_SRAM);
        p.source_kind = GBASAVE_ROM_PLAN_SOURCE_SUPERFW;
        p.region_count = 2u;
        p.region[0] = {0x00600000u, 0x00010000u, 0xFFu,
                       static_cast<std::uint8_t>(GBASAVE_ROM_REGION_FF | GBASAVE_ROM_REGION_SAFE_CODE), 2u, 0u};
        p.region[1] = {0x00700000u, 0x00100000u, 0xFFu,
                       static_cast<std::uint8_t>(GBASAVE_ROM_REGION_FF | GBASAVE_ROM_REGION_POINTER_FREE), 20u, 0u};
        assert(gbasave_nor_save_layout_resolve(&p, &target, &layout) == GBASAVE_NOR_SAVE_LAYOUT_READY);
        assert(layout.payload_offset == 0x00600000u);
        assert(layout.storage[0] == 0x00700000u);
        assert(layout.output_bytes == 0x00800000u);
        assert((layout.flags & GBASAVE_NOR_SAVE_LAYOUT_PAYLOAD_INTERNAL_FF) != 0u);
        assert((layout.flags & GBASAVE_NOR_SAVE_LAYOUT_SOURCE_SUPERFW) != 0u);
    }

    {
        auto p = basePlan(0x00400000u, SFW_SAVE_FLASH1024K);
        GbasaveNorSaveTarget noRww{0x01000000u, 0x00020000u, 0u};
        assert(gbasave_nor_save_layout_resolve(&p, &noRww, &layout) == GBASAVE_NOR_SAVE_LAYOUT_UNSUPPORTED);
    }

    std::cout << "generic NOR save-layout host model PASS\n";
    return 0;
}
