#include "gbasave/embedded/save_plan_stream.h"
#include "gbasave/embedded/superfw_save_signatures.h"

#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

void put_u16_le(std::vector<std::uint8_t>& rom, std::size_t off, std::uint16_t value)
{
    rom.at(off + 0) = static_cast<std::uint8_t>(value & 0xFFu);
    rom.at(off + 1) = static_cast<std::uint8_t>((value >> 8u) & 0xFFu);
}

void put_u32_le(std::vector<std::uint8_t>& rom, std::size_t off, std::uint32_t value)
{
    rom.at(off + 0) = static_cast<std::uint8_t>(value & 0xFFu);
    rom.at(off + 1) = static_cast<std::uint8_t>((value >> 8u) & 0xFFu);
    rom.at(off + 2) = static_cast<std::uint8_t>((value >> 16u) & 0xFFu);
    rom.at(off + 3) = static_cast<std::uint8_t>((value >> 24u) & 0xFFu);
}

template <std::size_t N>
void put_signature(std::vector<std::uint8_t>& rom, std::size_t off, const std::uint16_t (&sig)[N])
{
    for (std::size_t i = 0; i < N; ++i)
        put_u16_le(rom, off + i * 2u, sig[i]);
}

bool same_plan(const SfwSavePlan& a, const SfwSavePlan& b)
{
    if (a.filesize != b.filesize || a.save_type != b.save_type ||
        a.overflow != b.overflow || a.op_count != b.op_count ||
        a.irq_count != b.irq_count)
        return false;
    for (std::uint32_t i = 0; i < a.op_count; ++i) {
        if (a.op[i].offset != b.op[i].offset || a.op[i].kind != b.op[i].kind ||
            a.op[i].raw_len != b.op[i].raw_len)
            return false;
    }
    for (std::uint32_t i = 0; i < a.irq_count; ++i) {
        if (a.irq_offset[i] != b.irq_offset[i])
            return false;
    }
    return true;
}

int vector_read_at(void *context, std::uint32_t offset, std::uint8_t *dst, std::uint32_t bytes)
{
    auto *rom = static_cast<std::vector<std::uint8_t> *>(context);
    if (!rom || offset > rom->size() || bytes > rom->size() - offset)
        return 0;
    std::memcpy(dst, rom->data() + offset, bytes);
    return 1;
}

[[noreturn]] void fail(const char *what)
{
    std::fprintf(stderr, "FAIL save-plan-stream: %s\n", what);
    std::exit(1);
}

} // namespace

int main()
{
    constexpr std::uint32_t kRomBytes = 0x600u;
    constexpr std::uint32_t kReadOffset = 0x0FCu;  // deliberately crosses 0x100 chunk boundary
    constexpr std::uint32_t kWriteOffset = 0x27Cu; // deliberately crosses another boundary
    constexpr std::uint32_t kIrqOffset = 0x400u;

    std::vector<std::uint8_t> rom(kRomBytes, 0xFFu);
    const char marker[] = "EEPROM_V";
    std::memcpy(rom.data() + 0x20u, marker, sizeof(marker) - 1u);
    put_signature(rom, kReadOffset, eeprom_v2_read_sig);
    put_signature(rom, kWriteOffset, eeprom_v2_write_sig);
    put_u32_le(rom, kIrqOffset, 0x03007FFCu);

    SfwSavePlan whole{};
    sfw_saveplan_init(&whole, kRomBytes);
    sfw_saveplan_scan_filtered(&whole, rom.data(), kRomBytes, 0u, 0u, SFW_SCAN_ALL);
    sfw_saveplan_finalize(&whole);
    if (!sfw_saveplan_usable(&whole))
        fail("whole-ROM reference plan is unexpectedly unusable");

    GbasaveSavePlanStream stream{};
    gbasave_save_plan_stream_begin(&stream, kRomBytes, SFW_SCAN_ALL);
    for (std::uint32_t off = 0; off < kRomBytes; off += 0x100u) {
        const std::uint32_t take = (off + 0x100u <= kRomBytes) ? 0x100u : kRomBytes - off;
        if (!gbasave_save_plan_stream_feed(&stream, off, rom.data() + off, take))
            fail("aligned streaming feed rejected");
    }
    if (!gbasave_save_plan_stream_finish(&stream))
        fail("streaming plan is unexpectedly unusable");
    if (!same_plan(whole, *gbasave_save_plan_stream_plan(&stream)))
        fail("streaming plan differs from whole-ROM plan");

    GbasaveSavePlanStream bad{};
    gbasave_save_plan_stream_begin(&bad, kRomBytes, SFW_SCAN_ALL);
    if (gbasave_save_plan_stream_feed(&bad, 0u, rom.data(), 0x101u))
        fail("non-final unaligned chunk was accepted");

    SfwSavePlan db{};
    sfw_saveplan_init(&db, kRomBytes);
    db.save_type = SFW_SAVE_EEPROM64K;
    db.source_db = 1u;
    db.op_count = 2u;
    db.op[0].kind = SFW_OP_EEPROM_READ;
    db.op[0].offset = kReadOffset;
    db.op[1].kind = SFW_OP_EEPROM_WRITE;
    db.op[1].offset = kWriteOffset;
    db.irq_count = 1u;
    db.irq_offset[0] = kIrqOffset;

    GbasaveRandomAccessRom random_access{&rom, kRomBytes, vector_read_at};
    if (!gbasave_save_plan_offsets_in_bounds(&db, kRomBytes))
        fail("valid DB plan failed bounds validation");
    if (!gbasave_validate_eeprom_db_plan(&random_access, &db))
        fail("valid EEPROM DB plan failed exact-source validation");

    const std::uint8_t saved_irq = rom[kIrqOffset];
    rom[kIrqOffset] ^= 1u;
    if (gbasave_validate_eeprom_db_plan(&random_access, &db))
        fail("corrupt IRQ vector literal was accepted");
    rom[kIrqOffset] = saved_irq;

    const std::uint8_t saved_handler = rom[kReadOffset];
    rom[kReadOffset] ^= 1u;
    if (gbasave_validate_eeprom_db_plan(&random_access, &db))
        fail("corrupt EEPROM handler was accepted");
    rom[kReadOffset] = saved_handler;

    db.op[1].offset = kRomBytes - 64u;
    if (gbasave_save_plan_offsets_in_bounds(&db, kRomBytes))
        fail("out-of-bounds DB handler was accepted");

    std::puts("PASS libgbasave streaming save-plan: whole/stream parity + boundary signatures + fail-closed exact DB validation");
    return 0;
}
