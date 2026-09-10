#include "gbasave/save_memory_patcher.h"

#include "gbasave/gbabr_plan.h"
#include "gbasave/flash_scanner.h"
#include "streaming/internal/save_memory_stub_abi.h"

#include <algorithm>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

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

std::string joinMissingCapabilities(const std::vector<std::string> &items)
{
    if (items.empty())
        return "none";
    std::ostringstream out;
    for (std::size_t i = 0u; i < items.size(); ++i) {
        if (i != 0u)
            out << ", ";
        out << items[i];
    }
    return out.str();
}

bool hasCapability(const GbasaveSaveMemoryTarget &target, std::uint32_t capability)
{
    return (target.capabilities & capability) != 0u;
}

std::string explainUnsupportedSaveMemoryTarget(
    const SfwSavePlan &savePlan,
    const GbasaveSaveMemoryProfileDescriptor &profile)
{
    const auto &target = profile.target;
    const std::uint32_t logicalBytes = sfw_save_type_size(savePlan.save_type);
    std::vector<std::string> missing;
    const auto addMissing = [&missing](std::string requirement) {
        if (std::find(missing.begin(), missing.end(), requirement) == missing.end())
            missing.push_back(std::move(requirement));
    };

    if (logicalBytes > target.total_bytes)
        addMissing("backing memory >= " + std::to_string(logicalBytes) + " bytes");

    switch (savePlan.save_type) {
    case SFW_SAVE_SRAM:
        if (!hasCapability(target, GBASAVE_SAVE_MEMORY_CAP_DIRECT_SRAM_GAMEPLAY))
            addMissing("DIRECT_SRAM_GAMEPLAY");
        if (!hasCapability(target, GBASAVE_SAVE_MEMORY_CAP_SRAM_WINDOW))
            addMissing("SRAM_WINDOW");
        if (target.gba_window_base == 0u)
            addMissing("mapped GBA save window");
        if (target.window_bytes < logicalBytes)
            addMissing("visible window >= " + std::to_string(logicalBytes) + " bytes");
        break;
    case SFW_SAVE_EEPROM4K:
    case SFW_SAVE_EEPROM64K:
        if (!hasCapability(target, GBASAVE_SAVE_MEMORY_CAP_EEPROM_TO_RAM))
            addMissing("EEPROM_TO_RAM");
        if (!hasCapability(target, GBASAVE_SAVE_MEMORY_CAP_SRAM_WINDOW))
            addMissing("SRAM_WINDOW");
        if (target.gba_window_base == 0u)
            addMissing("mapped GBA save window");
        if (target.window_bytes < logicalBytes)
            addMissing("visible window >= " + std::to_string(logicalBytes) + " bytes");
        break;
    case SFW_SAVE_FLASH512K:
        if (!hasCapability(target, GBASAVE_SAVE_MEMORY_CAP_FLASH512_TO_RAM))
            addMissing("FLASH512_TO_RAM");
        if (target.gba_window_base == 0u)
            addMissing("mapped GBA save window");
        if (target.window_bytes < 0x10000u)
            addMissing("visible window >= 65536 bytes");
        break;
    case SFW_SAVE_FLASH1024K:
        if (!hasCapability(target, GBASAVE_SAVE_MEMORY_CAP_FLASH1M_TO_BANKED_RAM))
            addMissing("FLASH1M_TO_BANKED_RAM");
        if (!hasCapability(target, GBASAVE_SAVE_MEMORY_CAP_BANKED_WINDOW))
            addMissing("BANKED_WINDOW");
        if (target.total_bytes < 0x20000u)
            addMissing("backing memory >= 131072 bytes");
        if (target.window_bytes < 0x10000u)
            addMissing("visible window >= 65536 bytes");
        if (target.bank_count < 2u)
            addMissing("at least 2 banks");
        if (target.selector_kind == GBASAVE_SAVE_MEMORY_SELECTOR_NONE || target.selector_gba_address == 0u)
            addMissing("declared bank selector");
        break;
    default:
        addMissing("supported SRAM/EEPROM/FLASH save protocol");
        break;
    }

    std::ostringstream out;
    out << "target '" << (profile.key ? profile.key : "unknown") << "' cannot implement detected "
        << sfw_save_type_name(savePlan.save_type) << " save semantics. "
        << "Missing requirement(s): " << joinMissingCapabilities(missing) << ". "
        << "Target provides total=" << target.total_bytes
        << " bytes, window=" << target.window_bytes
        << " bytes, banks=" << static_cast<unsigned>(target.bank_count)
        << ". The ROM was not modified.";
    return out.str();
}

std::size_t trailingFfBytes(const RomImage &rom)
{
    std::size_t start = rom.size();
    while (start != 0u && rom.bytes()[start - 1u] == 0xFFu)
        --start;
    return rom.size() - start;
}



std::uint32_t minimumReplacementBytes(std::uint8_t kind)
{
    using namespace save_memory_stub_abi;
    switch (kind) {
    case SFW_OP_FLASH_READ: return kFlashReadLen;
    case SFW_OP_FLASH_ERASE_CHIP: return kFlashEraseChipLen;
    case SFW_OP_FLASH_ERASE_SECTOR: return kFlashEraseSectorLen;
    case SFW_OP_FLASH_WRITE_SECTOR: return kFlashWriteSectorLen;
    case SFW_OP_FLASH_WRITE_BYTE: return kFlashWriteByteLen;
    case SFW_OP_FLASH_IDENT: return kFlashIdent1MLen;
    case SFW_OP_FLASH_VERIFY: return kThumbRet0Len;
    default: return 0u;
    }
}

bool hasSaveOpAt(const SfwSavePlan &plan, std::uint8_t kind, std::size_t offset)
{
    return std::any_of(plan.op, plan.op + plan.op_count, [kind, offset](const SfwSaveOp &operation) {
        return operation.kind == kind && operation.offset == offset;
    });
}

void addMappedSaveOp(SfwSavePlan &plan, const FunctionInfo &function, std::uint8_t kind)
{
    const auto minimumBytes = minimumReplacementBytes(kind);
    if (minimumBytes == 0u)
        throw std::runtime_error("unsupported semantic save-memory operation kind");
    if (function.size < minimumBytes) {
        throw std::runtime_error(
            "semantic FLASH routine is smaller than the proven replacement stub: " +
            function.name);
    }
    if (function.offset > 0xFFFFFFFFu)
        throw std::runtime_error("semantic FLASH routine offset exceeds save-plan ABI");
    if (hasSaveOpAt(plan, kind, function.offset))
        return;
    if (plan.op_count >= SFW_MAX_SAVE_OPS)
        throw std::runtime_error("semantic FLASH save plan exceeds embedded operation limit");

    auto &operation = plan.op[plan.op_count++];
    operation = {};
    operation.kind = kind;
    operation.offset = static_cast<std::uint32_t>(function.offset);
}

bool isReplacementEntryRole(FlashRoutineRole role)
{
    switch (role) {
    case FlashRoutineRole::Read:
    case FlashRoutineRole::ReadId:
    case FlashRoutineRole::VerifySector:
    case FlashRoutineRole::VerifySectorNBytes:
    case FlashRoutineRole::EraseChip:
    case FlashRoutineRole::EraseSector:
    case FlashRoutineRole::ProgramByte:
    case FlashRoutineRole::ProgramSector:
        return true;
    default:
        return false;
    }
}

bool bodyContainsOffset(const FunctionInfo &function, std::size_t offset)
{
    return offset >= function.offset && offset < function.offset + function.bodySize;
}

void proveBankSelectorCallsAreBypassed(const FlashMap &map)
{
    const auto &switchBank = map.function(FlashRoutineRole::SwitchBank);
    for (const auto callsite : switchBank.directCallsites) {
        const bool covered = std::any_of(map.functions.begin(), map.functions.end(), [&](const FunctionInfo &function) {
            return isReplacementEntryRole(function.role) && bodyContainsOffset(function, callsite);
        });
        if (!covered) {
            throw std::runtime_error(
                "FLASH bank selector has a direct caller outside the routines replaced by the save-memory backend");
        }
    }
}

bool flashMapToSfwPlan(const RomImage &rom, SfwSavePlan &out)
{
    FlashMap map;
    try {
        map = FlashScanner{}.scan(rom);
    } catch (const std::runtime_error &) {
        return false;
    }

    // FlashScanner currently accepts only a structurally proven FLASH1M
    // library/setup-table family. Keep this adapter capability-driven: map
    // semantic routine roles to the existing save-memory ABI, never titles,
    // game codes, hashes, or fixed ROM offsets.
    sfw_saveplan_init(&out, static_cast<std::uint32_t>(rom.size()));
    out.save_type = SFW_SAVE_FLASH1024K;
    out.source_db = 0u;

    for (const auto &function : map.functions) {
        switch (function.role) {
        case FlashRoutineRole::Read:
            addMappedSaveOp(out, function, SFW_OP_FLASH_READ);
            break;
        case FlashRoutineRole::EraseChip:
            addMappedSaveOp(out, function, SFW_OP_FLASH_ERASE_CHIP);
            break;
        case FlashRoutineRole::EraseSector:
            addMappedSaveOp(out, function, SFW_OP_FLASH_ERASE_SECTOR);
            break;
        case FlashRoutineRole::ProgramSector:
            addMappedSaveOp(out, function, SFW_OP_FLASH_WRITE_SECTOR);
            break;
        case FlashRoutineRole::ProgramByte:
            addMappedSaveOp(out, function, SFW_OP_FLASH_WRITE_BYTE);
            break;
        case FlashRoutineRole::ReadId:
            addMappedSaveOp(out, function, SFW_OP_FLASH_IDENT);
            break;
        case FlashRoutineRole::VerifySector:
        case FlashRoutineRole::VerifySectorNBytes:
            addMappedSaveOp(out, function, SFW_OP_FLASH_VERIFY);
            break;
        default:
            break;
        }
    }

    if (!sfw_saveplan_usable(&out))
        return false;

    // The RAM-backed FLASH1M route replaces every public/physical operation
    // that can issue FLASH commands. Prove direct SwitchFlashBank callers are
    // inside those replaced entries so an unpatched selector cannot leak a
    // NOR command sequence into the SRAM/FRAM window.
    proveBankSelectorCallsAreBypassed(map);
    return true;
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

struct DetailedSavePlan {
    SfwSavePlan plan{};
    SavePlanAnalysisSource source{SavePlanAnalysisSource::SignatureScan};
};

DetailedSavePlan analyzeSavePlanDetailed(const RomImage &rom)
{
    if (rom.empty() || rom.size() > 0xFFFFFFFFu)
        throw std::runtime_error("save-memory analysis requires a non-empty <=4 GiB GBA ROM");

    DetailedSavePlan result;
    if (exactPlanToSfw(rom, result.plan)) {
        result.source = SavePlanAnalysisSource::ExactDatabase;
        return result;
    }

    sfw_saveplan_init(&result.plan, static_cast<std::uint32_t>(rom.size()));
    const auto &bytes = rom.bytes();
    sfw_saveplan_scan_filtered(
        &result.plan, bytes.data(), static_cast<std::uint32_t>(bytes.size()),
        0u, 0u, SFW_SCAN_ALL);
    sfw_saveplan_finalize(&result.plan);
    if (sfw_saveplan_usable(&result.plan)) {
        result.source = SavePlanAnalysisSource::SignatureScan;
        return result;
    }

    // Exact byte signatures are intentionally the fast path, but rebuilt or
    // relocated Nintendo save libraries can preserve the FLASH ABI while
    // changing every instruction byte. Reuse FlashScanner's structural proof
    // as a conservative fallback instead of adding per-ROM exceptions.
    if (flashMapToSfwPlan(rom, result.plan)) {
        result.source = SavePlanAnalysisSource::SemanticFlashStructure;
        return result;
    }

    throw std::runtime_error(
        "save analysis could not prove a complete supported save API. "
        "Recognized signatures/structures were incomplete or ambiguous; no patch was generated.");
}

} // namespace

const char *toString(SavePlanAnalysisSource source)
{
    switch (source) {
    case SavePlanAnalysisSource::ExactDatabase: return "EXACT_SHARED_PLAN";
    case SavePlanAnalysisSource::SignatureScan: return "SDK_SIGNATURE_SCAN";
    case SavePlanAnalysisSource::SemanticFlashStructure: return "SEMANTIC_FLASH_STRUCTURE";
    }
    return "UNKNOWN";
}

SfwSavePlan SaveMemoryPatcher::analyze(const RomImage &rom) const
{
    return analyzeSavePlanDetailed(rom).plan;
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
    const auto analyzed = analyzeSavePlanDetailed(rom);
    result.savePlan = analyzed.plan;
    result.analysisSource = analyzed.source;

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
        throw std::runtime_error(explainUnsupportedSaveMemoryTarget(result.savePlan, profile));
    if (rc == GBASAVE_SAVE_MEMORY_PLAN_CAPACITY) {
        const std::size_t runtimeBytesNeeded = gbasave_save_memory_banked_runtime_size();
        const std::size_t ffTailBytes = trailingFfBytes(rom);
        throw std::runtime_error(
            "ROM-space placement failed for save-memory translation on target '" + result.profileKey +
            "': source ROM=" + std::to_string(result.originalRomBytes) +
            " bytes, selected ROM capacity=" + std::to_string(romCapacityBytes) +
            " bytes, translation runtime=" + std::to_string(runtimeBytesNeeded) +
            " bytes, trailing 0xFF padding=" + std::to_string(ffTailBytes) +
            " bytes. The save-memory hardware itself is compatible, but no safe ROM slot for the "
            "translation payload was available. Increase -c only if the cartridge really maps more ROM "
            "address space; otherwise use a target/profile with sufficient mapped ROM space. The ROM was not modified.");
    }
    if (rc != GBASAVE_SAVE_MEMORY_PLAN_OK)
        throw std::runtime_error(
            "save-memory patch planner rejected the analyzed save API or target geometry as invalid; "
            "no output was generated. Re-run 'GBASaveHandler s <rom.gba>' and 'GBASaveHandler info " +
            result.profileKey + "' to inspect the detected save plan and target capabilities.");

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
