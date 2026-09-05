#pragma once

#include "gbasave/rom_image.h"
#include "gbasave/save_types.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace gbasave {

// A hook is only installed at a validated low-level save primitive. Public
// Nintendo wrappers stay stock whenever they contain useful control flow.
enum class SavePrimitiveRole {
    FlashSwitchBank,
    FlashReadId,
    FlashReadCore,
    FlashVerifyCore,
    FlashEraseChip,
    FlashEraseSector,
    FlashProgramSector,
    EepromReadDword,
    EepromProgramDword,
    SramReadCore,
    SramWrite,
    SramVerifyCore,
};

enum class PatchInstructionSet {
    Thumb,
    Arm,
};

struct SavePrimitiveHook {
    SavePrimitiveRole role{};
    std::string name;
    std::size_t signatureOffset{};
    std::size_t entryOffset{};
    std::size_t overwriteBytes{};
    PatchInstructionSet instructionSet{PatchInstructionSet::Thumb};
};

struct SaveLibraryMatch {
    SaveType type{SaveType::Unknown};
    std::string libraryName;
    std::string marker;
    std::size_t markerOffset{};
    SaveGeometry geometry;
    std::vector<SavePrimitiveHook> primitiveHooks;

    // Only literals proven to be loaded by Thumb PC-relative LDR instructions
    // are rewritten. This is intentionally not a generic 0x0E... word sweep.
    std::vector<std::size_t> directSramLiteralOffsets;

    // Nintendo save hotkey chaining keeps the game IRQ dispatcher untouched.
    // Instruction-proven BIOS-vector literals are retained as structural
    // evidence only; v10 never rewrites them to BIOS-reserved scratch RAM.
    // The hotkey hook is installed lazily after the game's first SRAM API use
    // and chains directly to sramIrqDispatcherOffset.
    std::vector<std::size_t> sramIrqVectorLiteralOffsets;
    std::size_t sramIrqDispatcherOffset{static_cast<std::size_t>(-1)};

    // Tiny mutable EWRAM state slot proven not to overlap the game-owned
    // save image or any instruction-decoded RAM literal. Used to preserve
    // dynamic IRQ chaining without the BIOS-reserved 0x03007FF4 slot.
    std::uint32_t sramRuntimeStateAddress{};

    // Optional structurally-inferred game-owned 32 KiB EWRAM SRAM image.
    // This is derived from nearby instruction-proven EWRAM/SRAM literal pairs
    // that agree on the same logical offset, never from a title/game-code rule.
    std::uint32_t sramRamMirrorBase{};
    std::vector<std::uint32_t> sramRamMirrorEvidenceOffsets;
    // Number of logical SRAM bytes covered by the strongest validated
    // SRAM<->EWRAM transfer call that supports sramRamMirrorBase.  Large
    // transfer evidence is stronger than pointer proximity and lets games
    // with one bulk load/store (instead of many literal pairs) prove their
    // own live save image without title-specific rules.
    std::uint32_t sramRamMirrorTransferCoverageBytes{};

    // Legacy/private shadow compatibility is deliberately separate from ROM
    // storage placement.  It is true only when a conservative ROM-wide RAM
    // reference audit finds no evidence that the historical private shadow
    // range is game-owned.  New games should prefer sramRamMirrorBase.
    bool sramPrivateShadowCompatible{};

    // True only for exact-plan SRAM_F layouts whose fast SDK access path requires
    // a backend with a separately hardware-qualified direct SRAM protocol engine.
    // Backends without that capability must refuse rather than guess at the fast ABI.
    bool requiresDirectSramProtocolEngine{};
    // Optional structurally inferred pre-load SRAM self-test write offset.
    // 0xFFFFFFFF means no conservative proof was found.
    std::uint32_t sramPreloadRefreshTriggerOffset{0xFFFFFFFFu};
};

class SaveLibraryScanner {
public:
    SaveLibraryMatch scan(const RomImage &rom, SaveType requested = SaveType::Unknown) const;
    static std::string toText(const RomImage &rom, const SaveLibraryMatch &match);
};

std::string toString(SavePrimitiveRole role);

} // namespace gbasave
