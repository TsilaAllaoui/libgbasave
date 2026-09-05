#pragma once

#include <cstddef>
#include <cstdint>

namespace gbasave {

inline constexpr std::size_t kMaxRuntimeLogicalUnits = 64u;
inline constexpr std::size_t kMaxSramJournalSpans = 16u;

enum class RuntimeProtocol : std::uint16_t {
    Flash = 1,
    Eeprom = 2,
    Sram = 3,
};

enum class RuntimeStorageMode : std::uint16_t {
    FlashVersionedSlots = 1,
    SramHotkeyMirror = 2,
    EepromRecordLanes = 3,
    SramProgramOnlyJournal = 4,
    EepromCompactAB = 5,
};

#pragma pack(push, 1)
struct RuntimeConfig {
    std::uint32_t magic;
    std::uint32_t version;

    std::uint16_t protocol;
    std::uint16_t storageMode;
    std::uint16_t norFlashType;
    std::uint16_t forcedFlashId;
    // Target NOR program engine selector/opcode. This is a hardware backend
    // capability (M6/M6M=0x40 word program; M36=0xE8 proven buffered path),
    // never a game/title property.
    std::uint16_t norProgramCommand;
    std::uint16_t reservedNorConfig;

    std::uint32_t logicalSaveSizeBytes;
    std::uint32_t logicalBankSizeBytes;
    std::uint32_t logicalUnitSizeBytes;
    std::uint32_t physicalBlockBytes;

    std::uint16_t logicalUnitCount;
    std::uint16_t logicalUnitShift;
    std::uint16_t logicalUnitOffsetMask;
    std::uint16_t unitsPerBank;
    std::uint16_t bankCount;
    std::uint16_t bankSelectShift;
    std::uint16_t flashSlotsPerBlock;
    std::uint16_t physicalStorageBlockCount;
    std::uint32_t flashSlotMetadataOffsetBytes;

    std::uint32_t forcedSetupProfileAddress;
    std::uint32_t currentBankStateAddress;

    std::uint32_t globalWaitForWritePointer;
    std::uint32_t globalProgramSectorPointer;
    std::uint32_t globalGeometryPointer;
    std::uint32_t globalEraseChipPointer;
    std::uint32_t globalEraseSectorPointer;
    std::uint32_t globalMaxTimePointer;

    // SRAM hotkey backend. The game sees a RAM shadow during play. Only the
    // hotkey commits that shadow to the persistent NOR mirror.
    std::uint32_t sramMirrorAddress;
    std::uint32_t sramBackupAddress;
    std::uint32_t sramShadowAddress;
    std::uint32_t sramBootResumeAddress;
    std::uint32_t sramSizeBytes;
    std::uint32_t originalSramBaseAddress;
    std::uint16_t sramHotkeyRaw;
    std::uint16_t reservedSramConfig;

    // Optional owned-shadow refresh trigger. 0xFFFFFFFF disables it.
    // When enabled, a write to this SRAM-relative offset is a structurally
    // identified pre-load self-test boundary: restore the newest persistent
    // snapshot before applying the game's test write.
    std::uint32_t sramRefreshTriggerOffset;

    // Structurally validated Nintendo ARM IRQ dispatcher. SRAM hotkey IRQ
    // interposers chain directly to this ROM address; never persist runtime
    // state in the BIOS-reserved 0x03007Fxx workspace.
    std::uint32_t sramIrqDispatcherAddress;

    // Analyzer-proven mutable EWRAM state used by the SRAM IRQ interposer.
    // +0 magic, +4 initialized, +8 hotkey latch/reserved, +12 dynamic game
    // IRQ chain target. Never points into BIOS-reserved 0x03007Fxx memory.
    std::uint32_t sramRuntimeStateAddress;

    // Erase-free SRAM journal fallback. The logical journal arena is the
    // concatenation of structurally SAFE_CODE+FF spans. Runtime commits only
    // 1->0 bits and never erases sectors in this mode.
    std::uint32_t sramJournalRecordBytes;
    std::uint16_t sramJournalSpanCount;
    std::uint16_t sramJournalRecordCount;
    std::uint32_t sramJournalSpanAddresses[kMaxSramJournalSpans];
    std::uint32_t sramJournalSpanLengths[kMaxSramJournalSpans];

    // One physical storage block per logical unit for VersionedSlots.
    std::uint32_t storageBlockAddresses[kMaxRuntimeLogicalUnits];
};
#pragma pack(pop)

constexpr std::uint32_t kRuntimeConfigMagic = 0x48565347u; // "GSVH"
constexpr std::uint32_t kRuntimeConfigVersion = 14u;

// SRAM retention modes. The established appended-tail path leaves this field
// zero and keeps its proven private EWRAM shadow/worker.
// Full-size/internal-hole SRAM may opt into a structurally proven game-owned
// 32 KiB RAM image plus the collision-free stack-worker A/B snapshot backend.
constexpr std::uint16_t kSramConfigGameOwnedShadowSnapshot = 0x0001u;

// Opt-in only: some structurally qualified SRAM libraries perform an internal
// reset/preload sequence that clears the game-owned save image before a full
// ReadSram transfer.  Only those layouts may force a committed NOR snapshot
// reload on a full-image read. Ordinary SRAM lifecycles must retain live-shadow
// semantics so a title/menu reload cannot overwrite a
// freshly modified EWRAM save with the older committed generation.
constexpr std::uint16_t kSramConfigFullReadRefresh = 0x0002u;

} // namespace gbasave
