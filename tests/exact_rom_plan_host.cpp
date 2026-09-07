#include "gbasave/embedded/exact_rom_plan.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <vector>

namespace {
int read_at(void *context, std::uint32_t offset, std::uint8_t *dst, std::uint32_t bytes)
{
    auto *rom = static_cast<std::vector<std::uint8_t> *>(context);
    if (!rom || offset > rom->size() || bytes > rom->size() - offset)
        return 0;
    for (std::uint32_t i = 0; i < bytes; ++i)
        dst[i] = (*rom)[offset + i];
    return 1;
}
[[noreturn]] void fail(const char *what) { std::fprintf(stderr, "FAIL exact-rom-plan: %s\n", what); std::exit(1); }
}

int main(int argc, char **argv)
{
    if (argc != 2) fail("usage: exact_rom_plan_host <SMA3 USA ROM>");
    std::ifstream file(argv[1], std::ios::binary);
    if (!file) fail("cannot open fixture");
    std::vector<std::uint8_t> rom((std::istreambuf_iterator<char>(file)), {});
    if (rom.size() != 0x400000u) fail("unexpected fixture size");
    std::uint8_t scratch[4096];
    GbasaveRandomAccessRom io{&rom, static_cast<std::uint32_t>(rom.size()), read_at};
    GbasaveExactRomPlan plan{};
    int rc = gbasave_exact_rom_plan_lookup(&io, rom.data(), 0xC0u, scratch, sizeof(scratch), &plan);
    if (rc != GBASAVE_EXACT_ROM_PLAN_READY) fail("SMA3 exact plan was not found/ready");
    if (plan.game_code != 0x45413341u || plan.save_type != SFW_SAVE_EEPROM64K ||
        plan.full_crc32 != 0x40A48276u || plan.save_plan.op_count != 2u ||
        plan.save_plan.irq_count != 7u || plan.region_count != 3u)
        fail("SMA3 compiled metadata differs from analyzer oracle");
    std::uint32_t actual = 0u;
    if (!gbasave_exact_rom_plan_verify_full_crc(&io, scratch, sizeof(scratch), plan.full_crc32, &actual) || actual != plan.full_crc32)
        fail("full CRC verification failed");

    // Source change inside a sampled region must stop exact-plan selection.
    const auto saved = rom[0x100u];
    rom[0x100u] ^= 1u;
    GbasaveExactRomPlan corrupt{};
    rc = gbasave_exact_rom_plan_lookup(&io, rom.data(), 0xC0u, scratch, sizeof(scratch), &corrupt);
    if (rc != GBASAVE_EXACT_ROM_PLAN_NOT_FOUND) fail("sample-corrupt ROM retained exact plan");
    rom[0x100u] = saved;

    // Source change outside all three samples may still identify the candidate,
    // but the mandatory full-source gate must reject it before mutation.
    const std::size_t outside = 0x100000u;
    const auto saved_outside = rom[outside];
    rom[outside] ^= 1u;
    rc = gbasave_exact_rom_plan_lookup(&io, rom.data(), 0xC0u, scratch, sizeof(scratch), &corrupt);
    if (rc != GBASAVE_EXACT_ROM_PLAN_READY) fail("unsampled corruption should still reach candidate plan");
    if (gbasave_exact_rom_plan_verify_full_crc(&io, scratch, sizeof(scratch), corrupt.full_crc32, &actual))
        fail("full-source CRC accepted unsampled corruption");
    rom[outside] = saved_outside;

    std::puts("PASS libgbasave exact ROM plan: compiled analyzer metadata + bounded sample lookup + mandatory full CRC gate");
    return 0;
}
