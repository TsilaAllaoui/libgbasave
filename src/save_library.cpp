#include "gbasave/save_library.h"
#include "gbasave/gbabr_plan.h"
#include "gbasave/exact_plan_save_library.h"
#include "gbasave/save_marker.h"
#include "gbasave/save_signature.h"

#include <algorithm>
#include <initializer_list>
#include <map>
#include <sstream>
#include <stdexcept>

namespace gbasave {
namespace {

SavePrimitiveHook thumbHook(SavePrimitiveRole role, std::string name, std::size_t offset)
{
    return {role, std::move(name), offset, offset, 8u, PatchInstructionSet::Thumb};
}

SaveLibraryMatch scanFlash512(const RomImage &rom)
{
    const auto marker = findUniqueSaveMarker(rom, SaveLibraryFamily::Flash512);
    if (marker.marker.empty())
        throw std::runtime_error("FLASH512 marker not found");

    SaveLibraryMatch result;
    result.type = SaveType::Flash512;
    result.libraryName = "Nintendo " + marker.marker;
    result.marker = marker.marker;
    result.markerOffset = marker.offset;
    result.geometry = {SaveType::Flash512, 64u * 1024u, 64u * 1024u, 4u * 1024u, 1u, 16u};

    // Nintendo's common FLASH wrapper code is intentionally left intact.
    // These are the physical primitives beneath ReadFlash/VerifyFlashSector.
    const auto readId = uniquePattern(rom, exactPattern("30B591B0684600F0"), marker.marker + " ReadFlashId");
    const auto readCore = uniquePattern(rom, exactPattern("10B5041C531E002A"), marker.marker + " ReadFlash_Core");
    const auto verifyCore = uniquePattern(rom, exactPattern("30B5051C0B1C541E"), marker.marker + " VerifyFlashSector_Core");
    const auto eraseChip = uniquePattern(rom, exactPattern("70B590B0154D2988"), marker.marker + " EraseFlashChip");
    const auto eraseSector = uniquePattern(rom, exactPattern("70B5464640B490B0"), marker.marker + " EraseFlashSector");
    // ProgramFlashSector changed slightly across Nintendo FLASH_V revisions
    // while keeping the same ABI and physical primitive role. Match the stable
    // unique entry/prologue and validate the surrounding primitive family
    // independently instead of pinning one relocation-sensitive body.
    const auto programSector = uniquePattern(
        rom,
        exactPattern("F0B590B00F1C0004040C"),
        marker.marker + " ProgramFlashSector");

    result.primitiveHooks = {
        thumbHook(SavePrimitiveRole::FlashReadId, "ReadFlashId", readId),
        thumbHook(SavePrimitiveRole::FlashReadCore, "ReadFlash_Core", readCore),
        thumbHook(SavePrimitiveRole::FlashVerifyCore, "VerifyFlashSector_Core", verifyCore),
        thumbHook(SavePrimitiveRole::FlashEraseChip, "EraseFlashChip", eraseChip),
        thumbHook(SavePrimitiveRole::FlashEraseSector, "EraseFlashSector", eraseSector),
        thumbHook(SavePrimitiveRole::FlashProgramSector, "ProgramFlashSector", programSector),
    };
    return result;
}

SaveLibraryMatch scanEeprom(const RomImage &rom, SaveType requested)
{
    const auto marker = findUniqueSaveMarker(rom, SaveLibraryFamily::Eeprom);
    if (marker.marker.empty())
        throw std::runtime_error("EEPROM marker not found");
    if (marker.marker == "EEPROM_V111")
        throw std::runtime_error("EEPROM_V111 uses a different transfer path; the validated V120+ adapter refuses it");

    MaskedPattern readPattern;
    MaskedPattern writePattern;
    if (marker.marker == "EEPROM_V120" || marker.marker == "EEPROM_V121" || marker.marker == "EEPROM_V122") {
        readPattern = wildcardPattern("A2B00D1C0004030C034800688088834205D3014800E0", {20u});
        writePattern = wildcardPattern("30B5A9B00D1C0004040C034800688088844205D3014800E0", {22u});
    } else if (marker.marker == "EEPROM_V124") {
        readPattern = wildcardPattern("A2B00D1C0004030C034800688088834205D3014800E0", {20u});
        writePattern = exactPattern("F0B5ACB00D1C0004010C1206170E034800688088814205D3");
    } else {
        readPattern = exactPattern("A2B00D1C0004030C034800688088834205D301484AE0");
        writePattern = exactPattern("F0B5474680B4ACB00E1C0004050C1206120E904603480068");
    }

    const auto readSignature = uniquePattern(rom, readPattern, marker.marker + " ReadEepromDword");
    if (readSignature < 2u || (rom.u16(readSignature - 2u) & 0xFF00u) != 0xB500u)
        throw std::runtime_error("EEPROM read signature is not preceded by the expected Thumb PUSH entry");
    const auto writeEntry = uniquePattern(rom, writePattern, marker.marker + " ProgramEepromDword");

    SaveLibraryMatch result;
    result.type = requested == SaveType::Eeprom512 ? SaveType::Eeprom512 : SaveType::Eeprom8K;
    result.libraryName = "Nintendo " + marker.marker;
    result.marker = marker.marker;
    result.markerOffset = marker.offset;
    const std::size_t totalBytes = result.type == SaveType::Eeprom512 ? 512u : 8u * 1024u;
    result.geometry = {result.type, totalBytes, totalBytes, 512u, 1u, totalBytes / 512u};
    result.primitiveHooks = {
        thumbHook(SavePrimitiveRole::EepromReadDword, "ReadEepromDword", readSignature - 2u),
        thumbHook(SavePrimitiveRole::EepromProgramDword, "ProgramEepromDword", writeEntry),
    };
    return result;
}


std::uint32_t decodeArmImmediate(std::uint32_t instruction)
{
    const std::uint32_t immediate = instruction & 0xFFu;
    const std::uint32_t rotate = ((instruction >> 8u) & 0xFu) * 2u;
    if (rotate == 0u)
        return immediate;
    return (immediate >> rotate) | (immediate << (32u - rotate));
}

struct StandardIrqInfo {
    std::size_t dispatcherOffset{};
    std::size_t startupVectorLiteralOffset{};
};

StandardIrqInfo findStandardArmIrqInfo(const RomImage &rom)
{
    // Nintendo crt0 stores its ARM dispatcher into the BIOS user-IRQ vector.
    // We preserve that dispatcher completely and only redirect the vector
    // destination literal to our private original-handler slot.
    const std::size_t scanEnd = std::min<std::size_t>(rom.size(), 0x1000u);
    for (std::size_t offset = 0; offset + 12u <= scanEnd; offset += 4u) {
        const std::uint32_t loadVector = rom.u32(offset);
        if ((loadVector & 0xFFFFF000u) != 0xE59F1000u)
            continue;

        const std::size_t literalOffset = offset + 8u + (loadVector & 0xFFFu);
        if (literalOffset + 4u > rom.size() || rom.u32(literalOffset) != 0x03007FFCu)
            continue;

        const std::uint32_t makeHandler = rom.u32(offset + 4u);
        if ((makeHandler & 0xFFFFF000u) != 0xE28F0000u || rom.u32(offset + 8u) != 0xE5810000u)
            continue;

        const std::uint32_t handlerOffset = static_cast<std::uint32_t>(offset + 12u) +
            decodeArmImmediate(makeHandler);
        if (handlerOffset + 4u > rom.size() || rom.u32(handlerOffset) != 0xE3A03301u)
            continue;
        return {handlerOffset, literalOffset};
    }
    throw std::runtime_error("SRAM hotkey requires the standard Nintendo ARM IRQ dispatcher");
}

std::vector<std::size_t> findThumbIrqVectorLiteralOffsets(const RomImage &rom)
{
    std::vector<std::size_t> offsets;
    for (std::size_t instructionOffset = 0; instructionOffset + 2u <= rom.size(); instructionOffset += 2u) {
        const std::uint16_t instruction = rom.u16(instructionOffset);
        if ((instruction & 0xF800u) != 0x4800u)
            continue;
        const std::size_t literalOffset = ((instructionOffset + 4u) & ~std::size_t{3u}) +
            static_cast<std::size_t>(instruction & 0x00FFu) * 4u;
        if (literalOffset + 4u <= rom.size() && rom.u32(literalOffset) == 0x03007FFCu)
            offsets.push_back(literalOffset);
    }
    std::sort(offsets.begin(), offsets.end());
    offsets.erase(std::unique(offsets.begin(), offsets.end()), offsets.end());
    return offsets;
}


struct ThumbLiteralLoad {
    std::size_t instructionOffset{};
    std::size_t literalOffset{};
    std::uint32_t value{};
    std::uint8_t destinationRegister{};
};

std::vector<ThumbLiteralLoad> findThumbLiteralLoads(const RomImage &rom)
{
    std::vector<ThumbLiteralLoad> loads;
    for (std::size_t instructionOffset = 0; instructionOffset + 2u <= rom.size(); instructionOffset += 2u) {
        const std::uint16_t instruction = rom.u16(instructionOffset);
        if ((instruction & 0xF800u) != 0x4800u)
            continue;
        const std::size_t literalOffset = ((instructionOffset + 4u) & ~std::size_t{3u}) +
            static_cast<std::size_t>(instruction & 0x00FFu) * 4u;
        if (literalOffset + 4u > rom.size())
            continue;
        loads.push_back({
            instructionOffset,
            literalOffset,
            rom.u32(literalOffset),
            static_cast<std::uint8_t>((instruction >> 8u) & 7u)});
    }
    return loads;
}

bool decodeThumbBlTarget(const RomImage &rom, std::size_t instructionOffset, std::size_t &targetOffset);

struct SramRamMirrorEvidence {
    std::uint32_t base{};
    std::vector<std::uint32_t> logicalOffsets;
    std::uint32_t transferCoverageBytes{};
};

bool hasNearbyThumbBl(const RomImage &rom, std::size_t afterOffset)
{
    // A mirror-evidence pair should be preparing r0/r1 for a real Thumb call,
    // not just placing two unrelated pointers near each other. Thumb-1 BL is
    // encoded as F000..F7FF followed by F800..FFFF.
    const std::size_t end = std::min<std::size_t>(rom.size(), afterOffset + 0x14u);
    for (std::size_t offset = afterOffset; offset + 4u <= end; offset += 2u) {
        const std::uint16_t high = rom.u16(offset);
        const std::uint16_t low = rom.u16(offset + 2u);
        if ((high & 0xF800u) == 0xF000u && (low & 0xF800u) == 0xF800u)
            return true;
    }
    return false;
}


struct ThumbConstantState {
    bool known[8]{};
    std::uint32_t value[8]{};
};

void setThumbConstant(ThumbConstantState &state, unsigned reg, std::uint32_t value)
{
    if (reg >= 8u)
        return;
    state.known[reg] = true;
    state.value[reg] = value;
}

void clearThumbConstant(ThumbConstantState &state, unsigned reg)
{
    if (reg < 8u)
        state.known[reg] = false;
}

ThumbConstantState inferThumbCallArgumentConstants(
    const RomImage &rom,
    std::size_t callOffset,
    std::size_t windowBytes = 0x40u)
{
    ThumbConstantState state{};
    const std::size_t begin = callOffset > windowBytes ? callOffset - windowBytes : 0u;
    for (std::size_t offset = begin & ~std::size_t{1u}; offset + 2u <= callOffset; offset += 2u) {
        const std::uint16_t insn = rom.u16(offset);

        // LDR Rd,[PC,#imm]
        if ((insn & 0xF800u) == 0x4800u) {
            const unsigned rd = (insn >> 8u) & 7u;
            const std::size_t literal = ((offset + 4u) & ~std::size_t{3u}) +
                static_cast<std::size_t>(insn & 0x00FFu) * 4u;
            if (literal + 4u <= rom.size())
                setThumbConstant(state, rd, rom.u32(literal));
            else
                clearThumbConstant(state, rd);
            continue;
        }

        // MOV Rd,#imm8
        if ((insn & 0xF800u) == 0x2000u) {
            setThumbConstant(state, (insn >> 8u) & 7u, insn & 0x00FFu);
            continue;
        }

        // ADD/SUB Rd,#imm8
        if ((insn & 0xF800u) == 0x3000u || (insn & 0xF800u) == 0x3800u) {
            const unsigned rd = (insn >> 8u) & 7u;
            if (state.known[rd]) {
                const std::uint32_t imm = insn & 0x00FFu;
                state.value[rd] = (insn & 0x0800u) ? state.value[rd] - imm : state.value[rd] + imm;
            }
            continue;
        }

        // LSL/LSR immediate. These are common for Nintendo's size constants,
        // e.g. MOV r2,#0xF0; LSL r2,#7 -> 0x7800.
        if ((insn & 0xF800u) == 0x0000u || (insn & 0xF800u) == 0x0800u) {
            const unsigned imm = (insn >> 6u) & 0x1Fu;
            const unsigned rs = (insn >> 3u) & 7u;
            const unsigned rd = insn & 7u;
            if (state.known[rs]) {
                if ((insn & 0xF800u) == 0x0000u)
                    setThumbConstant(state, rd, state.value[rs] << imm);
                else
                    setThumbConstant(state, rd, imm == 0u ? 0u : state.value[rs] >> imm);
            } else {
                clearThumbConstant(state, rd);
            }
            continue;
        }

        // MOV low/high register. Only retain constants when the source is a
        // low register we are tracking; high-register values are deliberately
        // treated as unknown.
        if ((insn & 0xFF00u) == 0x4600u) {
            const unsigned rd = (insn & 7u) | ((insn >> 4u) & 8u);
            const unsigned rs = (insn >> 3u) & 0xFu;
            if (rd < 8u) {
                if (rs < 8u && state.known[rs])
                    setThumbConstant(state, rd, state.value[rs]);
                else
                    clearThumbConstant(state, rd);
            }
            continue;
        }

        // ADD/SUB register or imm3.
        if ((insn & 0xF800u) == 0x1800u) {
            const bool immediate = (insn & 0x0400u) != 0u;
            const bool subtract = (insn & 0x0200u) != 0u;
            const unsigned operand = (insn >> 6u) & 7u;
            const unsigned rs = (insn >> 3u) & 7u;
            const unsigned rd = insn & 7u;
            if (state.known[rs] && (immediate || state.known[operand])) {
                const std::uint32_t rhs = immediate ? operand : state.value[operand];
                setThumbConstant(state, rd, subtract ? state.value[rs] - rhs : state.value[rs] + rhs);
            } else {
                clearThumbConstant(state, rd);
            }
            continue;
        }

        // Loads/arithmetic with an unmodelled destination can invalidate an
        // argument constant. Be conservative for the low registers used by
        // save-library ABIs instead of carrying stale values across code.
        if ((insn & 0xF000u) == 0x6000u || (insn & 0xF000u) == 0x8000u ||
            (insn & 0xF000u) == 0x9000u) {
            clearThumbConstant(state, insn & 7u);
        }
    }
    return state;
}

SramRamMirrorEvidence inferSramRamMirrorFromValidatedTransfers(
    const RomImage &rom,
    std::uint32_t saveBytes,
    const std::vector<std::size_t> &readWrappers,
    const std::vector<std::size_t> &writeWrappers)
{
    struct Candidate {
        std::uint32_t base{};
        std::uint32_t coverage{};
        std::vector<std::uint32_t> offsets;
    };
    std::map<std::uint32_t, Candidate> candidates;

    const auto isTarget = [](std::size_t target, const std::vector<std::size_t> &targets) {
        return std::find(targets.begin(), targets.end(), target) != targets.end();
    };

    for (std::size_t callOffset = 0u; callOffset + 4u <= rom.size(); callOffset += 2u) {
        std::size_t target = 0u;
        if (!decodeThumbBlTarget(rom, callOffset, target))
            continue;
        if (!isTarget(target, readWrappers) && !isTarget(target, writeWrappers))
            continue;

        const auto args = inferThumbCallArgumentConstants(rom, callOffset);
        if (!args.known[0] || !args.known[1] || !args.known[2])
            continue;
        const std::uint32_t byteCount = args.value[2];
        if (byteCount == 0u || byteCount > saveBytes)
            continue;

        const auto consider = [&](std::uint32_t sramAddress, std::uint32_t ramAddress) {
            if (sramAddress < 0x0E000000u || sramAddress >= 0x0E000000u + saveBytes)
                return;
            if (ramAddress < 0x02000000u || ramAddress >= 0x02040000u)
                return;
            const std::uint32_t logicalOffset = sramAddress - 0x0E000000u;
            if (logicalOffset > saveBytes || byteCount > saveBytes - logicalOffset)
                return;
            if (ramAddress < logicalOffset)
                return;
            const std::uint32_t base = ramAddress - logicalOffset;
            if ((base & 3u) != 0u || base < 0x02000000u || base > 0x02040000u - saveBytes)
                return;
            if (ramAddress + byteCount > base + saveBytes)
                return;

            auto &candidate = candidates[base];
            candidate.base = base;
            candidate.coverage = std::max(candidate.coverage, byteCount);
            candidate.offsets.push_back(logicalOffset);
        };

        // Nintendo SRAM copy routines are seen in both source,destination
        // orientations across SDK/game wrappers, so prove the relation rather
        // than assuming one fixed argument order.
        consider(args.value[0], args.value[1]);
        consider(args.value[1], args.value[0]);
    }

    std::uint32_t bestBase = 0u;
    std::uint32_t bestCoverage = 0u;
    std::vector<std::uint32_t> bestOffsets;
    bool tied = false;
    for (auto &[base, candidate] : candidates) {
        std::sort(candidate.offsets.begin(), candidate.offsets.end());
        candidate.offsets.erase(std::unique(candidate.offsets.begin(), candidate.offsets.end()), candidate.offsets.end());
        // Hardware showed that one bulk transfer is not sufficient evidence.
        // Require two independent logical offsets even when one transfer happens
        // to cover most SRAM, so a coincidental EWRAM buffer cannot claim the
        // complete live save image.
        if (candidate.offsets.size() < 2u || candidate.coverage < (saveBytes * 3u) / 4u)
            continue;
        if (candidate.coverage > bestCoverage) {
            bestBase = base;
            bestCoverage = candidate.coverage;
            bestOffsets = candidate.offsets;
            tied = false;
        } else if (candidate.coverage == bestCoverage && candidate.coverage != 0u && base != bestBase) {
            tied = true;
        }
    }
    if (bestCoverage == 0u || tied)
        return {};
    return {bestBase, bestOffsets, bestCoverage};
}

SramRamMirrorEvidence mergeSramRamMirrorEvidence(
    const SramRamMirrorEvidence &literalPairs,
    const SramRamMirrorEvidence &validatedTransfers)
{
    if (literalPairs.base != 0u && validatedTransfers.base != 0u &&
        literalPairs.base != validatedTransfers.base)
        return {}; // conflicting structural proofs: fail closed
    if (validatedTransfers.base != 0u) {
        auto merged = validatedTransfers;
        if (literalPairs.base == validatedTransfers.base) {
            merged.logicalOffsets.insert(
                merged.logicalOffsets.end(), literalPairs.logicalOffsets.begin(), literalPairs.logicalOffsets.end());
            std::sort(merged.logicalOffsets.begin(), merged.logicalOffsets.end());
            merged.logicalOffsets.erase(
                std::unique(merged.logicalOffsets.begin(), merged.logicalOffsets.end()), merged.logicalOffsets.end());
        }
        return merged;
    }
    return literalPairs;
}

bool privateSramShadowIsCompatible(const RomImage &rom)
{
    constexpr std::uint32_t kPrivateShadowStart = 0x02027000u;
    constexpr std::uint32_t kPrivateShadowEnd = 0x0202F000u; // exclusive, 32 KiB
    constexpr std::uint32_t kPrivateStateStart = 0x0203BFE0u;
    constexpr std::uint32_t kPrivateWorkerEnd = 0x02040000u;

    const auto conflicts = [](std::uint32_t value) {
        return (value >= kPrivateShadowStart && value < kPrivateShadowEnd) ||
               (value >= kPrivateStateStart && value < kPrivateWorkerEnd);
    };

    // A pointer-looking word is data until executable structure proves code
    // actually loads it. Likewise, arbitrary compressed/filler halfwords can
    // decode as Thumb LDR literals by chance. Require the same strong
    // function-prologue class used by the shared analyzer for authoritative
    // executable evidence: push {...,lr} for Thumb, or a canonical LR-saving
    // ARM prologue. Weak push-without-LR/filler evidence must not reject an
    // otherwise proven legacy workspace.
    const auto hasStrongThumbPrologue = [&rom](std::size_t instructionOffset) {
        constexpr std::size_t kWindow = 0x600u;
        const std::size_t lower = instructionOffset > kWindow ? instructionOffset - kWindow : 0u;
        for (std::size_t offset = instructionOffset & ~std::size_t{1u};; offset -= 2u) {
            if (offset + 2u <= rom.size()) {
                const std::uint16_t halfword = rom.u16(offset);
                if ((halfword & 0xFF00u) == 0xB500u) // push {...,lr}
                    return true;
            }
            if (offset + 16u <= rom.size()) {
                bool allZero = true;
                bool allFf = true;
                for (std::size_t index = 0u; index < 16u; ++index) {
                    const std::uint8_t value = rom.bytes()[offset + index];
                    allZero = allZero && value == 0x00u;
                    allFf = allFf && value == 0xFFu;
                }
                if (allZero || allFf)
                    break;
            }
            if (offset < lower + 2u)
                break;
        }
        return false;
    };

    const auto hasStrongArmPrologue = [&rom](std::size_t instructionOffset) {
        constexpr std::size_t kWindow = 0x800u;
        const std::size_t lower = instructionOffset > kWindow ? instructionOffset - kWindow : 0u;
        for (std::size_t offset = instructionOffset & ~std::size_t{3u};; offset -= 4u) {
            if (offset + 4u <= rom.size()) {
                const std::uint32_t word = rom.u32(offset);
                if (((word & 0xFFFF0000u) == 0xE92D0000u && (word & (1u << 14u)) != 0u) ||
                    word == 0xE52DE004u)
                    return true;
            }
            if (offset + 16u <= rom.size()) {
                bool allZero = true;
                bool allFf = true;
                for (std::size_t index = 0u; index < 16u; ++index) {
                    const std::uint8_t value = rom.bytes()[offset + index];
                    allZero = allZero && value == 0x00u;
                    allFf = allFf && value == 0xFFu;
                }
                if (allZero || allFf)
                    break;
            }
            if (offset < lower + 4u)
                break;
        }
        return false;
    };

    for (const auto &load : findThumbLiteralLoads(rom)) {
        if (conflicts(load.value) && hasStrongThumbPrologue(load.instructionOffset))
            return false;
    }

    // ARM single-data-transfer LDR Rd,[PC,+/-imm12], immediate/pre-indexed.
    // Only the loaded literal value matters; raw aligned words are never
    // references on their own.
    for (std::size_t instructionOffset = 0u; instructionOffset + 4u <= rom.size(); instructionOffset += 4u) {
        const std::uint32_t word = rom.u32(instructionOffset);
        if ((word & 0x0C100000u) != 0x04100000u ||
            ((word >> 16u) & 0xFu) != 15u ||
            (word & (1u << 25u)) != 0u)
            continue;
        const std::size_t immediate = word & 0xFFFu;
        const std::size_t base = instructionOffset + 8u;
        std::size_t literalOffset = 0u;
        if ((word & (1u << 23u)) != 0u) {
            literalOffset = base + immediate;
        } else {
            if (immediate > base)
                continue;
            literalOffset = base - immediate;
        }
        if (literalOffset + 4u > rom.size())
            continue;
        if (conflicts(rom.u32(literalOffset)) && hasStrongArmPrologue(instructionOffset))
            return false;
    }
    return true;
}

std::vector<std::uint32_t> decodedRamLiteralValues(const RomImage &rom)
{
    std::vector<std::uint32_t> values;
    for (const auto &load : findThumbLiteralLoads(rom)) {
        if (load.value >= 0x02000000u && load.value < 0x02040000u)
            values.push_back(load.value);
    }

    // Conservative ARM PC-relative literal loads. For the tiny runtime-state
    // reservation false positives are preferable to corrupting game RAM.
    for (std::size_t instructionOffset = 0u; instructionOffset + 4u <= rom.size(); instructionOffset += 4u) {
        const std::uint32_t word = rom.u32(instructionOffset);
        if ((word & 0x0C100000u) != 0x04100000u ||
            ((word >> 16u) & 0xFu) != 15u ||
            (word & (1u << 25u)) != 0u)
            continue;
        const std::size_t immediate = word & 0xFFFu;
        const std::size_t base = instructionOffset + 8u;
        std::size_t literalOffset = 0u;
        if ((word & (1u << 23u)) != 0u) {
            literalOffset = base + immediate;
        } else {
            if (immediate > base)
                continue;
            literalOffset = base - immediate;
        }
        if (literalOffset + 4u > rom.size())
            continue;
        const std::uint32_t value = rom.u32(literalOffset);
        if (value >= 0x02000000u && value < 0x02040000u)
            values.push_back(value);
    }
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end()), values.end());
    return values;
}

bool decodedRamLiteralInRange(const RomImage &rom, std::uint32_t start, std::uint32_t end)
{
    const auto values = decodedRamLiteralValues(rom);
    const auto it = std::lower_bound(values.begin(), values.end(), start);
    return it != values.end() && *it < end;
}

std::uint32_t inferSramRuntimeStateAddress(
    const RomImage &rom,
    std::uint32_t mirrorBase,
    std::uint32_t saveBytes)
{
    constexpr std::uint32_t kStateBytes = 16u;
    constexpr std::uint32_t kPrivateState = 0x0203BFE0u;
    constexpr std::uint32_t kGapGuardBytes = 0x200u;
    const std::uint32_t mirrorEnd = mirrorBase + saveBytes;

    const auto overlapsMirror = [mirrorBase, mirrorEnd](std::uint32_t start, std::uint32_t bytes) {
        const std::uint32_t end = start + bytes;
        return start < mirrorEnd && end > mirrorBase;
    };

    // First reuse the historical private state island when the game-owned
    // save image does not cover it and executable structure never references it.
    if (!overlapsMirror(kPrivateState, kStateBytes) &&
        !decodedRamLiteralInRange(rom, kPrivateState, kPrivateState + kStateBytes))
        return kPrivateState;

    // Full-size games such as a full-size SRAM title can own the top 32 KiB of EWRAM. In that
    // case accept a tiny slot immediately below the proven save image only
    // when a sizeable guard window below the mirror is entirely free of
    // instruction-decoded RAM literals. This is intentionally fail-closed.
    if (mirrorBase >= 0x02000000u + kGapGuardBytes + 0x20u) {
        const std::uint32_t candidate = (mirrorBase - 0x20u) & ~0xFu;
        const std::uint32_t guardStart = candidate - kGapGuardBytes;
        if (candidate >= 0x02000000u && candidate + kStateBytes <= mirrorBase &&
            !decodedRamLiteralInRange(rom, guardStart, mirrorBase))
            return candidate;
    }

    // Generic guarded-gap fallback. Some games reference the historical 0203BFE0 island but still leave a
    // large structurally silent EWRAM gap elsewhere. Reserve only 16 bytes in
    // a gap that is at least 0x200 bytes away from every instruction-decoded
    // EWRAM literal, stays below the separate 0203C000 flash/eeprom worker
    // reservation, and does not overlap the proven save image. This remains
    // fail-closed: absence of a guarded gap means SRAM patching is refused.
    constexpr std::uint32_t kSearchStart = 0x02000400u;
    constexpr std::uint32_t kSearchEnd = 0x0203C000u;
    const auto refs = decodedRamLiteralValues(rom);
    std::uint32_t upper = kSearchEnd;
    for (auto it = refs.rbegin(); it != refs.rend(); ++it) {
        const std::uint32_t ref = *it;
        if (ref >= kSearchEnd)
            continue;
        const std::uint32_t guardedHigh = ref > kGapGuardBytes ? ref - kGapGuardBytes : kSearchStart;
        if (upper > guardedHigh && upper - guardedHigh >= kStateBytes) {
            std::uint32_t candidate = (upper - kStateBytes) & ~0xFu;
            if (candidate >= guardedHigh && candidate >= kSearchStart &&
                !overlapsMirror(candidate, kStateBytes))
                return candidate;
        }
        const std::uint32_t nextUpper = ref + kGapGuardBytes;
        if (nextUpper < upper)
            upper = nextUpper;
    }
    if (upper > kSearchStart && upper - kSearchStart >= kStateBytes) {
        const std::uint32_t candidate = (upper - kStateBytes) & ~0xFu;
        if (candidate >= kSearchStart && !overlapsMirror(candidate, kStateBytes))
            return candidate;
    }
    return 0u;
}

SramRamMirrorEvidence inferSramRamMirrorFromLiteralPairs(const RomImage &rom, std::uint32_t saveBytes)
{
    // Nintendo SRAM games often keep a RAM image whose offsets mirror the
    // physical SRAM aperture.  Prove that relation from code: a nearby pair
    // of Thumb literal loads must reference EWRAM R+off and SRAM 0E000000+off.
    // Two distinct offsets are required so one coincidental pointer cannot
    // claim 32 KiB of game RAM.
    // SDK ReadSram/WriteSram arguments are r0/r1. Require two very-nearby
    // PC-relative loads into those distinct argument registers; this turns the
    // mirror relation into call-site-shaped evidence rather than a generic
    // pointer-proximity heuristic.
    constexpr std::size_t kPairDistance = 0x10u;
    const auto loads = findThumbLiteralLoads(rom);
    std::map<std::uint32_t, std::vector<std::uint32_t>> evidence;

    for (const auto &ramLoad : loads) {
        if (ramLoad.value < 0x02000000u || ramLoad.value >= 0x02040000u)
            continue;
        for (const auto &sramLoad : loads) {
            if (sramLoad.value < 0x0E000000u || sramLoad.value >= 0x0E000000u + saveBytes)
                continue;
            if (ramLoad.destinationRegister > 1u || sramLoad.destinationRegister > 1u ||
                ramLoad.destinationRegister == sramLoad.destinationRegister)
                continue;
            const std::size_t distance = ramLoad.instructionOffset > sramLoad.instructionOffset
                ? ramLoad.instructionOffset - sramLoad.instructionOffset
                : sramLoad.instructionOffset - ramLoad.instructionOffset;
            if (distance > kPairDistance)
                continue;
            const std::size_t laterInstruction = std::max(
                ramLoad.instructionOffset, sramLoad.instructionOffset);
            if (!hasNearbyThumbBl(rom, laterInstruction + 2u))
                continue;

            const std::uint32_t logicalOffset = sramLoad.value - 0x0E000000u;
            if (ramLoad.value < logicalOffset)
                continue;
            const std::uint32_t base = ramLoad.value - logicalOffset;
            if ((base & 3u) != 0u || base < 0x02000000u ||
                base > 0x02040000u - saveBytes)
                continue;
            evidence[base].push_back(logicalOffset);
        }
    }

    std::uint32_t bestBase = 0u;
    std::vector<std::uint32_t> bestOffsets;
    bool tied = false;
    for (auto &[base, offsets] : evidence) {
        std::sort(offsets.begin(), offsets.end());
        offsets.erase(std::unique(offsets.begin(), offsets.end()), offsets.end());
        if (offsets.size() < 2u)
            continue;
        if (offsets.size() > bestOffsets.size()) {
            bestBase = base;
            bestOffsets = offsets;
            tied = false;
        } else if (offsets.size() == bestOffsets.size() && !offsets.empty()) {
            tied = true;
        }
    }
    if (bestOffsets.size() < 2u || tied)
        return {};
    return {bestBase, bestOffsets, 0u};
}

std::vector<std::size_t> findThumbSramLiteralOffsets(const RomImage &rom)
{
    std::vector<std::size_t> literalOffsets;
    for (std::size_t instructionOffset = 0; instructionOffset + 2u <= rom.size(); instructionOffset += 2u) {
        const std::uint16_t instruction = rom.u16(instructionOffset);
        if ((instruction & 0xF800u) != 0x4800u) // LDR Rd,[PC,#imm]
            continue;

        const std::size_t literalOffset = ((instructionOffset + 4u) & ~std::size_t{3u}) +
            static_cast<std::size_t>(instruction & 0x00FFu) * 4u;
        if (literalOffset + 4u > rom.size())
            continue;

        const std::uint32_t value = rom.u32(literalOffset);
        if (value >= 0x0E000000u && value < 0x0E008000u)
            literalOffsets.push_back(literalOffset);
    }
    std::sort(literalOffsets.begin(), literalOffsets.end());
    literalOffsets.erase(std::unique(literalOffsets.begin(), literalOffsets.end()), literalOffsets.end());
    return literalOffsets;
}


bool decodeThumbBlTarget(const RomImage &rom, std::size_t instructionOffset, std::size_t &targetOffset)
{
    if (instructionOffset + 4u > rom.size())
        return false;
    const std::uint16_t high = rom.u16(instructionOffset);
    const std::uint16_t low = rom.u16(instructionOffset + 2u);
    if ((high & 0xF800u) != 0xF000u || (low & 0xF800u) != 0xF800u)
        return false;

    std::int32_t displacement = static_cast<std::int32_t>(
        ((static_cast<std::uint32_t>(high & 0x07FFu)) << 12u) |
        ((static_cast<std::uint32_t>(low & 0x07FFu)) << 1u));
    if ((displacement & (1 << 22)) != 0)
        displacement -= (1 << 23);
    const std::int64_t target = static_cast<std::int64_t>(instructionOffset + 4u) + displacement;
    if (target < 0 || static_cast<std::uint64_t>(target) >= rom.size())
        return false;
    targetOffset = static_cast<std::size_t>(target);
    return true;
}

std::uint32_t inferSramPreloadRefreshTrigger(
    const RomImage &rom,
    std::uint32_t saveBytes,
    const std::vector<std::size_t> &readWrappers,
    const std::vector<std::size_t> &directLiteralOffsets)
{
    // Conservative structural proof for a destructive tail-of-SRAM preload
    // self-test.  This deliberately has no title/game-code/SDK-version checks.
    // Require:
    //   * at least two distinct SRAM literals in the final 0x100 bytes;
    //   * each is actually loaded by Thumb code and followed shortly by a call
    //     to a validated Nintendo ReadSram wrapper; and
    //   * the chosen (highest) offset has at least two independent code refs.
    // If that proof is absent, return disabled rather than guess.  This avoids
    // turning an ordinary late-SRAM gameplay write into a destructive restore.
    if (saveBytes < 0x100u || readWrappers.empty())
        return 0xFFFFFFFFu;

    struct Candidate {
        std::uint32_t logicalOffset{};
        std::size_t qualifyingRefs{};
    };
    std::vector<Candidate> candidates;
    const std::uint32_t tailStart = saveBytes - 0x100u;

    for (const auto literalOffset : directLiteralOffsets) {
        if (literalOffset + 4u > rom.size())
            continue;
        const std::uint32_t address = rom.u32(literalOffset);
        if (address < 0x0E000000u || address >= 0x0E000000u + saveBytes)
            continue;
        const std::uint32_t logical = address - 0x0E000000u;
        if (logical < tailStart)
            continue;

        std::size_t refs = 0u;
        for (std::size_t instructionOffset = 0; instructionOffset + 2u <= rom.size(); instructionOffset += 2u) {
            const std::uint16_t instruction = rom.u16(instructionOffset);
            if ((instruction & 0xF800u) != 0x4800u)
                continue;
            const std::size_t loadedLiteral = ((instructionOffset + 4u) & ~std::size_t{3u}) +
                static_cast<std::size_t>(instruction & 0x00FFu) * 4u;
            if (loadedLiteral != literalOffset)
                continue;

            const std::size_t searchEnd = std::min<std::size_t>(rom.size(), instructionOffset + 0x50u);
            bool reachesReadWrapper = false;
            for (std::size_t callOffset = instructionOffset + 2u; callOffset + 4u <= searchEnd; callOffset += 2u) {
                std::size_t target = 0u;
                if (!decodeThumbBlTarget(rom, callOffset, target))
                    continue;
                if (std::find(readWrappers.begin(), readWrappers.end(), target) != readWrappers.end()) {
                    reachesReadWrapper = true;
                    break;
                }
            }
            if (reachesReadWrapper)
                ++refs;
        }
        if (refs != 0u)
            candidates.push_back({logical, refs});
    }

    std::sort(candidates.begin(), candidates.end(), [](const auto &left, const auto &right) {
        return left.logicalOffset < right.logicalOffset;
    });
    candidates.erase(std::unique(candidates.begin(), candidates.end(), [](const auto &left, const auto &right) {
        return left.logicalOffset == right.logicalOffset;
    }), candidates.end());
    if (candidates.size() < 2u)
        return 0xFFFFFFFFu;

    const auto &trigger = candidates.back();
    if (trigger.qualifyingRefs < 2u)
        return 0xFFFFFFFFu;
    return trigger.logicalOffset;
}

SaveLibraryMatch scanSram(const RomImage &rom)
{
    const auto marker = findUniqueSaveMarker(rom, SaveLibraryFamily::Sram);
    if (marker.marker.empty())
        throw std::runtime_error("supported SRAM marker not found");

    const auto readWrapperPattern = exactPattern("70B5A0B0041C0D1C161C084A10880849");
    const auto writePattern = exactPattern("30B5051C0C1C131C0B4A10880B490840");
    const auto verifyWrapperPattern = exactPattern("70B5B0B0041C0D1C161C084A10880849");
    const auto readWrappers = findPattern(rom, readWrapperPattern);
    const auto writers = findPattern(rom, writePattern);
    const auto verifyWrappers = findPattern(rom, verifyWrapperPattern);

    if (readWrappers.empty() || writers.empty() || verifyWrappers.empty())
        throw std::runtime_error("SRAM marker found but standard Nintendo Read/Write/Verify routines were not all found");
    if (readWrappers.size() != writers.size() || writers.size() != verifyWrappers.size())
        throw std::runtime_error("SRAM SDK copy count mismatch between ReadSram/WriteSram/VerifySram");

    SaveLibraryMatch result;
    result.type = SaveType::Sram;
    result.libraryName = "Nintendo " + marker.marker;
    result.marker = marker.marker;
    result.markerOffset = marker.offset;
    result.geometry = {SaveType::Sram, 32u * 1024u, 32u * 1024u, 32u * 1024u, 1u, 1u};

    for (std::size_t index = 0; index < readWrappers.size(); ++index) {
        // ReadSram/VerifySram copy their cores from immediately preceding ROM
        // code into RAM. The core-source literal at wrapper+0x34 is the Thumb source.
        const auto readCoreAddress = rom.u32(readWrappers[index] + 0x34u) & ~1u;
        const auto verifyCoreAddress = rom.u32(verifyWrappers[index] + 0x34u) & ~1u;

        const auto chooseCoreOrWrapper = [&](std::uint32_t coreAddress, std::size_t wrapperOffset, const char *name) {
            if (coreAddress >= 0x08000000u && coreAddress < 0x08000000u + rom.size()) {
                const std::size_t coreOffset = coreAddress - 0x08000000u;
                if (coreOffset >= wrapperOffset || wrapperOffset - coreOffset < 8u)
                    throw std::runtime_error(std::string(name) + " core layout is not the expected copied-core form");
                return coreOffset;
            }
            // Some games carry a second SDK copy whose wrapper copies from a
            // preloaded RAM helper. There is no ROM core to patch in that copy,
            // so patch the wrapper boundary as the safe fallback.
            if ((coreAddress >= 0x02000000u && coreAddress < 0x02040000u) ||
                (coreAddress >= 0x03000000u && coreAddress < 0x03008000u))
                return wrapperOffset;
            throw std::runtime_error(std::string(name) + " core points outside recognized ROM/RAM ranges");
        };

        const auto readEntry = chooseCoreOrWrapper(readCoreAddress, readWrappers[index], "ReadSram");
        const auto verifyEntry = chooseCoreOrWrapper(verifyCoreAddress, verifyWrappers[index], "VerifySram");
        result.primitiveHooks.push_back(thumbHook(SavePrimitiveRole::SramReadCore,
            readEntry == readWrappers[index] ? "ReadSram" : "ReadSram_Core", readEntry));
        result.primitiveHooks.push_back(thumbHook(SavePrimitiveRole::SramWrite, "WriteSram", writers[index]));
        result.primitiveHooks.push_back(thumbHook(SavePrimitiveRole::SramVerifyCore,
            verifyEntry == verifyWrappers[index] ? "VerifySram" : "VerifySram_Core", verifyEntry));
    }

    result.directSramLiteralOffsets = findThumbSramLiteralOffsets(rom);
    result.sramPreloadRefreshTriggerOffset = inferSramPreloadRefreshTrigger(
        rom,
        static_cast<std::uint32_t>(result.geometry.totalBytes),
        readWrappers,
        result.directSramLiteralOffsets);
    const auto literalPairMirror = inferSramRamMirrorFromLiteralPairs(
        rom, static_cast<std::uint32_t>(result.geometry.totalBytes));
    const auto transferMirror = inferSramRamMirrorFromValidatedTransfers(
        rom,
        static_cast<std::uint32_t>(result.geometry.totalBytes),
        readWrappers,
        writers);
    const auto ramMirror = mergeSramRamMirrorEvidence(literalPairMirror, transferMirror);
    result.sramRamMirrorBase = ramMirror.base;
    result.sramRamMirrorEvidenceOffsets = ramMirror.logicalOffsets;
    result.sramRamMirrorTransferCoverageBytes = ramMirror.transferCoverageBytes;
    if (result.sramRamMirrorBase != 0u) {
        result.sramRuntimeStateAddress = inferSramRuntimeStateAddress(
            rom, result.sramRamMirrorBase, static_cast<std::uint32_t>(result.geometry.totalBytes));
    }
    result.sramPrivateShadowCompatible = privateSramShadowIsCompatible(rom);
    const auto irqInfo = findStandardArmIrqInfo(rom);
    result.sramIrqDispatcherOffset = irqInfo.dispatcherOffset;
    result.sramIrqVectorLiteralOffsets = findThumbIrqVectorLiteralOffsets(rom);
    result.sramIrqVectorLiteralOffsets.push_back(irqInfo.startupVectorLiteralOffset);
    std::sort(result.sramIrqVectorLiteralOffsets.begin(), result.sramIrqVectorLiteralOffsets.end());
    result.sramIrqVectorLiteralOffsets.erase(
        std::unique(result.sramIrqVectorLiteralOffsets.begin(), result.sramIrqVectorLiteralOffsets.end()),
        result.sramIrqVectorLiteralOffsets.end());
    return result;
}

} // namespace

SaveLibraryMatch SaveLibraryScanner::scan(const RomImage &rom, SaveType requested) const
{
    GbabrExactPlan exactPlan;
    const bool hasExactPlan = findGbabrExactPlan(rom, exactPlan);
    if (requested == SaveType::Unknown) {
        if (hasExactPlan)
            requested = exactPlan.saveType;
        else
            requested = autoDetectSaveType(rom);
    }
    if (hasExactPlan && exactPlan.saveType != requested)
        throw std::runtime_error("requested save type disagrees with exact shared GBABR plan");

    // Preserve the v1.0 single-library scanner first. Exact-plan fallbacks are
    // used only where v1.0 rejected legitimate duplicate/fast SDK layouts.
    try {
        switch (requested) {
        case SaveType::Flash1M: {
            const auto marker = findUniqueSaveMarker(rom, SaveLibraryFamily::Flash1M);
            if (marker.marker.empty())
                throw std::runtime_error("FLASH1M marker not found");
            SaveLibraryMatch result;
            result.type = SaveType::Flash1M;
            result.libraryName = "Nintendo " + marker.marker;
            result.marker = marker.marker;
            result.markerOffset = marker.offset;
            result.geometry = {SaveType::Flash1M, 128u * 1024u, 64u * 1024u, 4u * 1024u, 2u, 16u};
            return result;
        }
        case SaveType::Flash512:
            return scanFlash512(rom);
        case SaveType::Eeprom512:
        case SaveType::Eeprom8K:
            return scanEeprom(rom, requested);
        case SaveType::Sram:
            return scanSram(rom);
        default:
            throw std::runtime_error("unsupported requested save type");
        }
    } catch (const std::runtime_error &) {
        if (!hasExactPlan)
            throw;
    }

    return scanSaveLibraryFromExactPlan(rom, requested, exactPlan);
}

std::string SaveLibraryScanner::toText(const RomImage &rom, const SaveLibraryMatch &match)
{
    std::ostringstream stream;
    stream << "GBASaveHandler save-library map\n";
    stream << "ROM: " << rom.title() << " / " << rom.gameCode() << " rev " << static_cast<unsigned>(rom.revision()) << "\n";
    stream << "Library: " << match.libraryName << "\n";
    stream << "Marker: " << match.marker << " @ " << hex(match.markerOffset, 6) << "\n";
    stream << "Save type: " << toString(match.type) << " / " << match.geometry.totalBytes << " bytes\n";
    for (const auto &hook : match.primitiveHooks) {
        stream << "  " << toString(hook.role) << " " << hook.name
               << " signature=" << hex(hook.signatureOffset, 6)
               << " entry=" << hex(hook.entryOffset, 6) << "\n";
    }
    if (!match.directSramLiteralOffsets.empty())
        stream << "  Direct SRAM Thumb literals: " << match.directSramLiteralOffsets.size() << "\n";
    if (match.type == SaveType::Sram) {
        stream << "  SRAM IRQ dispatcher preserved: " << hex(match.sramIrqDispatcherOffset, 6) << "\n";
        stream << "  SRAM IRQ-vector literals: " << match.sramIrqVectorLiteralOffsets.size() << "\n";
        if (match.sramRamMirrorBase != 0u) {
            stream << "  SRAM game-owned EWRAM mirror: " << hex(match.sramRamMirrorBase, 8)
                   << " evidence_offsets=" << match.sramRamMirrorEvidenceOffsets.size()
                   << " bulk_coverage=" << match.sramRamMirrorTransferCoverageBytes << "\n";
        }
        if (match.sramRuntimeStateAddress != 0u)
            stream << "  SRAM runtime state: " << hex(match.sramRuntimeStateAddress, 8)
                   << " ANALYZER_PROVEN\n";
        stream << "  SRAM legacy private shadow: "
               << (match.sramPrivateShadowCompatible ? "RAM_AUDIT_PASS" : "RAM_AUDIT_REJECT") << "\n";
        if (match.sramPreloadRefreshTriggerOffset != 0xFFFFFFFFu)
            stream << "  SRAM preload-refresh trigger: " << hex(match.sramPreloadRefreshTriggerOffset, 4)
                   << " STRUCTURAL_PROOF\n";
    }
    return stream.str();
}

std::string toString(SavePrimitiveRole role)
{
    switch (role) {
    case SavePrimitiveRole::FlashSwitchBank: return "FlashSwitchBank";
    case SavePrimitiveRole::FlashReadId: return "FlashReadId";
    case SavePrimitiveRole::FlashReadCore: return "FlashReadCore";
    case SavePrimitiveRole::FlashVerifyCore: return "FlashVerifyCore";
    case SavePrimitiveRole::FlashEraseChip: return "FlashEraseChip";
    case SavePrimitiveRole::FlashEraseSector: return "FlashEraseSector";
    case SavePrimitiveRole::FlashProgramSector: return "FlashProgramSector";
    case SavePrimitiveRole::EepromReadDword: return "EepromReadDword";
    case SavePrimitiveRole::EepromProgramDword: return "EepromProgramDword";
    case SavePrimitiveRole::SramReadCore: return "SramReadCore";
    case SavePrimitiveRole::SramWrite: return "SramWrite";
    case SavePrimitiveRole::SramVerifyCore: return "SramVerifyCore";
    }
    return "Unknown";
}

} // namespace gbasave
