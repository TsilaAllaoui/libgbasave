#include "gbasave/embedded/exact_rom_plan.h"
#include "gbasave/embedded/nor_save_layout.h"

#include <cassert>
#include <cstdint>
#include <iostream>

static bool overlaps(std::uint32_t a, std::uint32_t as, std::uint32_t b, std::uint32_t bs)
{
    return a < b + bs && b < a + as;
}

int main()
{
    constexpr std::uint32_t kBPGE = 0x45475042u; // "BPGE" little-endian
    constexpr std::uint8_t kRevision = 0u;
    constexpr std::uint8_t kHeaderChecksum = 129u;
    constexpr std::uint8_t kFixed96 = 150u;

    const std::uint32_t n = gbasave_exact_rom_plan_identity_candidate_count(
        kBPGE, kRevision, kHeaderChecksum, kFixed96);
    assert(n >= 1u);

    GbasaveExactRomPlan p{};
    bool found = false;
    for (std::uint32_t i = 0u; i < n; ++i) {
        GbasaveExactRomPlan c{};
        assert(gbasave_exact_rom_plan_identity_candidate_get(
                   kBPGE, kRevision, kHeaderChecksum, kFixed96, i, &c) ==
               GBASAVE_EXACT_ROM_PLAN_READY);
        if (c.full_crc32 == 3600586444u) {
            p = c;
            found = true;
            break;
        }
    }
    assert(found);
    assert(p.ready != 0u);
    assert(p.rom_size == 0x01000000u);
    assert(p.save_type == SFW_SAVE_FLASH1024K);
    assert(p.region_count != 0u);

    const GbasaveNorSaveTarget m36{0x01000000u, 0x00020000u, 0x00100000u};
    GbasaveNorSaveLayout layout{};
    assert(gbasave_nor_save_layout_resolve(&p, &m36, &layout) == GBASAVE_NOR_SAVE_LAYOUT_READY);
    assert(layout.route == GBASAVE_NOR_SAVE_LAYOUT_RWW_FLASH_COMPACT);
    assert(layout.storage_count == 6u);
    assert(layout.output_bytes == p.rom_size);
    assert(layout.minimum_capacity_bytes == p.rom_size);
    assert((layout.flags & GBASAVE_NOR_SAVE_LAYOUT_PAYLOAD_INTERNAL_FF) != 0u);
    assert((layout.flags & GBASAVE_NOR_SAVE_LAYOUT_STORAGE_INTERNAL_FF) != 0u);

    // Generic geometry-first policy must reproduce the previously hardware-proven
    // full-capacity M36 FLASH1M placement, independent of region-table order.
    assert(layout.payload_offset == 0x00720000u);
    const std::uint32_t expectedStorage[6] = {
        0x00800000u, 0x00820000u, 0x00880000u,
        0x008A0000u, 0x008C0000u, 0x008E0000u
    };
    for (std::uint32_t i = 0u; i < 6u; ++i) assert(layout.storage[i] == expectedStorage[i]);

    GbasaveExactRomPlan reversed = p;
    for (std::uint32_t i = 0u; i < reversed.region_count / 2u; ++i) {
        const std::uint32_t j = reversed.region_count - 1u - i;
        const GbasaveExactRomRegion tmp = reversed.region[i];
        reversed.region[i] = reversed.region[j];
        reversed.region[j] = tmp;
    }
    GbasaveNorSaveLayout reversedLayout{};
    assert(gbasave_nor_save_layout_resolve(&reversed, &m36, &reversedLayout) ==
           GBASAVE_NOR_SAVE_LAYOUT_READY);
    assert(reversedLayout.payload_offset == layout.payload_offset);
    assert(reversedLayout.storage_count == layout.storage_count);
    for (std::uint32_t i = 0u; i < layout.storage_count; ++i)
        assert(reversedLayout.storage[i] == layout.storage[i]);

    const std::uint32_t payloadBank = layout.payload_offset / m36.rww_bank_bytes;
    for (std::uint32_t i = 0u; i < layout.storage_count; ++i) {
        assert((layout.storage[i] % m36.erase_block_bytes) == 0u);
        assert(layout.storage[i] + m36.erase_block_bytes <= p.rom_size);
        assert(layout.storage[i] / m36.rww_bank_bytes != payloadBank);
        assert(!overlaps(layout.storage[i], m36.erase_block_bytes,
                         layout.payload_offset, layout.payload_reserved_bytes));
        for (std::uint32_t j = 0u; j < i; ++j)
            assert(!overlaps(layout.storage[i], m36.erase_block_bytes,
                             layout.storage[j], m36.erase_block_bytes));
    }

    // Streaming/unknown plans deliberately carry no trusted region table and
    // must remain fail-closed for a full-capacity 16 MiB protocol-save ROM.
    GbasaveExactRomPlan streamLike{};
    streamLike.rom_size = p.rom_size;
    streamLike.save_type = SFW_SAVE_FLASH1024K;
    streamLike.ready = 1u;
    streamLike.region_count = 0u;
    streamLike.save_plan.filesize = p.rom_size;
    streamLike.save_plan.save_type = SFW_SAVE_FLASH1024K;
    assert(gbasave_nor_save_layout_resolve(&streamLike, &m36, &layout) ==
           GBASAVE_NOR_SAVE_LAYOUT_UNSUPPORTED);

    std::cout << "LeafGreen exact internal journal layout PASS\n";
    return 0;
}
