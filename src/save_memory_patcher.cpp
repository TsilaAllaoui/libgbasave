#include "gbasave/save_memory_patcher.h"

#include "gbasave/gbabr_plan.h"

#include <algorithm>
#include <stdexcept>

namespace gbasave {
namespace {

uint8_t sfwTypeForSaveType(SaveType type)
{
    switch (type) {
    case SaveType::Sram: return SFW_SAVE_SRAM;
    case SaveType::Eeprom512: return SFW_SAVE_EEPROM4K;
    case SaveType::Eeprom8K: return SFW_SAVE_EEPROM64K;
    case SaveType::Flash512: return SFW_SAVE_FLASH512K;
    case SaveType::Flash1M: return SFW_SAVE_FLASH1024K;
    default: return SFW_SAVE_NONE;
    }
}

bool exactPlanToSfw(const RomImage &rom, SfwSavePlan &out)
{
    GbabrExactPlan exact;
    if (!findGbabrExactPlan(rom, exact))
        return false;

    const auto saveType = sfwTypeForSaveType(exact.saveType);
    if (saveType == SFW_SAVE_NONE)
        return false;
    if (exact.operations.size() > SFW_MAX_SAVE_OPS || exact.irqOffsets.size() > SFW_MAX_IRQ_OPS)
        throw std::runtime_error("exact save plan exceeds embedded save-memory limits");

    sfw_saveplan_init(&out, static_cast<std::uint32_t>(rom.size()));
    out.save_type = saveType;
    out.source_db = 1u;
    for (const auto &operation : exact.operations) {
        if (operation.raw.size() > SFW_MAX_RAW_BYTES)
            throw std::runtime_error("exact save plan raw operation exceeds embedded limit");
        auto &dst = out.op[out.op_count++];
        dst.offset = static_cast<std::uint32_t>(operation.offset);
        dst.kind = operation.kind;
        dst.raw_len = static_cast<std::uint8_t>(operation.raw.size());
        for (std::size_t index = 0u; index < operation.raw.size(); ++index)
            dst.raw[index] = operation.raw[index];
    }
    for (const auto offset : exact.irqOffsets)
        out.irq_offset[out.irq_count++] = static_cast<std::uint32_t>(offset);
    return sfw_saveplan_usable(&out) != 0;
}

} // namespace

SfwSavePlan SaveMemoryPatcher::analyze(const RomImage &rom) const
{
    if (rom.empty() || rom.size() > 0xFFFFFFFFu)
        throw std::runtime_error("save-memory analysis requires a non-empty <=4 GiB GBA ROM");

    SfwSavePlan plan{};
    if (exactPlanToSfw(rom, plan))
        return plan;

    sfw_saveplan_init(&plan, static_cast<std::uint32_t>(rom.size()));
    const auto &bytes = rom.bytes();
    sfw_saveplan_scan_filtered(
        &plan,
        bytes.data(),
        static_cast<std::uint32_t>(bytes.size()),
        0u,
        0u,
        SFW_SCAN_ALL);
    sfw_saveplan_finalize(&plan);
    if (!sfw_saveplan_usable(&plan))
        throw std::runtime_error("no complete supported SRAM/EEPROM/FLASH save plan found");
    return plan;
}

SaveMemoryPatchResult SaveMemoryPatcher::patch(
    RomImage &rom,
    const GbasaveSaveMemoryProfileDescriptor &profile,
    std::size_t romCapacityBytes) const
{
    if (romCapacityBytes == 0u || romCapacityBytes > 0xFFFFFFFFu)
        throw std::runtime_error("invalid save-memory ROM capacity");

    SaveMemoryPatchResult result{};
    result.profileKey = profile.key ? profile.key : "";
    result.profileDisplayName = profile.display_name ? profile.display_name : "";
    result.originalRomBytes = rom.size();
    result.savePlan = analyze(rom);

    const std::uint32_t original = static_cast<std::uint32_t>(rom.size());
    const std::uint32_t capacity = static_cast<std::uint32_t>(romCapacityBytes);
    std::uint32_t trailingSlot = 0u;
    int trailingAvailable = 0;
    const std::uint32_t runtimeBytes = gbasave_save_memory_banked_runtime_size();
    if (gbasave_save_memory_patch_trailing_ff_probe_needed(&result.savePlan, original, capacity) &&
        runtimeBytes <= original) {
        const std::uint32_t start = original > 0x10000u ? original - 0x10000u : 0u;
        const auto &bytes = rom.bytes();
        for (std::uint32_t candidate = original - runtimeBytes;;) {
            if ((candidate & 3u) == 0u && candidate >= start) {
                bool erased = true;
                for (std::uint32_t index = 0u; index < runtimeBytes; ++index) {
                    if (bytes[candidate + index] != 0xFFu) {
                        erased = false;
                        break;
                    }
                }
                if (erased) {
                    trailingAvailable = 1;
                    trailingSlot = candidate;
                    break;
                }
            }
            if (candidate < start + 4u)
                break;
            candidate -= 4u;
        }
    }

    const int rc = gbasave_save_memory_patch_plan_build(
        &result.patchPlan,
        &result.savePlan,
        &profile.target,
        original,
        original,
        capacity,
        trailingAvailable,
        trailingSlot);
    if (rc == GBASAVE_SAVE_MEMORY_PLAN_UNSUPPORTED)
        throw std::runtime_error("save type is unsupported by selected SRAM/FRAM profile");
    if (rc == GBASAVE_SAVE_MEMORY_PLAN_CAPACITY)
        throw std::runtime_error("save-memory runtime does not fit selected ROM capacity");
    if (rc != GBASAVE_SAVE_MEMORY_PLAN_OK)
        throw std::runtime_error("failed to build save-memory patch plan");

    rom.resize(result.patchPlan.virtual_size, 0xFFu);
    constexpr std::uint32_t kChunkBytes = 64u * 1024u;
    auto &bytes = rom.bytes();
    for (std::uint32_t offset = 0u; offset < bytes.size(); offset += kChunkBytes) {
        const auto remaining = bytes.size() - offset;
        const auto chunkBytes = static_cast<std::uint32_t>(
            std::min<std::size_t>(kChunkBytes, remaining));
        gbasave_save_memory_patch_apply_overlay(
            &result.patchPlan,
            offset,
            bytes.data() + offset,
            chunkBytes);
    }
    result.outputRomBytes = rom.size();
    return result;
}

} // namespace gbasave
