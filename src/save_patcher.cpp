#include "gbasave/save_patcher.h"
#include "gbasave/save_memory_patcher.h"
#include "gbasave/m36_compat_patcher.h"

#include "gbasave/flash_scanner.h"
#include "gbasave/runtime_config.h"
#include "gbasave/runtime_image.h"
#include "gbasave/storage_image.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <map>
#include <stdexcept>

namespace gbasave {
namespace {

constexpr std::uint32_t kGbaRomBase = 0x08000000u;
constexpr std::uint32_t kCurrentBankStateAddress = 0x0203BFF0u; // private EWRAM state; 0x030079E0 is Nintendo ReadFlash1 pointer
constexpr std::uint32_t kSramShadowAddress = 0x02027000u;
constexpr std::uint32_t kLegacySramStateAddress = 0x0203BFE0u;
constexpr std::uint16_t kSramHotkeyRaw = 0x00F9u; // L+R+SELECT+B, active low
constexpr std::size_t kSramJournalHeaderBytes = 32u;
constexpr std::size_t kMinimumSramJournalRecords = 2u;
constexpr std::size_t kPreferredSramJournalRecords = 16u;

// Nintendo FLASH1M_V102 globals used by IdentifyFlash/driver dispatch.
constexpr std::uint32_t kGlobalWaitForWrite = 0x030079E4u;
constexpr std::uint32_t kGlobalProgramSector = 0x030079E8u;
constexpr std::uint32_t kGlobalGeometry = 0x030079ECu;
constexpr std::uint32_t kGlobalEraseChip = 0x030079F4u;
constexpr std::uint32_t kGlobalEraseSector = 0x030079F8u;
constexpr std::uint32_t kGlobalMaxTime = 0x030079FCu;

std::uint16_t powerOfTwoShift(std::size_t value, const char *what)
{
    if (value == 0u || (value & (value - 1u)) != 0u)
        throw std::runtime_error(std::string(what) + " must be a power of two");
    std::uint16_t shift = 0;
    while (value > 1u) {
        value >>= 1u;
        ++shift;
    }
    return shift;
}

std::uint32_t runtimeAddress(const RuntimeImage &runtime, const std::string &symbol)
{
    return runtimeSymbolAddress(runtime, symbol);
}

void writeThumbTailJump(RomImage &rom, std::size_t offset, std::uint32_t targetAddress)
{
    if ((targetAddress & 1u) == 0u)
        throw std::runtime_error("Thumb trampoline target is not Thumb");
    const std::array<std::uint8_t, 8> trampoline = {
        0x00, 0x4B, // ldr r3,[pc,#0]
        0x18, 0x47, // bx r3
        static_cast<std::uint8_t>(targetAddress),
        static_cast<std::uint8_t>(targetAddress >> 8),
        static_cast<std::uint8_t>(targetAddress >> 16),
        static_cast<std::uint8_t>(targetAddress >> 24),
    };
    rom.write(offset, trampoline.data(), trampoline.size());
}

void writeArmTailJump(RomImage &rom, std::size_t offset, std::uint32_t thumbTargetAddress)
{
    if ((thumbTargetAddress & 1u) == 0u)
        throw std::runtime_error("ARM bridge target must be a Thumb entry");
    const std::array<std::uint8_t, 12> trampoline = {
        0x00, 0x30, 0x9F, 0xE5, // ldr r3,[pc,#0]
        0x13, 0xFF, 0x2F, 0xE1, // bx r3
        static_cast<std::uint8_t>(thumbTargetAddress),
        static_cast<std::uint8_t>(thumbTargetAddress >> 8),
        static_cast<std::uint8_t>(thumbTargetAddress >> 16),
        static_cast<std::uint8_t>(thumbTargetAddress >> 24),
    };
    rom.write(offset, trampoline.data(), trampoline.size());
}


void writeArmBranch(RomImage &rom, std::size_t offset, std::uint32_t armTargetAddress)
{
    if ((armTargetAddress & 3u) != 0u)
        throw std::runtime_error("ARM branch target is not word aligned");
    const std::int64_t sourceAddress = static_cast<std::int64_t>(kGbaRomBase + offset + 8u);
    const std::int64_t delta = static_cast<std::int64_t>(armTargetAddress) - sourceAddress;
    if ((delta & 3ll) != 0ll || delta < -0x02000000ll || delta > 0x01FFFFFCl)
        throw std::runtime_error("ARM branch target is out of range");
    const std::uint32_t instruction = 0xEA000000u |
        (static_cast<std::uint32_t>(delta >> 2u) & 0x00FFFFFFu);
    rom.write32(offset, instruction);
}


std::uint32_t decodeArmBranchTarget(std::uint32_t instruction, std::size_t romOffset)
{
    if ((instruction & 0xFF000000u) != 0xEA000000u)
        throw std::runtime_error("SRAM hotkey bootstrap requires an unconditional ARM B reset entry");
    std::int32_t displacement = static_cast<std::int32_t>((instruction & 0x00FFFFFFu) << 8u) >> 6u;
    return static_cast<std::uint32_t>(kGbaRomBase + romOffset + 8u + displacement);
}

std::uint16_t defaultFlashId(SaveType type)
{
    switch (type) {
    case SaveType::Flash1M: return 0x09C2u;
    case SaveType::Flash512: return 0x1B32u;
    default: return 0u;
    }
}

std::uint16_t effectiveFlashId(const SaveLibraryMatch &saveLibrary, const PatchOptions &options)
{
    return options.forcedFlashId != 0u ? options.forcedFlashId : defaultFlashId(saveLibrary.type);
}

const SetupProfile &selectForcedSetupProfile(const FlashMap &flashMap, std::uint16_t flashId)
{
    const auto it = std::find_if(flashMap.setupProfiles.begin(), flashMap.setupProfiles.end(), [flashId](const auto &profile) {
        return profile.flashId == flashId;
    });
    if (it == flashMap.setupProfiles.end())
        throw std::runtime_error("requested FLASH1M ID does not exist in the stock setup table: " + hex(flashId, 4));
    return *it;
}

RuntimeConfig makeBaseConfig(const SaveLibraryMatch &saveLibrary, const PatchOptions &options)
{
    RuntimeConfig config{};
    config.magic = kRuntimeConfigMagic;
    config.version = kRuntimeConfigVersion;
    config.norFlashType = static_cast<std::uint16_t>(options.norFlashType);
    config.forcedFlashId = effectiveFlashId(saveLibrary, options);
    config.norProgramCommand = norBackendDescriptor(options.norFlashType).runtimeProgramSelector;
    config.logicalSaveSizeBytes = static_cast<std::uint32_t>(saveLibrary.geometry.totalBytes);
    config.logicalBankSizeBytes = static_cast<std::uint32_t>(saveLibrary.geometry.bankBytes);
    config.logicalUnitSizeBytes = static_cast<std::uint32_t>(saveLibrary.geometry.sectorBytes);
    config.physicalBlockBytes = static_cast<std::uint32_t>(options.programStorageBlockBytes);
    config.logicalUnitCount = static_cast<std::uint16_t>(saveLibrary.geometry.totalBytes / saveLibrary.geometry.sectorBytes);
    config.logicalUnitShift = powerOfTwoShift(saveLibrary.geometry.sectorBytes, "logical save unit size");
    if (saveLibrary.geometry.sectorBytes > 0x10000u)
        throw std::runtime_error("logical save unit exceeds runtime offset-mask range");
    config.logicalUnitOffsetMask = static_cast<std::uint16_t>(saveLibrary.geometry.sectorBytes - 1u);
    config.unitsPerBank = static_cast<std::uint16_t>(saveLibrary.geometry.sectorsPerBank);
    config.bankCount = static_cast<std::uint16_t>(saveLibrary.geometry.bankCount);
    config.bankSelectShift = saveLibrary.geometry.sectorsPerBank != 0u
        ? powerOfTwoShift(saveLibrary.geometry.sectorsPerBank, "units per bank")
        : 0u;
    config.currentBankStateAddress = kCurrentBankStateAddress;
    config.originalSramBaseAddress = 0x0E000000u;
    config.sramRefreshTriggerOffset = 0xFFFFFFFFu;
    return config;
}

void configureVersionedSlots(RuntimeConfig &config, const StorageLayout &storage)
{
    config.storageMode = static_cast<std::uint16_t>(RuntimeStorageMode::FlashVersionedSlots);
    const std::size_t unitBytes = config.logicalUnitSizeBytes;
    // Metadata contains one 16-bit monotonic head word plus one 16-bit tag
    // per slot. The head makes reads O(1) instead of scanning every slot.
    const std::size_t slots = (storage.physicalEraseBlockBytes - 2u) / (unitBytes + 2u);
    if (slots == 0u || slots > 16u)
        throw std::runtime_error("physical block cannot hold a supported versioned save slot set");
    const std::size_t metadataOffset = slots * unitBytes;
    if (metadataOffset + 2u + slots * 2u > storage.physicalEraseBlockBytes)
        throw std::runtime_error("versioned-slot metadata exceeds physical block");
    config.flashSlotsPerBlock = static_cast<std::uint16_t>(slots);
    config.flashSlotMetadataOffsetBytes = static_cast<std::uint32_t>(metadataOffset);
    if (storage.blocks.size() < config.logicalUnitCount)
        throw std::runtime_error("versioned storage has fewer primary blocks than logical units");
    if (storage.blocks.size() > kMaxRuntimeLogicalUnits)
        throw std::runtime_error("versioned storage exceeds runtime physical-block mapping table");
    config.physicalStorageBlockCount = static_cast<std::uint16_t>(storage.blocks.size());
    for (std::size_t index = 0; index < storage.blocks.size(); ++index)
        config.storageBlockAddresses[index] = storage.block(index).gbaAddress();
}

void patchFlash1m(
    RomImage &rom,
    const FlashMap &flashMap,
    RuntimeImage &runtime,
    PatchReport &report)
{
    // Preserve Nintendo FLASH1M_V102 control flow. The stock
    // public wrappers implement bank switching, retries, partial-length
    // semantics, setup-profile installation, and return conventions.  Only
    // replace the physical primitives underneath those wrappers.
    const std::map<FlashRoutineRole, std::string> replacements = {
        {FlashRoutineRole::SwitchBank, "gbashSwitchFlashBank"},
        {FlashRoutineRole::ReadId, "gbashReadFlashId"},
        {FlashRoutineRole::ReadCore, "gbashReadFlashCore"},
        {FlashRoutineRole::VerifyCore, "gbashVerifyFlashCore"},
        {FlashRoutineRole::EraseChip, "gbashEraseFlashChip"},
        {FlashRoutineRole::EraseSector, "gbashEraseFlashSector"},
        {FlashRoutineRole::ProgramSector, "gbashProgramFlashSector"},
    };

    for (const auto &[role, symbol] : replacements) {
        const auto &function = flashMap.function(role);
        if (function.size < 8u)
            throw std::runtime_error("FLASH1M primitive too small for bridge: " + function.name);
        const auto target = runtimeAddress(runtime, symbol);
        writeThumbTailJump(rom, function.offset, target);
        report.routines.push_back({function.name, function.offset, target});
    }
}

const char *runtimeSymbolForPrimitive(SavePrimitiveRole role)
{
    switch (role) {
    case SavePrimitiveRole::FlashSwitchBank: return "gbashSwitchFlashBank";
    case SavePrimitiveRole::FlashReadId: return "gbashReadFlashId";
    case SavePrimitiveRole::FlashReadCore: return "gbashReadFlashCore";
    case SavePrimitiveRole::FlashVerifyCore: return "gbashVerifyFlashCore";
    case SavePrimitiveRole::FlashEraseChip: return "gbashEraseFlashChip";
    case SavePrimitiveRole::FlashEraseSector: return "gbashEraseFlashSector";
    case SavePrimitiveRole::FlashProgramSector: return "gbashProgramFlashSector";
    case SavePrimitiveRole::EepromReadDword: return "gbashReadEepromDword";
    case SavePrimitiveRole::EepromProgramDword: return "gbashProgramEepromDword";
    case SavePrimitiveRole::SramReadCore: return "gbashSramReadMirror";
    case SavePrimitiveRole::SramWrite: return "gbashSramWriteMirror";
    case SavePrimitiveRole::SramVerifyCore: return "gbashSramVerifyMirror";
    }
    throw std::runtime_error("unmapped save primitive role");
}

void writeThumbReturnZero(RomImage &rom, std::size_t offset)
{
    const std::array<std::uint8_t, 4> stub = {0x00u, 0x20u, 0x70u, 0x47u}; // movs r0,#0; bx lr
    rom.write(offset, stub.data(), stub.size());
}

const char *runtimeSymbolForAnalyzedFlashOp(std::uint8_t kind)
{
    switch (kind) {
    case SFW_OP_FLASH_READ: return "gbashReadFlash";
    case SFW_OP_FLASH_ERASE_CHIP: return "gbashEraseFlashChip";
    case SFW_OP_FLASH_ERASE_SECTOR: return "gbashEraseFlashSector";
    case SFW_OP_FLASH_WRITE_SECTOR: return "gbashProgramFlashSector";
    // The SuperFW-derived public ProgramFlashByte ABI is (sector, offset,
    // value), including FLASH1M setup records.  Keep this separate from the
    // Nintendo low-level pointer-form primitive.
    case SFW_OP_FLASH_WRITE_BYTE: return "gbashProgramFlashByteByOffset";
    case SFW_OP_FLASH_IDENT: return "gbashReadFlashId";
    case SFW_OP_FLASH_SWITCH_BANK: return "gbashSwitchFlashBank";
    default: return nullptr;
    }
}

void patchAnalyzedFlashApi(
    RomImage &rom,
    const RomImage &originalRom,
    SaveType expectedType,
    RuntimeImage &runtime,
    PatchReport &report)
{
    const SfwSavePlan plan = SaveMemoryPatcher{}.analyze(originalRom);
    const std::uint8_t expected = expectedType == SaveType::Flash1M
        ? SFW_SAVE_FLASH1024K : SFW_SAVE_FLASH512K;
    if (plan.save_type != expected)
        throw std::runtime_error(
            "normalized save-plan type disagrees with the validated FLASH library scan");

    std::size_t bridged = 0u;
    for (std::size_t index = 0u; index < plan.op_count; ++index) {
        const auto &op = plan.op[index];
        if (op.offset >= originalRom.size())
            throw std::runtime_error("analyzed FLASH operation points outside the source ROM");

        if (op.kind == SFW_OP_RAW_BYTES) {
            if (op.raw_len > SFW_MAX_RAW_BYTES || op.raw_len > originalRom.size() - op.offset)
                throw std::runtime_error("analyzed raw save operation exceeds source ROM bounds");
            rom.write(op.offset, op.raw, op.raw_len);
            report.routines.push_back({"Analyzed raw save compatibility op", op.offset, 0u});
            continue;
        }
        if (op.kind == SFW_OP_FLASH_VERIFY || op.kind == SFW_OP_RAW_THUMB_RET0) {
            if (4u > originalRom.size() - op.offset)
                throw std::runtime_error("analyzed verify/return operation is too close to ROM end");
            // The backing runtime verifies new snapshot data before commit, so
            // Nintendo's follow-up FLASH verify wrapper is redundant.  This is
            // the same public-ABI policy used by the FRAM and compact-M36
            // composers and avoids guessing between VerifySector/NBytes ABIs.
            writeThumbReturnZero(rom, op.offset);
            report.routines.push_back({"Analyzed FLASH verify -> verified-write success", op.offset, 0u});
            ++bridged;
            continue;
        }

        const char *symbol = runtimeSymbolForAnalyzedFlashOp(op.kind);
        if (!symbol)
            continue;
        if (8u > originalRom.size() - op.offset)
            throw std::runtime_error("analyzed FLASH entry is too small for a Thumb bridge at ROM boundary");
        const auto target = runtimeAddress(runtime, symbol);
        writeThumbTailJump(rom, op.offset, target);
        report.routines.push_back({std::string("Analyzed public FLASH API -> ") + symbol, op.offset, target});
        ++bridged;
    }

    if (bridged == 0u)
        throw std::runtime_error("normalized FLASH save plan contained no patchable public operations");
}

void patchValidatedPrimitives(
    RomImage &rom,
    const SaveLibraryMatch &saveLibrary,
    RuntimeImage &runtime,
    PatchReport &report)
{
    for (const auto &hook : saveLibrary.primitiveHooks) {
        const auto target = runtimeAddress(runtime, runtimeSymbolForPrimitive(hook.role));
        if (hook.overwriteBytes < (hook.instructionSet == PatchInstructionSet::Arm ? 12u : 8u))
            throw std::runtime_error("save primitive is too small for bridge: " + hook.name);

        if (hook.instructionSet == PatchInstructionSet::Arm)
            writeArmTailJump(rom, hook.entryOffset, target);
        else
            writeThumbTailJump(rom, hook.entryOffset, target);
        report.routines.push_back({hook.name, hook.entryOffset, target});
    }
}


void configureSramStorage(
    const RomImage &originalRom,
    const RomHoleCatalog &holes,
    std::size_t minimumTailOffset,
    const std::vector<ByteRange> &reservedRanges,
    const PatchOptions &options,
    const SaveLibraryMatch &saveLibrary,
    RuntimeConfig &runtimeConfig,
    PatchReport &report)
{
    runtimeConfig.protocol = static_cast<std::uint16_t>(RuntimeProtocol::Sram);
    const auto &backend = norBackendDescriptor(options.norFlashType);
    const std::size_t eraseBlockBytes = backend.eraseBlockBytes;

    // Storage preference is safety-first and geometry-explicit:
    //   1) two complete shared-analyzer FF erase units -> reusable A/B;
    //   2) aggregate SAFE_CODE+FF fragments -> erase-free append journal;
    //   3) appended tail erase units -> reusable A/B without touching game data.
    // The journal exists specifically for ROMs such as MMZ where fragmented
    // safe FF capacity is real but pretending it is two erase blocks is not.
    const auto internalAb = backend.supportsBlockErase
        ? tryCreateInternalEraseStorageLayout(
            originalRom, holes, minimumTailOffset, options.romAddressSpaceBytes,
            eraseBlockBytes, 2u, StorageLayoutKind::FixedSramMirror, reservedRanges)
        : std::optional<StorageLayout>{};

    const std::size_t journalRecordBytes =
        (saveLibrary.geometry.totalBytes + kSramJournalHeaderBytes + 3u) & ~std::size_t{3u};
    auto journal = internalAb ? std::optional<StorageLayout>{} : tryCreateProgramOnlyJournalLayout(
        originalRom, holes, minimumTailOffset, options.romAddressSpaceBytes, journalRecordBytes,
        kMinimumSramJournalRecords, kMaxSramJournalSpans, reservedRanges);
    // Erase-less NORs (for example MX26L6420) must never fall through to the
    // A/B erase mirror. If the exact GBABR plan has no sufficient FF fragments,
    // append a bounded one-way journal instead. Existing erase-capable M6/M36
    // placement remains byte-for-byte on its historical path.
    if (!backend.supportsBlockErase && !journal) {
        journal = tryCreateAppendedProgramOnlyJournalLayout(
            originalRom, minimumTailOffset, options.romAddressSpaceBytes, journalRecordBytes,
            kMinimumSramJournalRecords, kPreferredSramJournalRecords, reservedRanges);
    }

    if (internalAb) {
        report.storage = *internalAb;
        runtimeConfig.storageMode = static_cast<std::uint16_t>(RuntimeStorageMode::SramHotkeyMirror);
        runtimeConfig.sramMirrorAddress = report.storage.block(0).gbaAddress();
        runtimeConfig.sramBackupAddress = report.storage.block(1).gbaAddress();
    } else if (journal) {
        report.storage = *journal;
        runtimeConfig.storageMode = static_cast<std::uint16_t>(RuntimeStorageMode::SramProgramOnlyJournal);
        runtimeConfig.sramJournalRecordBytes = static_cast<std::uint32_t>(report.storage.journalRecordBytes);
        runtimeConfig.sramJournalRecordCount = static_cast<std::uint16_t>(report.storage.journalRecordCount);
        runtimeConfig.sramJournalSpanCount = static_cast<std::uint16_t>(report.storage.programOnlySpans.size());
        if (report.storage.programOnlySpans.size() > kMaxSramJournalSpans)
            throw std::runtime_error("SRAM program-only journal exceeds runtime span table");
        for (std::size_t index = 0u; index < report.storage.programOnlySpans.size(); ++index) {
            runtimeConfig.sramJournalSpanAddresses[index] = report.storage.programOnlySpans[index].gbaAddress();
            runtimeConfig.sramJournalSpanLengths[index] =
                static_cast<std::uint32_t>(report.storage.programOnlySpans[index].spanBytes);
        }
        report.sramUsesProgramOnlyJournal = true;
        report.sramJournalRecordCount = runtimeConfig.sramJournalRecordCount;
        report.sramJournalSpanCount = runtimeConfig.sramJournalSpanCount;
    } else {
        if (!backend.supportsBlockErase)
            throw std::runtime_error("selected NOR has no sector erase and no program-only SRAM journal fits");
        report.storage = createStorageLayout(
            originalRom, holes, minimumTailOffset, options.romAddressSpaceBytes,
            eraseBlockBytes, 2u, StorageLayoutKind::FixedSramMirror,
            false, reservedRanges, false);
        runtimeConfig.storageMode = static_cast<std::uint16_t>(RuntimeStorageMode::SramHotkeyMirror);
        runtimeConfig.sramMirrorAddress = report.storage.block(0).gbaAddress();
        runtimeConfig.sramBackupAddress = report.storage.block(1).gbaAddress();
    }

    runtimeConfig.physicalBlockBytes = report.sramUsesProgramOnlyJournal
        ? 0u
        : static_cast<std::uint32_t>(eraseBlockBytes);

    if (saveLibrary.sramIrqDispatcherOffset == static_cast<std::size_t>(-1) ||
        saveLibrary.sramIrqVectorLiteralOffsets.empty())
        throw std::runtime_error("SRAM hotkey requires the standard Nintendo IRQ-vector install path");

    runtimeConfig.reservedSramConfig = 0u;

    // RAM ownership is independent of ROM/NOR storage placement. A persistent
    // block being appended, internal, FF, or database-backed says nothing about
    // whether an EWRAM address is safe. Prefer a structurally proven game-owned
    // save image for every SRAM ROM. The historical private shadow is retained
    // only as a compatibility fallback after a separate ROM-wide RAM audit.
    const bool hasProvenGameOwnedMirror =
        saveLibrary.sramRamMirrorBase >= 0x02000000u &&
        saveLibrary.sramRamMirrorBase <= 0x02040000u - saveLibrary.geometry.totalBytes;

    if (hasProvenGameOwnedMirror) {
        if (saveLibrary.sramRuntimeStateAddress == 0u)
            throw std::runtime_error("SRAM game-owned mirror is proven but no collision-free runtime state slot is proven");
        runtimeConfig.sramShadowAddress = saveLibrary.sramRamMirrorBase;
        runtimeConfig.sramRuntimeStateAddress = saveLibrary.sramRuntimeStateAddress;
        runtimeConfig.reservedSramConfig = kSramConfigGameOwnedShadowSnapshot;
        runtimeConfig.sramRefreshTriggerOffset = saveLibrary.sramPreloadRefreshTriggerOffset;
        // Full-read forced reload is a separate, opt-in lifecycle capability.
        // Today it is enabled only when the structural preload detector proved
        // a reset/self-test boundary.  A mirror proof by itself must never turn
        // ordinary full SRAM reads into NOR restores.
        if (runtimeConfig.sramRefreshTriggerOffset != 0xFFFFFFFFu)
            runtimeConfig.reservedSramConfig |= kSramConfigFullReadRefresh;
        report.sramUsesEraseRebuild = false;
        report.sramUsesGameOwnedShadowSnapshot = true;
        report.sramRefreshTriggerOffset = runtimeConfig.sramRefreshTriggerOffset;
    } else if (saveLibrary.sramPrivateShadowCompatible) {
        runtimeConfig.sramShadowAddress = kSramShadowAddress;
        runtimeConfig.sramRuntimeStateAddress = kLegacySramStateAddress;
        report.sramUsesEraseRebuild = true;
        report.sramUsesGameOwnedShadowSnapshot = false;
    } else {
        throw std::runtime_error(
            "SRAM RAM workspace is not proven safe: no unique game-owned mirror and legacy private EWRAM shadow overlaps/aliases game RAM");
    }

    runtimeConfig.sramBootResumeAddress = decodeArmBranchTarget(originalRom.u32(0u), 0u);
    runtimeConfig.sramSizeBytes = static_cast<std::uint32_t>(saveLibrary.geometry.totalBytes);
    runtimeConfig.sramHotkeyRaw = kSramHotkeyRaw;
    runtimeConfig.sramIrqDispatcherAddress =
        kGbaRomBase + static_cast<std::uint32_t>(saveLibrary.sramIrqDispatcherOffset);
    report.sramHotkeyOnly = true;
    report.sramShadowAddress = runtimeConfig.sramShadowAddress;
    report.sramRuntimeStateAddress = runtimeConfig.sramRuntimeStateAddress;
    report.sramIrqEntryAddress = runtimeConfig.sramIrqDispatcherAddress;
}

void configureEepromStorage(
    const RomImage &originalRom,
    const RomHoleCatalog &holes,
    std::size_t minimumTailOffset,
    const std::vector<ByteRange> &reservedRanges,
    const PatchOptions &options,
    RuntimeConfig &runtimeConfig,
    PatchReport &report)
{
    runtimeConfig.protocol = static_cast<std::uint16_t>(RuntimeProtocol::Eeprom);
    const auto &backend = norBackendDescriptor(options.norFlashType);
    const bool requiresProgramOnlyFallback =
        runtimeConfig.logicalSaveSizeBytes > 512u &&
        options.storagePolicy == StoragePolicy::CompactV2 &&
        !backend.supportsBlockErase;

    // EEPROM512 is already physically minimal in SAFE_V1: one 64 KiB
    // program-only record block. Erase-less NORs automatically use the same
    // record-lane engine for EEPROM8K instead of pretending COMPACT_V2 GC can
    // erase a sector.
    if (runtimeConfig.logicalSaveSizeBytes <= 512u ||
        options.storagePolicy == StoragePolicy::SafeV1 || requiresProgramOnlyFallback) {
        const std::size_t unitCount = runtimeConfig.logicalUnitCount;
        report.storage = createStorageLayout(
            originalRom, holes, minimumTailOffset, options.romAddressSpaceBytes,
            options.programStorageBlockBytes, unitCount,
            StorageLayoutKind::EepromRecordLanes, false, reservedRanges);
        runtimeConfig.physicalBlockBytes = static_cast<std::uint32_t>(options.programStorageBlockBytes);
        runtimeConfig.storageMode = static_cast<std::uint16_t>(RuntimeStorageMode::EepromRecordLanes);
        runtimeConfig.physicalStorageBlockCount = static_cast<std::uint16_t>(report.storage.blocks.size());
        if (report.storage.blocks.size() != unitCount)
            throw std::runtime_error("EEPROM storage block count does not match logical 512-byte units");
        for (std::size_t index = 0; index < report.storage.blocks.size(); ++index)
            runtimeConfig.storageBlockAddresses[index] = report.storage.block(index).gbaAddress();
        report.eepromRecordsPerDword = 64u;
        report.storagePolicyFallbackProgramOnly = requiresProgramOnlyFallback;
        return;
    }

    // COMPACT_V2 for EEPROM8K: two complete A/B erase domains. Each dword has
    // a small fixed lane of commit-last records, giving bounded reads while
    // reducing persistent space from sixteen 64 KiB lanes to two actual NOR
    // erase blocks. GC reconstructs the latest complete logical image into the
    // inactive block and publishes the destination header last. No cartridge
    // SRAM/FRAM is assumed.
    const std::size_t eraseBlockBytes = backend.eraseBlockBytes;
    constexpr std::size_t kCompactBlockCount = 2u;
    constexpr std::size_t kCompactRecordBytes = 10u;
    constexpr std::size_t kCompactMetadataReserve = 1024u;
    const std::size_t dwordCount = runtimeConfig.logicalSaveSizeBytes / 8u;
    if (dwordCount == 0u || eraseBlockBytes <= kCompactMetadataReserve)
        throw std::runtime_error("EEPROM compact geometry is invalid");
    const std::size_t recordsPerDword =
        (eraseBlockBytes - kCompactMetadataReserve) / (dwordCount * kCompactRecordBytes);
    if (recordsPerDword < 2u || recordsPerDword > 0xFFFFu)
        throw std::runtime_error("EEPROM compact erase block does not provide enough per-dword versions");

    report.storage = createStorageLayout(
        originalRom, holes, minimumTailOffset, options.romAddressSpaceBytes,
        eraseBlockBytes, kCompactBlockCount,
        StorageLayoutKind::EepromCompactAB, true, reservedRanges, true);
    runtimeConfig.physicalBlockBytes = static_cast<std::uint32_t>(eraseBlockBytes);
    runtimeConfig.storageMode = static_cast<std::uint16_t>(RuntimeStorageMode::EepromCompactAB);
    // Reuse the generic per-block version-count field for compact EEPROM lane
    // depth; FLASH-only code never reads it while protocol==EEPROM. This keeps
    // RuntimeConfig stable and avoids runtime integer division on ARM7TDMI.
    runtimeConfig.flashSlotsPerBlock = static_cast<std::uint16_t>(recordsPerDword);
    runtimeConfig.physicalStorageBlockCount = static_cast<std::uint16_t>(report.storage.blocks.size());
    if (report.storage.blocks.size() != kCompactBlockCount)
        throw std::runtime_error("EEPROM compact storage requires exactly two physical erase blocks");
    for (std::size_t index = 0; index < report.storage.blocks.size(); ++index)
        runtimeConfig.storageBlockAddresses[index] = report.storage.block(index).gbaAddress();
    report.eepromRecordsPerDword = static_cast<std::uint16_t>(recordsPerDword);
}

std::size_t compactFlashProgramBlockBytes(std::size_t logicalUnitBytes)
{
    // FLASH journal blocks are program-only arenas; they are not NOR erase
    // units.  CompactV2 therefore sizes them from the save record itself, not
    // from a particular cartridge geometry.  Three primary generations leave
    // useful power-loss-safe headroom while keeping 128 KiB FLASH1M practical
    // in end-of-image padding on full-capacity carts.
    constexpr std::size_t kPrimaryGenerations = 3u;
    constexpr std::size_t kMetadataAndBreadcrumbReserve = 0x100u;
    if (logicalUnitBytes == 0u ||
        logicalUnitBytes > (static_cast<std::size_t>(-1) - kMetadataAndBreadcrumbReserve) / kPrimaryGenerations)
        throw std::runtime_error("invalid FLASH logical-unit size for compact journal");
    const std::size_t required = logicalUnitBytes * kPrimaryGenerations + kMetadataAndBreadcrumbReserve;
    std::size_t block = 1u;
    while (block < required) {
        if (block > static_cast<std::size_t>(-1) / 2u)
            throw std::runtime_error("FLASH compact journal size overflow");
        block <<= 1u;
    }
    return block;
}

void configureFlashStorage(
    const RomImage &originalRom,
    const RomHoleCatalog &holes,
    std::size_t minimumTailOffset,
    const std::vector<ByteRange> &reservedRanges,
    const PatchOptions &options,
    RuntimeConfig &runtimeConfig,
    PatchReport &report)
{
    runtimeConfig.protocol = static_cast<std::uint16_t>(RuntimeProtocol::Flash);
    const std::size_t unitCount = runtimeConfig.logicalUnitCount;
    const std::size_t programBlockBytes = options.storagePolicy == StoragePolicy::CompactV2
        ? compactFlashProgramBlockBytes(runtimeConfig.logicalUnitSizeBytes)
        : options.programStorageBlockBytes;
    report.storage = createStorageLayout(
        originalRom,
        holes,
        minimumTailOffset,
        options.romAddressSpaceBytes,
        programBlockBytes,
        unitCount + options.flashSpillBlockCount,
        StorageLayoutKind::FlashVersionedSlots,
        true,
        reservedRanges,
        true);
    runtimeConfig.physicalBlockBytes = static_cast<std::uint32_t>(options.programStorageBlockBytes);
    configureVersionedSlots(runtimeConfig, report.storage);
    report.flashVersionsPerSector = runtimeConfig.flashSlotsPerBlock;
    report.flashSpillBlockCount = static_cast<std::uint16_t>(report.storage.blocks.size() - unitCount);
}

void configureStorage(
    const RomImage &originalRom,
    const RomHoleCatalog &holes,
    std::size_t minimumTailOffset,
    const std::vector<ByteRange> &reservedRanges,
    const PatchOptions &options,
    const SaveLibraryMatch &saveLibrary,
    RuntimeConfig &runtimeConfig,
    PatchReport &report)
{
    switch (saveLibrary.type) {
    case SaveType::Sram:
        configureSramStorage(originalRom, holes, minimumTailOffset, reservedRanges, options, saveLibrary, runtimeConfig, report);
        return;
    case SaveType::Eeprom512:
    case SaveType::Eeprom8K:
        configureEepromStorage(originalRom, holes, minimumTailOffset, reservedRanges, options, runtimeConfig, report);
        return;
    case SaveType::Flash512:
    case SaveType::Flash1M:
        configureFlashStorage(originalRom, holes, minimumTailOffset, reservedRanges, options, runtimeConfig, report);
        return;
    default:
        throw std::runtime_error("unsupported save type while configuring storage");
    }
}

FlashMap configureFlash1mStockDriver(
    const RomImage &originalRom,
    std::uint16_t forcedFlashId,
    RuntimeConfig &runtimeConfig)
{
    FlashMap flashMap = FlashScanner{}.scan(originalRom);
    const auto &forcedSetup = selectForcedSetupProfile(flashMap, forcedFlashId);
    runtimeConfig.forcedSetupProfileAddress = kGbaRomBase + static_cast<std::uint32_t>(forcedSetup.offset);
    runtimeConfig.globalWaitForWritePointer = kGlobalWaitForWrite;
    runtimeConfig.globalProgramSectorPointer = kGlobalProgramSector;
    runtimeConfig.globalGeometryPointer = kGlobalGeometry;
    runtimeConfig.globalEraseChipPointer = kGlobalEraseChip;
    runtimeConfig.globalEraseSectorPointer = kGlobalEraseSector;
    runtimeConfig.globalMaxTimePointer = kGlobalMaxTime;
    return flashMap;
}


void patchSramBootAndIrqVector(
    RomImage &rom,
    const RomImage &originalRom,
    const SaveLibraryMatch &saveLibrary,
    const RuntimeImage &runtime,
    PatchReport &report)
{
    if (saveLibrary.type != SaveType::Sram)
        return;

    const std::uint32_t bootTarget = runtimeAddress(runtime, "gbashSramBootEntry");
    if ((bootTarget & 3u) != 0u)
        throw std::runtime_error("SRAM boot entry must be ARM aligned");
    // Preserve the original target in RuntimeConfig; only the reset branch is replaced.
    (void)decodeArmBranchTarget(originalRom.u32(0u), 0u);
    writeArmBranch(rom, 0u, bootTarget);
    report.routines.push_back({"SRAM Hotkey Bootstrap", 0u, bootTarget});

    // Game-owned SRAM uses an analyzer-proven EWRAM state slot. Redirect every
    // instruction-proven game write of 03007FFC to state.chainTarget (+12),
    // while the BIOS vector remains our transparent interposer. This preserves
    // dynamic Nintendo IRQ target changes (the v0.16 hardware-proven behavior)
    // without using BIOS-reserved 03007FF4. Legacy/private-shadow SRAM retains
    // its lazy vector ownership path unchanged.
    for (const auto literalOffset : saveLibrary.sramIrqVectorLiteralOffsets) {
        if (originalRom.u32(literalOffset) != 0x03007FFCu)
            throw std::runtime_error("SRAM IRQ-vector literal changed before patch");
        if (saveLibrary.sramRuntimeStateAddress != 0u && saveLibrary.sramRamMirrorBase != 0u)
            rom.write32(literalOffset, saveLibrary.sramRuntimeStateAddress + 12u);
    }
    report.routines.push_back({
        saveLibrary.sramRuntimeStateAddress != 0u && saveLibrary.sramRamMirrorBase != 0u
            ? "SRAM IRQ Dynamic Chain Interposer"
            : "SRAM IRQ Lazy Interposer",
        saveLibrary.sramIrqVectorLiteralOffsets.front(),
        runtimeAddress(runtime, "gbashSramHotkeyIrqEntry")});
}

void rewriteDirectSramLiterals(
    RomImage &rom,
    const RomImage &originalRom,
    const SaveLibraryMatch &saveLibrary,
    const RuntimeConfig &runtimeConfig,
    PatchReport &report)
{
    if (saveLibrary.type != SaveType::Sram)
        return;

    for (const auto literalOffset : saveLibrary.directSramLiteralOffsets) {
        const std::uint32_t originalAddress = originalRom.u32(literalOffset);
        const std::uint32_t relativeOffset = originalAddress - runtimeConfig.originalSramBaseAddress;
        if (relativeOffset >= runtimeConfig.sramSizeBytes)
            throw std::runtime_error("direct SRAM literal points beyond configured mirror");
        rom.write32(literalOffset, runtimeConfig.sramShadowAddress + relativeOffset);
        report.rewrittenSramLiterals.push_back(literalOffset);
    }
}

} // namespace

const char *toString(PatchStrategy strategy)
{
    switch (strategy) {
    case PatchStrategy::SharedPrimitiveRuntime: return "SHARED_PRIMITIVE_RUNTIME";
    case PatchStrategy::ExactDirectRww: return "EXACT_DIRECT_RWW";
    case PatchStrategy::AnalyzedDirectRww: return "ANALYZED_DIRECT_RWW";
    }
    return "UNKNOWN";
}

PatchOptions makePatchOptionsForNorProfile(const NorTargetProfileDescriptor &target)
{
    PatchOptions options;
    options.norFlashType = target.backendType;
    options.romAddressSpaceBytes = target.capacityBytes;
    options.targetProfileKey = target.key;
    options.targetProfileDisplayName = target.displayName;
    return options;
}

PatchReport SavePatcher::patchToTargetStorage(
    RomImage &rom,
    const SaveLibraryMatch &saveLibrary,
    const PatchOptions &options) const
{
    if (saveLibrary.type == SaveType::Unknown)
        throw std::runtime_error("save library has unknown type");

    // Backend routing is capability/protocol driven, never title driven. A backend
    // may provide a separately hardware-qualified direct protocol engine for
    // save-library families whose ABI cannot be expressed safely by the shared
    // primitive runtime. Exact GBABR structure remains the ABI oracle.
    const auto &backend = norBackendDescriptor(options.norFlashType);
    PatchOptions effectiveOptions = options;
    effectiveOptions.romAddressSpaceBytes = std::min(
        effectiveOptions.romAddressSpaceBytes, backend.romAddressSpaceLimit);
    if (rom.size() > effectiveOptions.romAddressSpaceBytes) {
        const std::string targetKey = effectiveOptions.targetProfileKey.empty()
            ? std::string(backend.profileKey) : effectiveOptions.targetProfileKey;
        throw std::runtime_error(
            "target capacity is too small: source ROM is " + std::to_string(rom.size()) +
            " bytes, while target '" + targetKey + "' can address only " +
            std::to_string(effectiveOptions.romAddressSpaceBytes) +
            " bytes. This is a physical target-size limit, not a save-analyzer failure. "
            "Next options: choose a target that exposes at least the source ROM size, or use a different "
            "cart whose mapping provides that ROM capacity. libgbasave will not truncate or silently shrink the ROM.");
    }

    if (saveLibrary.requiresDirectSramProtocolEngine && !backend.supportsDirectProtocolEngine)
        throw std::runtime_error(
            "the analyzed save ABI uses the fast SRAM calling convention and therefore requires "
            "a target with the qualified direct-SRAM protocol engine. The selected target does "
            "not provide that capability; the ROM was not modified.");

    // A direct engine is a target capability, not a requirement that every ROM
    // appear in the exact-plan database. Prefer the hardware-proven exact route
    // when present, then let structurally analyzed protocols use a qualified
    // generic direct composer where one exists.
    const bool hasQualifiedExactDirectPlan =
        backend.supportsDirectProtocolEngine && canUseM36HardwareProvenRoute(rom, saveLibrary);
    if (hasQualifiedExactDirectPlan &&
        (saveLibrary.type == SaveType::Sram ||
         saveLibrary.type == SaveType::Flash512 ||
         saveLibrary.type == SaveType::Flash1M))
        return patchM36HardwareProvenRoute(rom, saveLibrary, effectiveOptions);

    if (backend.supportsDirectProtocolEngine &&
        (saveLibrary.type == SaveType::Flash512 || saveLibrary.type == SaveType::Flash1M))
        return patchM36AnalyzedFlashRoute(rom, saveLibrary, effectiveOptions);

    if (saveLibrary.requiresDirectSramProtocolEngine && !hasQualifiedExactDirectPlan)
        throw std::runtime_error(
            "the analyzed SRAM_F fast ABI requires the target's direct protocol engine, but no "
            "complete structurally compatible direct patch plan was proven for this ROM. Generic "
            "fallback is disabled for this ABI because silently using the normal SRAM calling "
            "convention can corrupt saves; the ROM was not modified.");

    const RomImage originalRom = rom;
    const RomHoleCatalog holes = discoverRomHoles(originalRom);
    RuntimeImage runtime = loadEmbeddedRuntimeImage(options.norFlashType);
    const std::size_t runtimePlacementLimit = std::min(
        effectiveOptions.romAddressSpaceBytes, backend.mutableMainArrayEnd);
    const RuntimePlacement runtimePlacement = createRuntimePlacement(
        originalRom, holes, runtime.bytes.size(), runtimePlacementLimit, 4u,
        backend.runtimeExecutionBankBytes);
    if (runtimePlacement.end() > backend.mutableMainArrayEnd)
        throw std::runtime_error("runtime enters backend-reserved non-mutable NOR area");
    relocateRuntimeImage(runtime, runtimePlacement.gbaAddress());

    PatchReport report;
    report.saveType = saveLibrary.type;
    report.strategy = PatchStrategy::SharedPrimitiveRuntime;
    report.runtimeOffset = runtimePlacement.offset;
    report.runtimeSize = runtime.bytes.size();
    report.runtimePlacementSource = runtimePlacement.source;
    report.runtimeHoleSource = runtimePlacement.holeSource;
    report.gbabrDatabaseVersion = holes.gbabrDatabaseVersion;
    report.gbabrDatabaseMatched = holes.gbabrDatabaseMatched;
    report.gbabrErasedHoleCount = holes.databaseErasedHoles.size();
    report.forcedFlashId = effectiveFlashId(saveLibrary, effectiveOptions);

    RuntimeConfig runtimeConfig = makeBaseConfig(saveLibrary, effectiveOptions);
    std::vector<ByteRange> reservedRanges;
    if (backend.runtimeExecutionBankBytes != 0u) {
        const std::size_t bankMask = backend.runtimeExecutionBankBytes - 1u;
        const std::size_t bankStart = runtimePlacement.offset & ~bankMask;
        if (runtimePlacement.end() > bankStart + backend.runtimeExecutionBankBytes)
            throw std::runtime_error("runtime crosses its backend RWW execution bank");
        reservedRanges.push_back({bankStart, backend.runtimeExecutionBankBytes});
    } else {
        reservedRanges.push_back({runtimePlacement.offset, runtimePlacement.size});
    }
    if (backend.mutableMainArrayEnd < backend.romAddressSpaceLimit)
        reservedRanges.push_back({
            backend.mutableMainArrayEnd,
            backend.romAddressSpaceLimit - backend.mutableMainArrayEnd});
    for (std::size_t index = 0u; index < backend.storageForbiddenRangeCount; ++index) {
        const auto &range = backend.storageForbiddenRanges[index];
        if (range.bytes != 0u)
            reservedRanges.push_back({range.offset, range.bytes});
    }
    const std::size_t minimumTailOffset = runtimePlacement.source == RuntimePlacementSource::AppendedTail
        ? runtimePlacement.end()
        : originalRom.size();

    configureStorage(
        originalRom,
        holes,
        minimumTailOffset,
        reservedRanges,
        effectiveOptions,
        saveLibrary,
        runtimeConfig,
        report);

    FlashMap flash1mMap;
    const bool flash1mPlanPrimitives =
        saveLibrary.type == SaveType::Flash1M && !saveLibrary.primitiveHooks.empty();
    bool useAnalyzedPublicFlashApi = false;
    if (saveLibrary.type == SaveType::Flash1M && !flash1mPlanPrimitives) {
        try {
            flash1mMap = configureFlash1mStockDriver(originalRom, report.forcedFlashId, runtimeConfig);
        } catch (const std::runtime_error &) {
            // A complete normalized public save plan is an independent proof
            // path used by the FRAM/direct composers.  If primitive discovery
            // is ambiguous (common in heavily rebuilt ROM hacks), bridge those
            // proven public APIs to the same target-generic runtime instead of
            // requiring a ROM identity exception.
            const SfwSavePlan plan = SaveMemoryPatcher{}.analyze(originalRom);
            if (plan.save_type != SFW_SAVE_FLASH1024K)
                throw;
            useAnalyzedPublicFlashApi = true;
        }
    }

    patchRuntimeConfig(runtime, runtimeConfig);
    if (saveLibrary.type == SaveType::Sram &&
        (runtimeConfig.reservedSramConfig & kSramConfigGameOwnedShadowSnapshot) != 0u) {
        if (runtimeConfig.sramRuntimeStateAddress == 0u)
            throw std::runtime_error("game-owned SRAM IRQ route lacks a guarded chain-state slot");
        patchRuntimeSymbolWord(runtime, "gbashSramChainSlotAddressWord", runtimeConfig.sramRuntimeStateAddress + 12u);
        patchRuntimeSymbolWord(runtime, "gbashSramFallbackDispatcherWord", runtimeConfig.sramIrqDispatcherAddress);
    }
    report.storage.outputSizeBytes = std::max(report.storage.outputSizeBytes, runtimePlacement.end());
    const auto storageImage = initialiseStorageImage(rom, report.storage, effectiveOptions.storagePreserveImage);
    report.storagePreserveApplied = storageImage.exactPreservationApplied;
    report.storagePreserveMode = storageImage.preservationMode;
    rom.write(runtimePlacement.offset, runtime.bytes.data(), runtime.bytes.size());

    if (saveLibrary.type == SaveType::Flash1M && useAnalyzedPublicFlashApi)
        patchAnalyzedFlashApi(rom, originalRom, saveLibrary.type, runtime, report);
    else if (saveLibrary.type == SaveType::Flash1M && !flash1mPlanPrimitives)
        patchFlash1m(rom, flash1mMap, runtime, report);
    else
        patchValidatedPrimitives(rom, saveLibrary, runtime, report);

    patchSramBootAndIrqVector(rom, originalRom, saveLibrary, runtime, report);
    rewriteDirectSramLiterals(rom, originalRom, saveLibrary, runtimeConfig, report);
    return report;
}

} // namespace gbasave
