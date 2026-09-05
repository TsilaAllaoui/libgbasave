// GBASaveHandler GBA runtime.
// Freestanding C++17; no CRT, heap, exceptions, or standard library.
//
// The game-facing save protocol is kept separate from the physical NOR store.
// FLASH keeps Nintendo's stock wrappers and uses append-only versioned slots.
// EEPROM uses commit-last record lanes or compact A/B erase-domain lanes. SRAM uses a RAM working shadow during
// gameplay and commits that shadow to a persistent NOR mirror only on hotkey.

extern "C" {
using u8 = unsigned char;
using u16 = unsigned short;
using u32 = unsigned int;
using uptr = unsigned int;

struct RuntimeConfig {
    u32 magic;
    u32 version;
    u16 protocol;
    u16 storageMode;
    u16 norFlashType;
    u16 forcedFlashId;
    u16 norProgramCommand;
    u16 reservedNorConfig;
    u32 logicalSaveSizeBytes;
    u32 logicalBankSizeBytes;
    u32 logicalUnitSizeBytes;
    u32 physicalBlockBytes;
    u16 logicalUnitCount;
    u16 logicalUnitShift;
    u16 logicalUnitOffsetMask;
    u16 unitsPerBank;
    u16 bankCount;
    u16 bankSelectShift;
    u16 flashSlotsPerBlock;
    u16 physicalStorageBlockCount;
    u32 flashSlotMetadataOffsetBytes;
    u32 forcedSetupProfileAddress;
    u32 currentBankStateAddress;
    u32 globalWaitForWritePointer;
    u32 globalProgramSectorPointer;
    u32 globalGeometryPointer;
    u32 globalEraseChipPointer;
    u32 globalEraseSectorPointer;
    u32 globalMaxTimePointer;
    u32 sramMirrorAddress;
    u32 sramBackupAddress;
    u32 sramShadowAddress;
    u32 sramBootResumeAddress;
    u32 sramSizeBytes;
    u32 originalSramBaseAddress;
    u16 sramHotkeyRaw;
    u16 reservedSramConfig;
    u32 sramRefreshTriggerOffset;
    u32 sramIrqDispatcherAddress;
    u32 sramRuntimeStateAddress;
    u32 sramJournalRecordBytes;
    u16 sramJournalSpanCount;
    u16 sramJournalRecordCount;
    u32 sramJournalSpanAddresses[16];
    u32 sramJournalSpanLengths[16];
    u32 storageBlockAddresses[64];
};

__attribute__((section(".runtime_config"), used, aligned(4)))
RuntimeConfig gRuntimeConfig = {
    0x48565347u, 14u,
    1u, 1u, 1u, 0x09C2u, 0x0040u, 0u,
    0x20000u, 0x10000u, 0x1000u, 0x10000u,
    32u, 12u, 0x0FFFu, 16u, 2u, 4u, 15u, 32u, 0xF000u,
    0u, 0x030079E0u,
    0u, 0u, 0u, 0u, 0u, 0u,
    0u, 0u, 0x02027000u, 0u, 0x8000u, 0x0E000000u, 0x00F9u, 0u, 0xFFFFFFFFu, 0u,
    0x0203BFE0u, 0u, 0u, 0u, {0u}, {0u},
    {0u},
};

extern u8 __ram_worker_start[];
extern u8 __ram_worker_end[];
extern u8 __sram_stack_worker_start[];
extern u8 __sram_stack_worker_end[];

constexpr uptr kRamWorkerAddress = 0x0203C000u;
constexpr u16 kSramConfigGameOwnedShadowSnapshot = 0x0001u;
constexpr u16 kSramConfigFullReadRefresh = 0x0002u;
constexpr uptr kGamePakSaveWindow = 0x0E000000u;
constexpr u16 kSlotDataTag = 0xA55Au;
constexpr u16 kSlotBlankTag = 0x5AA5u;
constexpr u16 kSlotVirginTag = 0xFFFFu;
constexpr u16 kSlotDeadTag = 0x0000u;

// Persistent diagnostic breadcrumbs. These bytes are patcher-owned metadata,
// never part of the logical game save image. Stages are monotonic 1->0 bit
// clears so a power cut leaves the last completed boundary readable offline.
constexpr u16 kBreadcrumbMagic = 0xBC32u;
constexpr u16 kBreadcrumbOpSramCommit = 0x0001u;
constexpr u16 kBreadcrumbOpFlashSnapshot = 0x0002u;
constexpr u16 kBreadcrumbOpFlashPatch = 0x0003u;
constexpr u16 kBreadcrumbOpFlashBlank = 0x0004u;
constexpr u16 kBreadcrumbStageEnter = 0xFFFEu;
constexpr u16 kBreadcrumbStageDataWritten = 0xFFFCu;
constexpr u16 kBreadcrumbStageDataVerified = 0xFFF8u;
constexpr u16 kBreadcrumbStageMetadataWritten = 0xFFF0u;
constexpr u16 kBreadcrumbStageCommitted = 0xFFE0u;
constexpr u16 kBreadcrumbStageFinalVerified = 0xFFC0u;
constexpr u32 kFlashBreadcrumbOffsetFromMetadata = 0x80u;
constexpr u32 kFlashBreadcrumbStride = 8u;

struct BreadcrumbRecord {
    u16 magic;
    u16 operation;
    u16 subject;
    u16 stageBits;
};
constexpr u32 kSyntheticErasedSource = 0xFFFFFFFFu;

static inline volatile u16 &reg16(uptr address)
{
    return *reinterpret_cast<volatile u16 *>(address);
}

static inline volatile u8 &reg8(uptr address)
{
    return *reinterpret_cast<volatile u8 *>(address);
}

static inline void copyBytes(void *destination, const void *source, u32 byteCount)
{
    auto *out8 = static_cast<u8 *>(destination);
    const auto *in8 = static_cast<const u8 *>(source);

    while (byteCount && ((reinterpret_cast<uptr>(out8) | reinterpret_cast<uptr>(in8)) & 1u)) {
        *out8++ = *in8++;
        --byteCount;
    }

    if (((reinterpret_cast<uptr>(out8) | reinterpret_cast<uptr>(in8)) & 3u) == 0u) {
        auto *out32 = reinterpret_cast<u32 *>(out8);
        const auto *in32 = reinterpret_cast<const u32 *>(in8);
        while (byteCount >= 4u) {
            *out32++ = *in32++;
            byteCount -= 4u;
        }
        out8 = reinterpret_cast<u8 *>(out32);
        in8 = reinterpret_cast<const u8 *>(in32);
    } else {
        auto *out16 = reinterpret_cast<u16 *>(out8);
        const auto *in16 = reinterpret_cast<const u16 *>(in8);
        while (byteCount >= 2u) {
            *out16++ = *in16++;
            byteCount -= 2u;
        }
        out8 = reinterpret_cast<u8 *>(out16);
        in8 = reinterpret_cast<const u8 *>(in16);
    }

    while (byteCount--)
        *out8++ = *in8++;
}

static inline void fillBytes(void *destination, u8 value, u32 byteCount)
{
    auto *out = static_cast<u8 *>(destination);
    while (byteCount--)
        *out++ = value;
}

static inline bool internalRamRange(const void *pointer, u32 byteCount)
{
    const uptr start = reinterpret_cast<uptr>(pointer);
    const uptr end = start + byteCount;
    if (end < start)
        return false;
    return (start >= 0x02000000u && end <= 0x02040000u) ||
           (start >= 0x03000000u && end <= 0x03008000u);
}

// SRAM-only PIC NOR primitives live in sram_stack_worker.S. Full-size SRAM
// invokes them through assembly wrappers that copy exactly one primitive at a
// time onto the current System stack; C++ owns no executable stack buffer.
extern u32 sramStackProgramChunk(u32 targetAddress, const u8 *source, u32 byteCount, u32 programCommand);
extern u32 sramStackEraseBlock(u32 blockAddress);
extern u32 gbashRunSramStackProgram(u32 targetAddress, const u8 *source, u32 byteCount, u32 programCommand);
extern u32 gbashRunSramStackErase(u32 blockAddress, u32 programCommand);

// ---------- RAM-resident Intel NOR worker ----------

struct HardwareState {
    u16 ime;
    u16 waitcnt;
    u16 dmaControl[4];
    u16 pausedDmaMask;
};

static inline bool dmaReadsGamePakRom(u32 sourceAddress)
{
    return sourceAddress >= 0x08000000u && sourceAddress < 0x0E000000u;
}

__attribute__((section(".ram_worker"), noinline, used))
static void ramEnterNorCritical(HardwareState *state)
{
    volatile u32 *dmaSource[4] = {
        reinterpret_cast<volatile u32 *>(0x040000B0u),
        reinterpret_cast<volatile u32 *>(0x040000BCu),
        reinterpret_cast<volatile u32 *>(0x040000C8u),
        reinterpret_cast<volatile u32 *>(0x040000D4u),
    };
    volatile u16 *dmaControl[4] = {
        reinterpret_cast<volatile u16 *>(0x040000BAu),
        reinterpret_cast<volatile u16 *>(0x040000C6u),
        reinterpret_cast<volatile u16 *>(0x040000D2u),
        reinterpret_cast<volatile u16 *>(0x040000DEu),
    };
    volatile u16 &ime = *reinterpret_cast<volatile u16 *>(0x04000208u);
    volatile u16 &waitcnt = *reinterpret_cast<volatile u16 *>(0x04000204u);

    state->ime = ime;
    state->waitcnt = waitcnt;
    state->pausedDmaMask = 0u;
    ime = 0;

    // Only autonomous DMA that reads Game Pak ROM can collide with NOR
    // command/status mode. During an SRAM hotkey commit the outer guard has
    // already stopped all timers. Leave enabled DMA1/2 SPECIAL channels alone
    // in that case so DirectSound keeps its hidden current-source latch.
    const bool sramHotkeyCritical =
        *reinterpret_cast<const volatile u32 *>(0x0203BFCCu) != 0u;
    for (u32 index = 0; index < 4; ++index) {
        state->dmaControl[index] = *dmaControl[index];
        if ((state->dmaControl[index] & 0x8000u) == 0u || !dmaReadsGamePakRom(*dmaSource[index]))
            continue;
        const bool directSound = sramHotkeyCritical && (index == 1u || index == 2u) &&
            (state->dmaControl[index] & 0x3000u) == 0x3000u;
        if (directSound)
            continue;
        *dmaControl[index] = static_cast<u16>(state->dmaControl[index] & 0x7FFFu);
        state->pausedDmaMask = static_cast<u16>(state->pausedDmaMask | static_cast<u16>(1u << index));
    }
    waitcnt = static_cast<u16>((state->waitcnt & ~0x401Cu) | 0x000Cu);
}

__attribute__((section(".ram_worker"), noinline, used))
static void ramLeaveNorCritical(const HardwareState *state)
{
    volatile u16 *dmaControl[4] = {
        reinterpret_cast<volatile u16 *>(0x040000BAu),
        reinterpret_cast<volatile u16 *>(0x040000C6u),
        reinterpret_cast<volatile u16 *>(0x040000D2u),
        reinterpret_cast<volatile u16 *>(0x040000DEu),
    };
    volatile u16 &ime = *reinterpret_cast<volatile u16 *>(0x04000208u);
    volatile u16 &waitcnt = *reinterpret_cast<volatile u16 *>(0x04000204u);

    waitcnt = state->waitcnt;
    for (u32 index = 0; index < 4; ++index) {
        if ((state->pausedDmaMask & static_cast<u16>(1u << index)) != 0u)
            *dmaControl[index] = state->dmaControl[index];
    }
    ime = state->ime;
}

__attribute__((section(".ram_worker"), noinline, used))
static u32 ramProgramHalfword(volatile u16 *target, u16 value, u16 programSelector)
{
    const u16 before = *target;
    if (before == value)
        return 0;
    if ((before & value) != value)
        return 0xE101u;

    // MX26L6420 AMD/Fujitsu unlock-cycle word program. The CPU addresses are
    // byte addresses: FlashGBX's AGB 0xAAA/0x555 command offsets become
    // 0x08000AAA / 0x08000554 on the x16 Game Pak bus. Never issue chip erase.
    if (programSelector == 0x00A0u) {
        volatile u16 *const unlock1 = reinterpret_cast<volatile u16 *>(0x08000AAAu);
        volatile u16 *const unlock2 = reinterpret_cast<volatile u16 *>(0x08000554u);
        for (u32 attempt = 0u; attempt < 3u; ++attempt) {
            *unlock1 = 0x00AAu;
            *unlock2 = 0x0055u;
            *unlock1 = 0x00A0u;
            *target = value;

            u32 timeout = 0x200000u;
            bool ready = false;
            while (timeout--) {
                const u16 first = *target;
                const u16 second = *target;
                if (((first ^ second) & 0x0040u) == 0u) {
                    ready = true;
                    break;
                }
                if ((second & 0x0020u) != 0u) {
                    const u16 third = *target;
                    const u16 fourth = *target;
                    if (((third ^ fourth) & 0x0040u) == 0u) {
                        ready = true;
                        break;
                    }
                    break;
                }
            }
            *unlock1 = 0x00F0u;
            for (volatile u32 settle = 0u; settle < 64u; ++settle) { }
            if (ready && *target == value)
                return 0u;
        }
        return 0xE1A3u;
    }

    // M6MGD137 target-relative Intel sequence mirrored from the supplied
    // FlashGBX profile: 70/ready, 40/data, status completion, then 50/FF array
    // normalization. The distinct 0x0140 selector keeps the frozen M6/M6M
    // byte shape below untouched even though both physically program with 40h.
    if (programSelector == 0x0140u) {
        for (u32 attempt = 0u; attempt < 3u; ++attempt) {
            *target = 0x0070u;
            u32 timeout = 0x20000u;
            u16 status = 0u;
            while (timeout--) {
                status = *target;
                if ((status & 0x0080u) != 0u)
                    break;
            }
            if ((status & 0x0080u) == 0u) {
                *target = 0x0050u;
                *target = 0x00FFu;
                continue;
            }
            if ((status & 0x0018u) != 0u) {
                *target = 0x0050u;
                *target = 0x00FFu;
                continue;
            }

            *target = 0x0040u;
            *target = value;
            timeout = 0x200000u;
            status = 0u;
            while (timeout--) {
                status = *target;
                if ((status & 0x0080u) != 0u)
                    break;
            }
            const bool statusOk = (status & 0x0080u) != 0u && (status & 0x0018u) == 0u;
            *target = 0x0050u;
            *target = 0x00FFu;
            for (volatile u32 settle = 0u; settle < 64u; ++settle) { }
            if (statusOk && *target == value)
                return 0u;
        }
        return 0xE143u;
    }

    // Hardware-proven M6/M6M path. Keep this transaction byte-for-byte shaped
    // as the existing 0x40 implementation; M36 uses the separate E8 backend.
    for (u32 attempt = 0; attempt < 3u; ++attempt) {
        *target = 0x0050u;
        *target = 0x00FFu;
        *target = programSelector;
        *target = value;
        u32 timeout = 0x200u;
        while (timeout--) {
            const u16 status = *target;
            if (status == 0x0080u)
                break;
        }
        *target = 0x0050u;
        *target = 0x00FFu;
        for (volatile u32 settle = 0; settle < 64u; ++settle) { }
        if (*target == value)
            return 0;
    }
    return 0xE103u;
}

__attribute__((section(".ram_worker"), noinline, used))
static u32 ramM36UnlockBlock(u32 address)
{
    volatile u16 *const block = reinterpret_cast<volatile u16 *>(address & ~0x1FFFFu);
    *block = 0x0050u;
    *block = 0x0060u;
    *block = 0x00D0u;
    u32 timeout = 2000000u;
    u16 status = 0u;
    while (timeout--) {
        status = *block;
        if ((status & 0x0080u) != 0u)
            break;
    }
    if ((status & 0x0080u) == 0u) {
        *block = 0x00FFu;
        return 0xE151u;
    }
    if ((status & 0x003Au) != 0u) {
        *block = 0x0050u;
        *block = 0x00FFu;
        return 0xE152u;
    }
    *block = 0x00FFu;
    return 0u;
}

__attribute__((section(".ram_worker"), noinline, used))
static u32 ramM36ProgramLine32(u32 lineAddress, const u16 *words)
{
    volatile u16 *const target = reinterpret_cast<volatile u16 *>(lineAddress);
    volatile u16 &waitcnt = *reinterpret_cast<volatile u16 *>(0x04000204u);

    bool changed = false;
    for (u32 index = 0u; index < 16u; ++index) {
        const u16 before = target[index];
        const u16 desired = words[index];
        if ((before & desired) != desired)
            return 0xE101u;
        changed = changed || before != desired;
    }
    if (!changed)
        return 0u;

    *target = 0x0050u;
    *target = 0x00E8u;
    u32 timeout = 1000000u;
    u16 status = 0u;
    while (timeout--) {
        status = *target;
        if ((status & 0x0080u) != 0u)
            break;
    }
    if ((status & 0x0080u) == 0u) {
        *target = 0x00FFu;
        return 0xE153u;
    }
    if ((status & 0x003Au) != 0u) {
        *target = 0x0050u;
        *target = 0x00FFu;
        return 0xE154u;
    }

    *target = 15u; // 16 halfwords / 32 bytes
    waitcnt = 0x000Bu;
    for (u32 index = 0u; index < 16u; ++index) {
        target[index] = words[index];
        __asm__ volatile("nop" ::: "memory");
    }
    for (volatile u32 settle = 0u; settle < 64u; ++settle) { }
    waitcnt = 0x000Fu;
    *target = 0x00D0u;

    timeout = 2000000u;
    status = 0u;
    while (timeout--) {
        status = *target;
        if ((status & 0x0080u) != 0u)
            break;
    }
    if ((status & 0x0080u) == 0u) {
        *target = 0x00FFu;
        return 0xE155u;
    }
    if ((status & 0x003Au) != 0u) {
        *target = 0x0050u;
        *target = 0x00FFu;
        return 0xE156u;
    }
    *target = 0x00FFu;
    for (u32 index = 0u; index < 16u; ++index) {
        if (target[index] != words[index])
            return 0xE157u;
    }
    return 0u;
}

__attribute__((section(".ram_worker"), noinline, used))
static bool ramM36WorkerBankSeparated(u32 targetAddress, u32 byteCount, uptr workerAddress)
{
    constexpr u32 kRomBase = 0x08000000u;
    constexpr u32 kRomEnd = 0x0A000000u;
    constexpr u32 kRwwBankBytes = 0x00100000u;
    if (byteCount == 0u || targetAddress < kRomBase || targetAddress >= kRomEnd)
        return false;
    const u32 targetEnd = targetAddress + byteCount - 1u;
    if (targetEnd < targetAddress || targetEnd >= kRomEnd)
        return false;
    const uptr worker = workerAddress & ~uptr{1u};
    if (worker < kRomBase || worker >= kRomEnd)
        return false;
    const u32 workerBank = static_cast<u32>((worker - kRomBase) / kRwwBankBytes);
    const u32 firstTargetBank = (targetAddress - kRomBase) / kRwwBankBytes;
    const u32 lastTargetBank = (targetEnd - kRomBase) / kRwwBankBytes;
    return workerBank < firstTargetBank || workerBank > lastTargetBank;
}

__attribute__((section(".ram_worker"), noinline, used))
u32 ramProgramBytes(u32 targetAddress, const u8 *source, u32 byteCount, u32 programCommand)
{
    if (source == nullptr || !internalRamRange(source, byteCount))
        return 0xE10Bu;
    if (programCommand == 0x00E8u &&
        !ramM36WorkerBankSeparated(targetAddress, byteCount, reinterpret_cast<uptr>(&ramProgramBytes)))
        return 0xE15Bu;

    HardwareState state{};
    ramEnterNorCritical(&state);
    u32 result = 0u;

    if (programCommand == 0x00E8u) {
        // Hardware-proven M36L0 transaction discipline from the R37A runtime:
        // unlock each 128 KiB block, command/status WAITCNT=000F, 32-byte E8
        // payload WAITCNT=000B, D0 confirm, status error mask 003A, exact verify.
        volatile u16 &waitcnt = *reinterpret_cast<volatile u16 *>(0x04000204u);
        waitcnt = 0x000Fu;
        const u32 endAddress = targetAddress + byteCount;
        u32 lineAddress = targetAddress & ~31u;
        u32 unlockedBlock = 0xFFFFFFFFu;
        while (lineAddress < endAddress && result == 0u) {
            const u32 block = lineAddress & ~0x1FFFFu;
            if (block != unlockedBlock) {
                result = ramM36UnlockBlock(lineAddress);
                if (result != 0u)
                    break;
                unlockedBlock = block;
            }

            u16 words[16];
            volatile u16 *line = reinterpret_cast<volatile u16 *>(lineAddress);
            for (u32 index = 0u; index < 16u; ++index)
                words[index] = line[index];
            for (u32 byte = 0u; byte < 32u; ++byte) {
                const u32 address = lineAddress + byte;
                if (address < targetAddress || address >= endAddress)
                    continue;
                const u32 sourceOffset = address - targetAddress;
                const u32 wi = byte >> 1u;
                if ((byte & 1u) == 0u)
                    words[wi] = static_cast<u16>((words[wi] & 0xFF00u) | source[sourceOffset]);
                else
                    words[wi] = static_cast<u16>((words[wi] & 0x00FFu) | (static_cast<u16>(source[sourceOffset]) << 8u));
            }
            result = ramM36ProgramLine32(lineAddress, words);
            lineAddress += 32u;
        }
        *reinterpret_cast<volatile u16 *>(targetAddress & ~31u) = 0x00FFu;
        ramLeaveNorCritical(&state);
        return result;
    }

    const u32 firstHalfword = targetAddress & ~1u;
    const u32 endAddress = targetAddress + byteCount;
    for (u32 address = firstHalfword; address < endAddress; address += 2u) {
        volatile u16 *target = reinterpret_cast<volatile u16 *>(address);
        const u16 before = *target;
        u16 desired = before;
        if (address >= targetAddress && address < endAddress) {
            const u32 sourceOffset = address - targetAddress;
            desired = static_cast<u16>((desired & 0xFF00u) | source[sourceOffset]);
        }
        if (address + 1u >= targetAddress && address + 1u < endAddress) {
            const u32 sourceOffset = address + 1u - targetAddress;
            desired = static_cast<u16>((desired & 0x00FFu) | (static_cast<u16>(source[sourceOffset]) << 8u));
        }
        result = ramProgramHalfword(target, desired, static_cast<u16>(programCommand));
        if (result != 0u)
            break;
    }
    if (programCommand == 0x00A0u) {
        *reinterpret_cast<volatile u16 *>(0x08000AAAu) = 0x00F0u;
    } else {
        *reinterpret_cast<volatile u16 *>(targetAddress & ~1u) = 0x0050u;
        *reinterpret_cast<volatile u16 *>(targetAddress & ~1u) = 0x00FFu;
    }
    ramLeaveNorCritical(&state);
    return result;
}

__attribute__((section(".ram_worker"), noinline, used))
u32 ramCloneWithPatch(
    u32 targetAddress,
    u32 sourceAddress,
    const u8 *patchSource,
    u32 patchOffset,
    u32 patchLength,
    u32 totalBytes,
    u32 programCommand)
{
    if (patchLength != 0u && (patchSource == nullptr || !internalRamRange(patchSource, patchLength)))
        return 0xE11Bu;
    if (patchOffset > totalBytes || patchLength > totalBytes - patchOffset)
        return 0xE111u;
    if (programCommand == 0x00E8u &&
        !ramM36WorkerBankSeparated(targetAddress, totalBytes, reinterpret_cast<uptr>(&ramCloneWithPatch)))
        return 0xE15Cu;

    HardwareState state{};
    ramEnterNorCritical(&state);
    u32 result = 0u;

    if (programCommand == 0x00E8u) {
        volatile u16 &waitcnt = *reinterpret_cast<volatile u16 *>(0x04000204u);
        waitcnt = 0x000Fu;
        u32 unlockedBlock = 0xFFFFFFFFu;
        for (u32 lineOffset = 0u; lineOffset < totalBytes && result == 0u; lineOffset += 32u) {
            const u32 lineAddress = targetAddress + lineOffset;
            const u32 block = lineAddress & ~0x1FFFFu;
            if (block != unlockedBlock) {
                result = ramM36UnlockBlock(lineAddress);
                if (result != 0u)
                    break;
                unlockedBlock = block;
            }
            u16 words[16];
            for (u32 wi = 0u; wi < 16u; ++wi) {
                const u32 offset = lineOffset + wi * 2u;
                u8 lo = 0xFFu, hi = 0xFFu;
                if (sourceAddress != kSyntheticErasedSource && offset < totalBytes) {
                    const auto *source = reinterpret_cast<const volatile u8 *>(sourceAddress + offset);
                    lo = source[0];
                    if (offset + 1u < totalBytes)
                        hi = source[1];
                }
                if (offset >= patchOffset && offset < patchOffset + patchLength)
                    lo = patchSource[offset - patchOffset];
                if (offset + 1u >= patchOffset && offset + 1u < patchOffset + patchLength)
                    hi = patchSource[offset + 1u - patchOffset];
                words[wi] = static_cast<u16>(lo | (static_cast<u16>(hi) << 8u));
            }
            result = ramM36ProgramLine32(lineAddress, words);
        }
        *reinterpret_cast<volatile u16 *>(targetAddress) = 0x00FFu;
        ramLeaveNorCritical(&state);
        return result;
    }

    for (u32 offset = 0; offset < totalBytes; offset += 2u) {
        u8 low = 0xFFu;
        u8 high = 0xFFu;
        if (sourceAddress != kSyntheticErasedSource) {
            const auto *source = reinterpret_cast<const volatile u8 *>(sourceAddress + offset);
            low = source[0];
            if (offset + 1u < totalBytes)
                high = source[1];
        }
        if (offset >= patchOffset && offset < patchOffset + patchLength)
            low = patchSource[offset - patchOffset];
        if (offset + 1u >= patchOffset && offset + 1u < patchOffset + patchLength)
            high = patchSource[offset + 1u - patchOffset];
        const u16 desired = static_cast<u16>(low) | (static_cast<u16>(high) << 8u);
        if (desired != 0xFFFFu) {
            result = ramProgramHalfword(reinterpret_cast<volatile u16 *>(targetAddress + offset), desired, static_cast<u16>(programCommand));
            if (result != 0u)
                break;
        }
    }
    if (programCommand == 0x00A0u) {
        *reinterpret_cast<volatile u16 *>(0x08000AAAu) = 0x00F0u;
    } else {
        *reinterpret_cast<volatile u16 *>(targetAddress) = 0x0050u;
        *reinterpret_cast<volatile u16 *>(targetAddress) = 0x00FFu;
    }
    ramLeaveNorCritical(&state);
    return result;
}

__attribute__((section(".ram_worker"), noinline, used))
u32 ramEraseBlock(u32 blockAddress, u32 programCommand)
{
    if (programCommand == 0x00E8u &&
        !ramM36WorkerBankSeparated(blockAddress, 0x20000u, reinterpret_cast<uptr>(&ramEraseBlock)))
        return 0xE15Du;
    HardwareState state{};
    ramEnterNorCritical(&state);
    volatile u16 *target = reinterpret_cast<volatile u16 *>(blockAddress);

    // MX26L6420 has no sector erase command. Fail before issuing any write;
    // storage planning is expected to select only program-only layouts.
    if (programCommand == 0x00A0u) {
        ramLeaveNorCritical(&state);
        return 0xE2A0u;
    }

    // M6MGD137 target-relative erase from the supplied FlashGBX profile. The
    // 0x7F0000..0x80FFFF boot-sector fan-out is excluded by host geometry, so
    // every runtime erase target here is a complete 64 KiB main sector.
    if (programCommand == 0x0140u) {
        *target = 0x0060u;
        *target = 0x00D0u;
        *target = 0x0020u;
        *target = 0x00D0u;
        u32 timeout = 12000000u;
        u16 status = 0u;
        while (timeout--) {
            status = *target;
            if ((status & 0x0080u) != 0u)
                break;
        }
        u32 result = 0u;
        if ((status & 0x0080u) == 0u)
            result = 0xE241u;
        else if ((status & 0x0038u) != 0u)
            result = 0xE242u;
        *target = 0x0050u;
        *target = 0x00FFu;
        ramLeaveNorCritical(&state);
        return result;
    }

    if (programCommand == 0x00E8u) {
        volatile u16 &waitcnt = *reinterpret_cast<volatile u16 *>(0x04000204u);
        waitcnt = 0x000Fu;
        u32 result = ramM36UnlockBlock(blockAddress);
        if (result == 0u) {
            *target = 0x0050u;
            *target = 0x0020u;
            *target = 0x00D0u;
            u32 timeout = 12000000u;
            u16 status = 0u;
            while (timeout--) {
                status = *target;
                if ((status & 0x0080u) != 0u)
                    break;
            }
            if ((status & 0x0080u) == 0u)
                result = 0xE251u;
            else if ((status & 0x003Au) != 0u)
                result = 0xE252u;
            *target = 0x00FFu;
            if (result == 0u)
                result = ramM36UnlockBlock(blockAddress);
        }
        *target = 0x00FFu;
        ramLeaveNorCritical(&state);
        return result;
    }

    *target = 0x0050u;
    *target = 0x00FFu;
    *target = 0x0060u;
    *target = 0x00D0u;
    *target = 0x0020u;
    *target = 0x00D0u;
    *target = 0x0070u;
    u32 result = 0xE201u;
    for (u32 attempt = 0; attempt < 100u; ++attempt) {
        for (volatile u32 delay = 0; delay < 0x40000u; ++delay) { }
        const u16 status = *target;
        if (status == 0x0080u) {
            result = 0u;
            break;
        }
        if ((status & 0x0080u) != 0u) {
            result = 0xE203u;
            break;
        }
    }
    *target = 0x0050u;
    *target = 0x00FFu;
    ramLeaveNorCritical(&state);
    return result;
}

// ---------- Relocated-worker dispatch ----------

using ProgramBytesWorker = u32 (*)(u32, const u8 *, u32, u32);
using CloneWithPatchWorker = u32 (*)(u32, u32, const u8 *, u32, u32, u32, u32);
using EraseBlockWorker = u32 (*)(u32, u32);

static inline bool usesM36E8Backend()
{
    return gRuntimeConfig.norProgramCommand == 0x00E8u;
}

static inline void copyRamWorker()
{
    // M36 executes the worker directly from a plan-separated 1 MiB RWW ROM
    // bank. Other Intel backends retain the established EWRAM relocation.
    if (usesM36E8Backend())
        return;
    const u32 workerSize = static_cast<u32>(reinterpret_cast<uptr>(__ram_worker_end) - reinterpret_cast<uptr>(__ram_worker_start));
    copyBytes(reinterpret_cast<void *>(kRamWorkerAddress), __ram_worker_start, workerSize);
}

static inline ProgramBytesWorker programBytesWorker()
{
    if (usesM36E8Backend())
        return &ramProgramBytes;
    const uptr offset = (reinterpret_cast<uptr>(&ramProgramBytes) & ~1u) - reinterpret_cast<uptr>(__ram_worker_start);
    return reinterpret_cast<ProgramBytesWorker>((kRamWorkerAddress + offset) | 1u);
}

static inline CloneWithPatchWorker cloneWithPatchWorker()
{
    if (usesM36E8Backend())
        return &ramCloneWithPatch;
    const uptr offset = (reinterpret_cast<uptr>(&ramCloneWithPatch) & ~1u) - reinterpret_cast<uptr>(__ram_worker_start);
    return reinterpret_cast<CloneWithPatchWorker>((kRamWorkerAddress + offset) | 1u);
}

static inline EraseBlockWorker eraseBlockWorker()
{
    if (usesM36E8Backend())
        return &ramEraseBlock;
    const uptr offset = (reinterpret_cast<uptr>(&ramEraseBlock) & ~1u) - reinterpret_cast<uptr>(__ram_worker_start);
    return reinterpret_cast<EraseBlockWorker>((kRamWorkerAddress + offset) | 1u);
}

extern "C" __attribute__((section(".text.runtime"), used))
u32 gbashRunM36RwwProgram(u32 targetAddress, const u8 *source, u32 byteCount, u32 programCommand)
{
    if (programCommand != 0x00E8u)
        return 0xE15Eu;
    return ramProgramBytes(targetAddress, source, byteCount, programCommand);
}

extern "C" __attribute__((section(".text.runtime"), used))
u32 gbashRunM36RwwErase(u32 blockAddress, u32 programCommand)
{
    if (programCommand != 0x00E8u)
        return 0xE15Fu;
    return ramEraseBlock(blockAddress, programCommand);
}

// ---------- FLASH append-only versioned-slot storage ----------

static inline bool validUnit(u32 logicalUnit)
{
    return logicalUnit < gRuntimeConfig.logicalUnitCount;
}

static inline bool validPhysicalBlock(u32 blockIndex)
{
    return blockIndex < gRuntimeConfig.physicalStorageBlockCount && blockIndex < 64u;
}

static inline u32 storageBlockAddressByIndex(u32 blockIndex)
{
    return validPhysicalBlock(blockIndex) ? gRuntimeConfig.storageBlockAddresses[blockIndex] : 0u;
}

static inline u32 primaryBlockIndex(u32 logicalUnit)
{
    return logicalUnit;
}

static inline u32 slotAddressByBlock(u32 blockIndex, u32 slot)
{
    return storageBlockAddressByIndex(blockIndex) + slot * gRuntimeConfig.logicalUnitSizeBytes;
}

static inline u32 headAddressByBlock(u32 blockIndex)
{
    return storageBlockAddressByIndex(blockIndex) + gRuntimeConfig.flashSlotMetadataOffsetBytes;
}

static inline u32 markerAddressByBlock(u32 blockIndex, u32 slot)
{
    return headAddressByBlock(blockIndex) + 2u + slot * 2u;
}

constexpr u32 kSpillOwnerOffsetFromMetadata = 0x40u;
constexpr u16 kSpillOwnerBase = 0xB000u;

static inline u32 spillOwnerAddress(u32 blockIndex)
{
    return headAddressByBlock(blockIndex) + kSpillOwnerOffsetFromMetadata;
}

static inline u16 spillOwnerValue(u32 logicalUnit)
{
    return static_cast<u16>(kSpillOwnerBase | (logicalUnit & 0x003Fu));
}

static inline bool spillOwnedBy(u32 blockIndex, u32 logicalUnit)
{
    if (blockIndex < gRuntimeConfig.logicalUnitCount || !validPhysicalBlock(blockIndex))
        return false;
    return *reinterpret_cast<const volatile u16 *>(spillOwnerAddress(blockIndex)) == spillOwnerValue(logicalUnit);
}

struct LatestSlot {
    u32 address;
    u16 tag;
    int slot;
    u32 blockIndex;
};

static LatestSlot latestSlotInBlock(u32 blockIndex)
{
    LatestSlot result{0u, kSlotBlankTag, -1, blockIndex};
    if (!validPhysicalBlock(blockIndex))
        return result;

    const u16 head = *reinterpret_cast<const volatile u16 *>(headAddressByBlock(blockIndex));
    for (int slot = static_cast<int>(gRuntimeConfig.flashSlotsPerBlock) - 1; slot >= 0; --slot) {
        const u16 bit = static_cast<u16>(1u << static_cast<u32>(slot));
        if ((head & bit) != 0u)
            continue;
        const u16 tag = *reinterpret_cast<const volatile u16 *>(markerAddressByBlock(blockIndex, static_cast<u32>(slot)));
        if (tag == kSlotDataTag || tag == kSlotBlankTag) {
            result.address = slotAddressByBlock(blockIndex, static_cast<u32>(slot));
            result.tag = tag;
            result.slot = slot;
            return result;
        }
    }
    return result;
}

static LatestSlot latestSlot(u32 logicalUnit)
{
    LatestSlot none{0u, kSlotBlankTag, -1, 0xFFFFFFFFu};
    if (!validUnit(logicalUnit))
        return none;

    // Spill blocks are claimed monotonically from low to high physical index,
    // so the highest matching block is the newest generation domain. A block
    // whose owner was committed before a power cut but has no committed slot
    // is skipped, allowing the previous block to remain authoritative.
    for (int block = static_cast<int>(gRuntimeConfig.physicalStorageBlockCount) - 1;
         block >= static_cast<int>(gRuntimeConfig.logicalUnitCount); --block) {
        const u32 blockIndex = static_cast<u32>(block);
        if (!spillOwnedBy(blockIndex, logicalUnit))
            continue;
        const LatestSlot candidate = latestSlotInBlock(blockIndex);
        if (candidate.slot >= 0)
            return candidate;
    }
    return latestSlotInBlock(primaryBlockIndex(logicalUnit));
}

static bool rangeIsErased(u32 address, u32 byteCount)
{
    const auto *source = reinterpret_cast<const volatile u16 *>(address);
    const u32 halfwords = byteCount >> 1u;
    for (u32 index = 0; index < halfwords; ++index) {
        if (source[index] != 0xFFFFu)
            return false;
    }
    if (byteCount & 1u)
        return *reinterpret_cast<const volatile u8 *>(address + byteCount - 1u) == 0xFFu;
    return true;
}

static bool volatileRangeMatches(u32 address, const u8 *source, u32 byteCount);

static u32 programMarkerValue(u32 address, u16 value)
{
    copyRamWorker();
    const u16 marker = value;
    const u32 result = programBytesWorker()(address, reinterpret_cast<const u8 *>(&marker), 2u, gRuntimeConfig.norProgramCommand);
    if (result != 0u)
        return result;
    return *reinterpret_cast<const volatile u16 *>(address) == value ? 0u : 0xE30Bu;
}

static inline u32 flashBreadcrumbAddress(u32 blockIndex, u32 slot)
{
    return headAddressByBlock(blockIndex) + kFlashBreadcrumbOffsetFromMetadata + slot * kFlashBreadcrumbStride;
}

static u32 beginFlashBreadcrumb(u32 blockIndex, u32 slot, u16 operation, u16 subject)
{
    if (!validPhysicalBlock(blockIndex) || slot >= gRuntimeConfig.flashSlotsPerBlock || slot >= 16u)
        return 0xE34Au;
    const u32 address = flashBreadcrumbAddress(blockIndex, slot);
    if (!rangeIsErased(address, sizeof(BreadcrumbRecord)))
        return 0xE34Bu;
    BreadcrumbRecord record{kBreadcrumbMagic, operation, subject, kBreadcrumbStageEnter};
    copyRamWorker();
    const u32 result = programBytesWorker()(address, reinterpret_cast<const u8 *>(&record), sizeof(record), gRuntimeConfig.norProgramCommand);
    if (result != 0u)
        return result;
    return volatileRangeMatches(address, reinterpret_cast<const u8 *>(&record), sizeof(record)) ? 0u : 0xE34Cu;
}

static u32 advanceFlashBreadcrumb(u32 blockIndex, u32 slot, u16 stageBits)
{
    return programMarkerValue(flashBreadcrumbAddress(blockIndex, slot) + 6u, stageBits);
}

static u32 publishSlot(u32 blockIndex, u32 slot)
{
    if (!validPhysicalBlock(blockIndex) || slot >= gRuntimeConfig.flashSlotsPerBlock || slot >= 16u)
        return 0xE30Du;
    const u32 address = headAddressByBlock(blockIndex);
    const u16 before = *reinterpret_cast<const volatile u16 *>(address);
    const u16 after = static_cast<u16>(before & ~static_cast<u16>(1u << slot));
    if (before == after)
        return 0u;
    return programMarkerValue(address, after);
}

static int findVirginSlotInBlock(u32 blockIndex)
{
    if (!validPhysicalBlock(blockIndex))
        return -1;
    const u16 head = *reinterpret_cast<const volatile u16 *>(headAddressByBlock(blockIndex));
    for (u32 slot = 0; slot < gRuntimeConfig.flashSlotsPerBlock; ++slot) {
        const u16 bit = static_cast<u16>(1u << slot);
        if ((head & bit) == 0u)
            continue;

        const u32 marker = markerAddressByBlock(blockIndex, slot);
        const u16 tag = *reinterpret_cast<const volatile u16 *>(marker);
        if (tag == kSlotVirginTag && rangeIsErased(slotAddressByBlock(blockIndex, slot), gRuntimeConfig.logicalUnitSizeBytes))
            return static_cast<int>(slot);

        // Recover/quarantine a torn slot before moving forward.
        if (tag == kSlotVirginTag) {
            if (programMarkerValue(marker, kSlotDeadTag) != 0u)
                return -1;
        }
        if (publishSlot(blockIndex, slot) != 0u)
            return -1;
    }
    return -1;
}

struct FreeSlot {
    u32 blockIndex;
    int slot;
};

static int newestOwnedSpill(u32 logicalUnit)
{
    for (int block = static_cast<int>(gRuntimeConfig.physicalStorageBlockCount) - 1;
         block >= static_cast<int>(gRuntimeConfig.logicalUnitCount); --block) {
        if (spillOwnedBy(static_cast<u32>(block), logicalUnit))
            return block;
    }
    return -1;
}

static int claimSpillBlock(u32 logicalUnit)
{
    for (u32 blockIndex = gRuntimeConfig.logicalUnitCount;
         blockIndex < gRuntimeConfig.physicalStorageBlockCount; ++blockIndex) {
        const u32 ownerAddress = spillOwnerAddress(blockIndex);
        const u16 owner = *reinterpret_cast<const volatile u16 *>(ownerAddress);
        if (owner != 0xFFFFu)
            continue;
        if (programMarkerValue(ownerAddress, spillOwnerValue(logicalUnit)) != 0u)
            return -1;
        return static_cast<int>(blockIndex);
    }
    return -1;
}

static FreeSlot findVirginSlot(u32 logicalUnit)
{
    if (!validUnit(logicalUnit))
        return FreeSlot{0xFFFFFFFFu, -1};

    const int spill = newestOwnedSpill(logicalUnit);
    if (spill >= 0) {
        const int slot = findVirginSlotInBlock(static_cast<u32>(spill));
        if (slot >= 0)
            return FreeSlot{static_cast<u32>(spill), slot};
    } else {
        const u32 primary = primaryBlockIndex(logicalUnit);
        const int slot = findVirginSlotInBlock(primary);
        if (slot >= 0)
            return FreeSlot{primary, slot};
    }

    const int claimed = claimSpillBlock(logicalUnit);
    if (claimed < 0)
        return FreeSlot{0xFFFFFFFFu, -1};
    const int slot = findVirginSlotInBlock(static_cast<u32>(claimed));
    return FreeSlot{static_cast<u32>(claimed), slot};
}

static bool unitBytesEqual(u32 logicalUnit, u32 offset, const u8 *source, u32 byteCount);
static u32 verifyUnitBytes(u32 logicalUnit, u32 offset, const u8 *source, u32 byteCount);

static u32 commitSlotWithBreadcrumb(u32 blockIndex, u32 slot, u16 tag)
{
    u32 result = programMarkerValue(markerAddressByBlock(blockIndex, slot), tag);
    if (result != 0u)
        return result;
    result = advanceFlashBreadcrumb(blockIndex, slot, kBreadcrumbStageMetadataWritten);
    if (result != 0u)
        return result;
    result = publishSlot(blockIndex, slot);
    if (result != 0u)
        return result;
    return advanceFlashBreadcrumb(blockIndex, slot, kBreadcrumbStageCommitted);
}

static u32 appendSnapshot(u32 logicalUnit, const u8 *source)
{
    if (!validUnit(logicalUnit) || source == nullptr || !internalRamRange(source, gRuntimeConfig.logicalUnitSizeBytes))
        return 1u;
    if (unitBytesEqual(logicalUnit, 0u, source, gRuntimeConfig.logicalUnitSizeBytes))
        return 0u;
    const FreeSlot freeSlot = findVirginSlot(logicalUnit);
    if (freeSlot.slot < 0)
        return 0xE301u;
    u32 result = beginFlashBreadcrumb(freeSlot.blockIndex, static_cast<u32>(freeSlot.slot),
        kBreadcrumbOpFlashSnapshot, static_cast<u16>(logicalUnit));
    if (result != 0u)
        return result;

    copyRamWorker();
    const u32 target = slotAddressByBlock(freeSlot.blockIndex, static_cast<u32>(freeSlot.slot));
    result = programBytesWorker()(target, source, gRuntimeConfig.logicalUnitSizeBytes, gRuntimeConfig.norProgramCommand);
    if (result != 0u)
        return result;
    result = advanceFlashBreadcrumb(freeSlot.blockIndex, static_cast<u32>(freeSlot.slot), kBreadcrumbStageDataWritten);
    if (result != 0u)
        return result;

    const auto *stored = reinterpret_cast<const volatile u8 *>(target);
    for (u32 index = 0; index < gRuntimeConfig.logicalUnitSizeBytes; ++index) {
        if (stored[index] != source[index])
            return 0xE302u;
    }
    result = advanceFlashBreadcrumb(freeSlot.blockIndex, static_cast<u32>(freeSlot.slot), kBreadcrumbStageDataVerified);
    if (result != 0u)
        return result;
    result = commitSlotWithBreadcrumb(freeSlot.blockIndex, static_cast<u32>(freeSlot.slot), kSlotDataTag);
    if (result != 0u)
        return result;
    return advanceFlashBreadcrumb(freeSlot.blockIndex, static_cast<u32>(freeSlot.slot), kBreadcrumbStageFinalVerified);
}

static u32 appendPatchedSnapshot(u32 logicalUnit, u32 patchOffset, const u8 *patch, u32 patchLength)
{
    if (!validUnit(logicalUnit) || patch == nullptr || patchOffset > gRuntimeConfig.logicalUnitSizeBytes ||
        patchLength > gRuntimeConfig.logicalUnitSizeBytes - patchOffset)
        return 1u;
    if (unitBytesEqual(logicalUnit, patchOffset, patch, patchLength))
        return 0u;
    const FreeSlot freeSlot = findVirginSlot(logicalUnit);
    if (freeSlot.slot < 0)
        return 0xE311u;
    u32 result = beginFlashBreadcrumb(freeSlot.blockIndex, static_cast<u32>(freeSlot.slot),
        kBreadcrumbOpFlashPatch, static_cast<u16>(logicalUnit));
    if (result != 0u)
        return result;

    const LatestSlot latest = latestSlot(logicalUnit);
    const u32 sourceAddress = latest.slot >= 0 && latest.tag == kSlotDataTag ? latest.address : kSyntheticErasedSource;
    const u32 targetAddress = slotAddressByBlock(freeSlot.blockIndex, static_cast<u32>(freeSlot.slot));

    copyRamWorker();
    result = cloneWithPatchWorker()(
        targetAddress,
        sourceAddress,
        patch,
        patchOffset,
        patchLength,
        gRuntimeConfig.logicalUnitSizeBytes,
        gRuntimeConfig.norProgramCommand);
    if (result != 0u)
        return result;
    result = advanceFlashBreadcrumb(freeSlot.blockIndex, static_cast<u32>(freeSlot.slot), kBreadcrumbStageDataWritten);
    if (result != 0u)
        return result;
    result = commitSlotWithBreadcrumb(freeSlot.blockIndex, static_cast<u32>(freeSlot.slot), kSlotDataTag);
    if (result != 0u)
        return result;
    return advanceFlashBreadcrumb(freeSlot.blockIndex, static_cast<u32>(freeSlot.slot), kBreadcrumbStageFinalVerified);
}

static u32 appendBlank(u32 logicalUnit)
{
    if (!validUnit(logicalUnit))
        return 1u;
    const LatestSlot latest = latestSlot(logicalUnit);
    if (latest.slot < 0 || latest.tag == kSlotBlankTag)
        return 0u;
    const FreeSlot freeSlot = findVirginSlot(logicalUnit);
    if (freeSlot.slot < 0)
        return 0xE321u;
    u32 result = beginFlashBreadcrumb(freeSlot.blockIndex, static_cast<u32>(freeSlot.slot),
        kBreadcrumbOpFlashBlank, static_cast<u16>(logicalUnit));
    if (result != 0u)
        return result;
    result = commitSlotWithBreadcrumb(freeSlot.blockIndex, static_cast<u32>(freeSlot.slot), kSlotBlankTag);
    if (result != 0u)
        return result;
    return advanceFlashBreadcrumb(freeSlot.blockIndex, static_cast<u32>(freeSlot.slot), kBreadcrumbStageFinalVerified);
}

static void readUnitBytes(u32 logicalUnit, u32 offset, void *destination, u32 byteCount)
{
    auto *out = static_cast<u8 *>(destination);
    const LatestSlot latest = latestSlot(logicalUnit);
    if (latest.slot < 0 || latest.tag == kSlotBlankTag) {
        fillBytes(out, 0xFFu, byteCount);
        return;
    }
    copyBytes(out, reinterpret_cast<const void *>(latest.address + offset), byteCount);
}

static bool unitBytesEqual(u32 logicalUnit, u32 offset, const u8 *source, u32 byteCount)
{
    if (!validUnit(logicalUnit) || source == nullptr ||
        offset > gRuntimeConfig.logicalUnitSizeBytes ||
        byteCount > gRuntimeConfig.logicalUnitSizeBytes - offset)
        return false;

    const LatestSlot latest = latestSlot(logicalUnit);
    if (latest.slot < 0 || latest.tag == kSlotBlankTag) {
        for (u32 index = 0; index < byteCount; ++index) {
            if (source[index] != 0xFFu)
                return false;
        }
        return true;
    }

    const auto *stored = reinterpret_cast<const volatile u8 *>(latest.address + offset);
    for (u32 index = 0; index < byteCount; ++index) {
        if (stored[index] != source[index])
            return false;
    }
    return true;
}

static u32 verifyUnitBytes(u32 logicalUnit, u32 offset, const u8 *source, u32 byteCount)
{
    if (!validUnit(logicalUnit) || source == nullptr ||
        offset > gRuntimeConfig.logicalUnitSizeBytes ||
        byteCount > gRuntimeConfig.logicalUnitSizeBytes - offset)
        return 1u;

    const LatestSlot latest = latestSlot(logicalUnit);
    const u32 bankLocalUnit = gRuntimeConfig.unitsPerBank != 0u
        ? logicalUnit & static_cast<u32>(gRuntimeConfig.unitsPerBank - 1u)
        : logicalUnit;
    const u32 stockBase = kGamePakSaveWindow + bankLocalUnit * gRuntimeConfig.logicalUnitSizeBytes + offset;

    if (latest.slot < 0 || latest.tag == kSlotBlankTag) {
        for (u32 index = 0; index < byteCount; ++index) {
            if (source[index] != 0xFFu)
                return stockBase + index;
        }
        return 0u;
    }

    const auto *stored = reinterpret_cast<const volatile u8 *>(latest.address + offset);
    for (u32 index = 0; index < byteCount; ++index) {
        if (stored[index] != source[index])
            return stockBase + index;
    }
    return 0u;
}

// ---------- FLASH adapter ----------

static inline void selectBank(u32 bank)
{
    // Single-bank FLASH512 has no bank-selection state. Avoid touching the
    // compatibility EWRAM byte at all so the generic FLASH512 route does not
    // claim game RAM it does not need. Multi-bank FLASH keeps the established
    // state byte for Nintendo bank-switch semantics.
    if (gRuntimeConfig.bankCount <= 1u)
        return;
    reg8(gRuntimeConfig.currentBankStateAddress) = static_cast<u8>(bank < gRuntimeConfig.bankCount ? bank : 0u);
}

static inline u32 selectedBank()
{
    if (gRuntimeConfig.bankCount <= 1u)
        return 0u;
    const u32 bank = reg8(gRuntimeConfig.currentBankStateAddress);
    return bank < gRuntimeConfig.bankCount ? bank : 0u;
}

static inline u32 bankUnitIndex(u32 bank, u32 bankOffset)
{
    return bank * gRuntimeConfig.unitsPerBank + (bankOffset >> gRuntimeConfig.logicalUnitShift);
}

static inline u32 unitOffset(u32 bankOffset)
{
    return bankOffset & gRuntimeConfig.logicalUnitOffsetMask;
}

static inline void selectBankForLogicalUnit(u32 logicalUnit)
{
    if (gRuntimeConfig.bankCount <= 1u || gRuntimeConfig.unitsPerBank == 0u) {
        selectBank(0u);
        return;
    }
    const u32 bank = logicalUnit >> gRuntimeConfig.bankSelectShift;
    selectBank(bank);
}

static inline void installForcedSetupProfile()
{
    if (gRuntimeConfig.forcedSetupProfileAddress == 0u)
        return;
    const auto *profile = reinterpret_cast<const volatile u32 *>(gRuntimeConfig.forcedSetupProfileAddress);
    *reinterpret_cast<volatile u32 *>(gRuntimeConfig.globalProgramSectorPointer) = profile[0];
    *reinterpret_cast<volatile u32 *>(gRuntimeConfig.globalEraseChipPointer) = profile[1];
    *reinterpret_cast<volatile u32 *>(gRuntimeConfig.globalEraseSectorPointer) = profile[2];
    *reinterpret_cast<volatile u32 *>(gRuntimeConfig.globalWaitForWritePointer) = profile[3];
    *reinterpret_cast<volatile u32 *>(gRuntimeConfig.globalMaxTimePointer) = profile[4];
    *reinterpret_cast<volatile u32 *>(gRuntimeConfig.globalGeometryPointer) = gRuntimeConfig.forcedSetupProfileAddress + 0x14u;
}

__attribute__((section(".text.runtime"), used))
u32 gbashSwitchFlashBank(u32 bank)
{
    if (bank >= gRuntimeConfig.bankCount)
        return 1u;
    selectBank(bank);
    return 0u;
}

__attribute__((section(".text.runtime"), used))
u32 gbashReadFlashId()
{
    // Stock IdentifyFlash starts from bank 0.  Keep our private virtual bank
    // state synchronized without touching Nintendo's IWRAM globals.
    selectBank(0u);
    return gRuntimeConfig.forcedFlashId;
}

__attribute__((section(".text.runtime"), used))
u32 gbashIdentifyFlash()
{
    installForcedSetupProfile();
    selectBank(0);
    return 0u;
}

__attribute__((section(".text.runtime"), used))
u32 gbashReadFlashByteHelper(const volatile u8 *stockFlashAddress)
{
    if (stockFlashAddress == nullptr)
        return 0xFFu;
    const uptr address = reinterpret_cast<uptr>(stockFlashAddress);
    if (address < kGamePakSaveWindow || address >= kGamePakSaveWindow + gRuntimeConfig.logicalBankSizeBytes)
        return 0xFFu;
    const u32 bankOffset = address - kGamePakSaveWindow;
    const u32 unit = bankUnitIndex(selectedBank(), bankOffset);
    u8 value = 0xFFu;
    readUnitBytes(unit, unitOffset(bankOffset), &value, 1u);
    return value;
}

__attribute__((section(".text.runtime"), used))
u32 gbashReadFlashCore(const volatile u8 *stockSource, void *destination, u32 byteCount)
{
    if (stockSource == nullptr || destination == nullptr)
        return 1u;
    const uptr address = reinterpret_cast<uptr>(stockSource);
    if (address < kGamePakSaveWindow || address + byteCount > kGamePakSaveWindow + gRuntimeConfig.logicalBankSizeBytes)
        return 1u;

    u32 bankOffset = address - kGamePakSaveWindow;
    auto *out = static_cast<u8 *>(destination);
    while (byteCount != 0u) {
        const u32 unit = bankUnitIndex(selectedBank(), bankOffset);
        const u32 within = unitOffset(bankOffset);
        u32 chunk = gRuntimeConfig.logicalUnitSizeBytes - within;
        if (chunk > byteCount)
            chunk = byteCount;
        readUnitBytes(unit, within, out, chunk);
        out += chunk;
        bankOffset += chunk;
        byteCount -= chunk;
    }
    return 0u;
}

__attribute__((section(".text.runtime"), used))
u32 gbashReadFlash(u32 logicalSector, u32 offsetInSector, void *destination, u32 byteCount)
{
    if (!validUnit(logicalSector) || destination == nullptr ||
        offsetInSector > gRuntimeConfig.logicalUnitSizeBytes ||
        byteCount > gRuntimeConfig.logicalUnitSizeBytes - offsetInSector)
        return 1u;
    readUnitBytes(logicalSector, offsetInSector, destination, byteCount);
    return 0u;
}

__attribute__((section(".text.runtime"), used))
u32 gbashVerifyFlashCore(const u8 *source, const volatile u8 *stockTarget, u32 byteCount)
{
    if (source == nullptr || stockTarget == nullptr)
        return 1u;
    const uptr address = reinterpret_cast<uptr>(stockTarget);
    if (address < kGamePakSaveWindow || address + byteCount > kGamePakSaveWindow + gRuntimeConfig.logicalBankSizeBytes)
        return 1u;

    u32 bankOffset = address - kGamePakSaveWindow;
    u32 sourceOffset = 0u;
    while (byteCount != 0u) {
        const u32 unit = bankUnitIndex(selectedBank(), bankOffset);
        const u32 within = unitOffset(bankOffset);
        u32 chunk = gRuntimeConfig.logicalUnitSizeBytes - within;
        if (chunk > byteCount)
            chunk = byteCount;
        const u32 result = verifyUnitBytes(unit, within, source + sourceOffset, chunk);
        if (result != 0u)
            return result;
        bankOffset += chunk;
        sourceOffset += chunk;
        byteCount -= chunk;
    }
    return 0u;
}

__attribute__((section(".text.runtime"), used))
u32 gbashVerifyFlashSector(u32 logicalSector, const u8 *source)
{
    return verifyUnitBytes(logicalSector, 0u, source, gRuntimeConfig.logicalUnitSizeBytes);
}

__attribute__((section(".text.runtime"), used))
u32 gbashVerifyFlashSectorNBytes(u32 logicalSector, const u8 *source, u32 byteCount)
{
    if (byteCount > gRuntimeConfig.logicalUnitSizeBytes)
        return 1u;
    return verifyUnitBytes(logicalSector, 0u, source, byteCount);
}

__attribute__((section(".text.runtime"), used))
u32 gbashWaitForFlashWrite()
{
    return 0u;
}

__attribute__((section(".text.runtime"), used))
u32 gbashProgramFlashSector(u32 logicalSector, const u8 *source)
{
    if (!validUnit(logicalSector))
        return 1u;
    selectBankForLogicalUnit(logicalSector);
    return appendSnapshot(logicalSector, source);
}

__attribute__((section(".text.runtime"), used))
u32 gbashProgramFlashSectorAndVerify(u32 logicalSector, const u8 *source)
{
    const u32 result = appendSnapshot(logicalSector, source);
    if (result != 0u)
        return result;
    return verifyUnitBytes(logicalSector, 0u, source, gRuntimeConfig.logicalUnitSizeBytes);
}

__attribute__((section(".text.runtime"), used))
u32 gbashProgramFlashSectorAndVerifyNBytes(u32 logicalSector, const u8 *source, u32 byteCount)
{
    if (byteCount > gRuntimeConfig.logicalUnitSizeBytes)
        return 1u;
    const u32 result = byteCount == gRuntimeConfig.logicalUnitSizeBytes
        ? appendSnapshot(logicalSector, source)
        : appendPatchedSnapshot(logicalSector, 0u, source, byteCount);
    if (result != 0u)
        return result;
    return verifyUnitBytes(logicalSector, 0u, source, byteCount);
}

__attribute__((section(".text.runtime"), used))
u32 gbashEraseFlashSector(u32 logicalSector)
{
    if (!validUnit(logicalSector))
        return 1u;
    selectBankForLogicalUnit(logicalSector);
    return appendBlank(logicalSector);
}

__attribute__((section(".text.runtime"), used))
u32 gbashEraseFlashChip()
{
    const u32 bankBefore = selectedBank();
    for (u32 unit = 0; unit < gRuntimeConfig.logicalUnitCount; ++unit) {
        const u32 result = appendBlank(unit);
        if (result != 0u)
            return result;
    }
    // Stock chip erase does not perform a bank-select command.
    selectBank(bankBefore);
    return 0u;
}

__attribute__((section(".text.runtime"), used))
u32 gbashProgramFlashByte(const u8 *sourceByte, volatile u8 *stockFlashAddress)
{
    if (sourceByte == nullptr || stockFlashAddress == nullptr)
        return 1u;
    const uptr address = reinterpret_cast<uptr>(stockFlashAddress);
    if (address < kGamePakSaveWindow || address >= kGamePakSaveWindow + gRuntimeConfig.logicalBankSizeBytes)
        return 1u;
    const u32 bankOffset = address - kGamePakSaveWindow;
    const u32 unit = bankUnitIndex(selectedBank(), bankOffset);
    const u32 within = unitOffset(bankOffset);
    u8 value = *sourceByte;
    return appendPatchedSnapshot(unit, within, &value, 1u);
}

// Some FLASH512 libraries expose ProgramFlashByte(sector, offset, data) rather
// than the pointer-form used by FLASH1M. Keep a dedicated ABI adapter.
__attribute__((section(".text.runtime"), used))
u32 gbashProgramFlashByteByOffset(u32 logicalSector, u32 offset, u32 data)
{
    if (!validUnit(logicalSector) || offset >= gRuntimeConfig.logicalUnitSizeBytes)
        return 1u;
    u8 value = static_cast<u8>(data);
    return appendPatchedSnapshot(logicalSector, offset, &value, 1u);
}

// ---------- EEPROM adapter ----------
//
// One physical 64 KiB block stores one logical 512-byte EEPROM unit. Each
// unit contains 64 eight-byte dwords. Give every dword a private 1 KiB lane:
// 64 records x 16 bytes. Data is programmed first and the A55A tag at +14 is
// committed last. A torn record remains invisible and the next write skips it.

constexpr u32 kEepromDwordsPerUnit = 64u;
constexpr u32 kEepromRecordBytes = 16u;
constexpr u32 kEepromRecordsPerDword = 64u;
constexpr u32 kEepromLaneBytes = kEepromRecordBytes * kEepromRecordsPerDword;
constexpr u32 kEepromRecordTagOffset = 14u;

static inline u32 eepromRecordAddress(u32 unit, u32 localDword, u32 record)
{
    return storageBlockAddressByIndex(unit) + localDword * kEepromLaneBytes + record * kEepromRecordBytes;
}

static int latestEepromRecord(u32 unit, u32 localDword)
{
    for (int record = static_cast<int>(kEepromRecordsPerDword) - 1; record >= 0; --record) {
        const u32 address = eepromRecordAddress(unit, localDword, static_cast<u32>(record));
        if (*reinterpret_cast<const volatile u16 *>(address + kEepromRecordTagOffset) == kSlotDataTag)
            return record;
    }
    return -1;
}

static int virginEepromRecord(u32 unit, u32 localDword)
{
    for (u32 record = 0; record < kEepromRecordsPerDword; ++record) {
        const u32 address = eepromRecordAddress(unit, localDword, record);
        const u16 tag = *reinterpret_cast<const volatile u16 *>(address + kEepromRecordTagOffset);
        if (tag != kSlotVirginTag)
            continue;
        // Power-cut safety: do not reuse an uncommitted record if its data
        // bytes were already partially programmed.
        if (rangeIsErased(address, 8u))
            return static_cast<int>(record);
    }
    return -1;
}

// ---------- EEPROM8K compact A/B erase-domain storage ----------
//
// SAFE_V1 uses one 64 KiB append-only lane block for every logical 512-byte
// EEPROM unit. That gives excellent write headroom but costs 1 MiB for an
// 8 KiB EEPROM. COMPACT_V2 keeps bounded per-dword lookup while storing the
// complete EEPROM in only two actual NOR erase blocks.
//
// Each logical 8-byte dword owns N fixed 10-byte records inside each block:
//   bytes +0..+7 : Nintendo-encoded dword
//   bytes +8..+9 : A55A commit marker, programmed last
//
// The final metadata area contains an A/B generation header. During GC the
// inactive block is erased, every latest logical dword is reconstructed one at
// a time using only an 8-byte stack buffer, and the destination block header is
// committed LAST. A power cut therefore leaves either the old generation or
// the complete new generation authoritative. No cartridge SRAM/FRAM is used.

constexpr u32 kEepromCompactRecordBytes = 10u;
constexpr u32 kEepromCompactMetadataReserve = 1024u;
constexpr u32 kEepromCompactHeaderMagic = 0x324A4345u; // "ECJ2"
constexpr u16 kEepromCompactHeaderVersion = 1u;
constexpr u32 kEepromCompactHeaderCommitOffset = 20u;

static inline bool usesCompactEeprom()
{
    return gRuntimeConfig.storageMode == 5u;
}

static inline u32 compactEepromDwordCount()
{
    return gRuntimeConfig.logicalSaveSizeBytes >> 3u;
}

static inline u32 compactEepromRecordsPerDword()
{
    // Host derives this from erase geometry and stores it in the otherwise
    // FLASH-only slots-per-block field. Avoid libgcc division in the
    // freestanding ARM7TDMI runtime.
    return gRuntimeConfig.flashSlotsPerBlock;
}

static inline u32 compactEepromLaneBytes()
{
    return compactEepromRecordsPerDword() * kEepromCompactRecordBytes;
}

static inline u32 compactEepromMetadataOffset()
{
    return compactEepromDwordCount() * compactEepromLaneBytes();
}

static inline u32 compactEepromRecordAddress(u32 blockIndex, u32 dword, u32 record)
{
    return storageBlockAddressByIndex(blockIndex) +
        dword * compactEepromLaneBytes() + record * kEepromCompactRecordBytes;
}

static inline u32 compactEepromHeaderAddress(u32 blockIndex)
{
    return storageBlockAddressByIndex(blockIndex) + compactEepromMetadataOffset();
}

static inline u32 loadLe32(u32 address)
{
    const auto *p = reinterpret_cast<const volatile u8 *>(address);
    return static_cast<u32>(p[0]) |
        (static_cast<u32>(p[1]) << 8u) |
        (static_cast<u32>(p[2]) << 16u) |
        (static_cast<u32>(p[3]) << 24u);
}

static inline u16 loadLe16(u32 address)
{
    const auto *p = reinterpret_cast<const volatile u8 *>(address);
    return static_cast<u16>(p[0] | (static_cast<u16>(p[1]) << 8u));
}

static inline void storeLe32(u8 *out, u32 value)
{
    out[0] = static_cast<u8>(value);
    out[1] = static_cast<u8>(value >> 8u);
    out[2] = static_cast<u8>(value >> 16u);
    out[3] = static_cast<u8>(value >> 24u);
}

static inline void storeLe16(u8 *out, u16 value)
{
    out[0] = static_cast<u8>(value);
    out[1] = static_cast<u8>(value >> 8u);
}

static bool compactEepromHeaderValid(u32 blockIndex, u32 *generationOut)
{
    if (blockIndex >= 2u || !validPhysicalBlock(blockIndex))
        return false;
    const u32 records = compactEepromRecordsPerDword();
    if (records < 2u)
        return false;
    const u32 header = compactEepromHeaderAddress(blockIndex);
    const u32 generation = loadLe32(header + 4u);
    if (loadLe32(header + 0u) != kEepromCompactHeaderMagic ||
        loadLe32(header + 8u) != ~generation ||
        loadLe32(header + 12u) != gRuntimeConfig.logicalSaveSizeBytes ||
        loadLe16(header + 16u) != static_cast<u16>(records) ||
        loadLe16(header + 18u) != kEepromCompactHeaderVersion ||
        loadLe16(header + kEepromCompactHeaderCommitOffset) != kSlotDataTag)
        return false;
    if (generationOut != nullptr)
        *generationOut = generation;
    return true;
}

static inline bool compactGenerationNewer(u32 left, u32 right)
{
    return left != right && (left - right) < 0x80000000u;
}

static int activeCompactEepromBlock(u32 *generationOut)
{
    u32 generation0 = 0u, generation1 = 0u;
    const bool valid0 = compactEepromHeaderValid(0u, &generation0);
    const bool valid1 = compactEepromHeaderValid(1u, &generation1);
    if (!valid0 && !valid1)
        return -1;
    if (valid0 && (!valid1 || compactGenerationNewer(generation0, generation1))) {
        if (generationOut != nullptr) *generationOut = generation0;
        return 0;
    }
    if (generationOut != nullptr) *generationOut = generation1;
    return 1;
}

static int latestCompactEepromRecord(u32 blockIndex, u32 dword)
{
    const u32 records = compactEepromRecordsPerDword();
    for (int record = static_cast<int>(records) - 1; record >= 0; --record) {
        const u32 address = compactEepromRecordAddress(blockIndex, dword, static_cast<u32>(record));
        if (*reinterpret_cast<const volatile u16 *>(address + 8u) == kSlotDataTag)
            return record;
    }
    return -1;
}

static int virginCompactEepromRecord(u32 blockIndex, u32 dword)
{
    const u32 records = compactEepromRecordsPerDword();
    for (u32 record = 0u; record < records; ++record) {
        const u32 address = compactEepromRecordAddress(blockIndex, dword, record);
        if (*reinterpret_cast<const volatile u16 *>(address + 8u) != kSlotVirginTag)
            continue;
        // A torn uncommitted record is never reused until the whole inactive
        // erase block is recycled by A/B compaction.
        if (rangeIsErased(address, 8u))
            return static_cast<int>(record);
    }
    return -1;
}

static bool compactEncodedDword(u32 blockIndex, u32 dword, u8 *encoded)
{
    const int record = latestCompactEepromRecord(blockIndex, dword);
    if (record < 0) {
        fillBytes(encoded, 0xFFu, 8u);
        return false;
    }
    const auto *stored = reinterpret_cast<const volatile u8 *>(
        compactEepromRecordAddress(blockIndex, dword, static_cast<u32>(record)));
    for (u32 index = 0u; index < 8u; ++index)
        encoded[index] = stored[index];
    return true;
}

static bool allBytesFF(const u8 *bytes, u32 count)
{
    for (u32 index = 0u; index < count; ++index)
        if (bytes[index] != 0xFFu)
            return false;
    return true;
}

static bool bytesEqual(const volatile u8 *stored, const u8 *expected, u32 count)
{
    for (u32 index = 0u; index < count; ++index)
        if (stored[index] != expected[index])
            return false;
    return true;
}

static u32 programCompactEepromRecord(u32 blockIndex, u32 dword, u32 record, const u8 *encoded)
{
    const u32 address = compactEepromRecordAddress(blockIndex, dword, record);
    copyRamWorker();
    u32 result = programBytesWorker()(address, encoded, 8u, gRuntimeConfig.norProgramCommand);
    if (result != 0u)
        return result;
    if (!bytesEqual(reinterpret_cast<const volatile u8 *>(address), encoded, 8u))
        return 0xE442u;
    const u16 marker = kSlotDataTag;
    copyRamWorker();
    result = programBytesWorker()(address + 8u, reinterpret_cast<const u8 *>(&marker), 2u,
        gRuntimeConfig.norProgramCommand);
    if (result != 0u)
        return result;
    return *reinterpret_cast<const volatile u16 *>(address + 8u) == kSlotDataTag ? 0u : 0xE443u;
}

struct CompactEraseGuard {
    u16 ime;
    u16 dmaControl[4];
    u16 pausedMask;
};

static void enterCompactEraseGuard(CompactEraseGuard *state)
{
    volatile u32 *dmaSource[4] = {
        reinterpret_cast<volatile u32 *>(0x040000B0u),
        reinterpret_cast<volatile u32 *>(0x040000BCu),
        reinterpret_cast<volatile u32 *>(0x040000C8u),
        reinterpret_cast<volatile u32 *>(0x040000D4u),
    };
    volatile u16 *dmaControl[4] = {
        reinterpret_cast<volatile u16 *>(0x040000BAu),
        reinterpret_cast<volatile u16 *>(0x040000C6u),
        reinterpret_cast<volatile u16 *>(0x040000D2u),
        reinterpret_cast<volatile u16 *>(0x040000DEu),
    };
    volatile u16 &ime = *reinterpret_cast<volatile u16 *>(0x04000208u);
    state->ime = ime;
    state->pausedMask = 0u;
    ime = 0u;
    for (u32 index = 0u; index < 4u; ++index) {
        state->dmaControl[index] = *dmaControl[index];
        if ((state->dmaControl[index] & 0x8000u) == 0u || !dmaReadsGamePakRom(*dmaSource[index]))
            continue;
        *dmaControl[index] = static_cast<u16>(state->dmaControl[index] & 0x7FFFu);
        state->pausedMask = static_cast<u16>(state->pausedMask | static_cast<u16>(1u << index));
    }
}

static void leaveCompactEraseGuard(const CompactEraseGuard *state)
{
    volatile u16 *dmaControl[4] = {
        reinterpret_cast<volatile u16 *>(0x040000BAu),
        reinterpret_cast<volatile u16 *>(0x040000C6u),
        reinterpret_cast<volatile u16 *>(0x040000D2u),
        reinterpret_cast<volatile u16 *>(0x040000DEu),
    };
    for (u32 index = 0u; index < 4u; ++index)
        if ((state->pausedMask & static_cast<u16>(1u << index)) != 0u)
            *dmaControl[index] = state->dmaControl[index];
    *reinterpret_cast<volatile u16 *>(0x04000208u) = state->ime;
}

static u32 eraseCompactEepromBlock(u32 blockIndex)
{
    if (blockIndex >= 2u || !validPhysicalBlock(blockIndex))
        return 0xE450u;
    CompactEraseGuard state{};
    enterCompactEraseGuard(&state);
    const u32 result = gbashRunSramStackErase(
        storageBlockAddressByIndex(blockIndex), gRuntimeConfig.norProgramCommand);
    leaveCompactEraseGuard(&state);
    if (result != 0u)
        return result;
    return rangeIsErased(storageBlockAddressByIndex(blockIndex), gRuntimeConfig.physicalBlockBytes)
        ? 0u : 0xE451u;
}

static u32 writeCompactEepromHeaderFields(u32 blockIndex, u32 generation)
{
    u8 header[20];
    storeLe32(header + 0u, kEepromCompactHeaderMagic);
    storeLe32(header + 4u, generation);
    storeLe32(header + 8u, ~generation);
    storeLe32(header + 12u, gRuntimeConfig.logicalSaveSizeBytes);
    storeLe16(header + 16u, static_cast<u16>(compactEepromRecordsPerDword()));
    storeLe16(header + 18u, kEepromCompactHeaderVersion);
    const u32 address = compactEepromHeaderAddress(blockIndex);
    copyRamWorker();
    const u32 result = programBytesWorker()(address, header, sizeof(header), gRuntimeConfig.norProgramCommand);
    if (result != 0u)
        return result;
    return bytesEqual(reinterpret_cast<const volatile u8 *>(address), header, sizeof(header)) ? 0u : 0xE452u;
}

static u32 commitCompactEepromHeader(u32 blockIndex)
{
    const u16 marker = kSlotDataTag;
    const u32 address = compactEepromHeaderAddress(blockIndex) + kEepromCompactHeaderCommitOffset;
    copyRamWorker();
    const u32 result = programBytesWorker()(address, reinterpret_cast<const u8 *>(&marker), 2u,
        gRuntimeConfig.norProgramCommand);
    if (result != 0u)
        return result;
    return *reinterpret_cast<const volatile u16 *>(address) == kSlotDataTag ? 0u : 0xE453u;
}

static u32 initialiseCompactEepromBlock(u32 blockIndex, u32 generation, bool eraseFirst)
{
    if (eraseFirst || !rangeIsErased(storageBlockAddressByIndex(blockIndex), gRuntimeConfig.physicalBlockBytes)) {
        const u32 eraseResult = eraseCompactEepromBlock(blockIndex);
        if (eraseResult != 0u)
            return eraseResult;
    }
    u32 result = writeCompactEepromHeaderFields(blockIndex, generation);
    if (result != 0u)
        return result;
    result = commitCompactEepromHeader(blockIndex);
    if (result != 0u)
        return result;
    u32 verifiedGeneration = 0u;
    return compactEepromHeaderValid(blockIndex, &verifiedGeneration) && verifiedGeneration == generation
        ? 0u : 0xE454u;
}

static u32 compactEepromGeneration(u32 activeBlock, u32 currentGeneration, u32 *newActiveOut)
{
    const u32 destination = activeBlock ^ 1u;
    // The old active generation remains authoritative during erase and copy.
    u32 result = eraseCompactEepromBlock(destination);
    if (result != 0u)
        return result;

    const u32 nextGeneration = currentGeneration + 1u;
    result = writeCompactEepromHeaderFields(destination, nextGeneration);
    if (result != 0u)
        return result;

    const u32 dwordCount = compactEepromDwordCount();
    for (u32 dword = 0u; dword < dwordCount; ++dword) {
        u8 encoded[8];
        compactEncodedDword(activeBlock, dword, encoded);
        // An omitted record represents erased EEPROM (all FF), so no physical
        // record is needed for that logical value in the new generation.
        if (allBytesFF(encoded, 8u))
            continue;
        result = programCompactEepromRecord(destination, dword, 0u, encoded);
        if (result != 0u)
            return result;
    }

    // Publish the complete destination generation last. The old generation is
    // intentionally not erased here; it remains rollback evidence until the
    // next GC cycle needs that block as the destination.
    result = commitCompactEepromHeader(destination);
    if (result != 0u)
        return result;
    u32 verifiedGeneration = 0u;
    if (!compactEepromHeaderValid(destination, &verifiedGeneration) || verifiedGeneration != nextGeneration)
        return 0xE455u;
    if (newActiveOut != nullptr)
        *newActiveOut = destination;
    return 0u;
}

static u32 readCompactEepromDword(u32 dwordAddress, u8 *destination)
{
    const u32 dwordCount = compactEepromDwordCount();
    if (destination == nullptr || dwordCount == 0u || (dwordCount & (dwordCount - 1u)) != 0u)
        return 1u;
    const u32 dword = dwordAddress & (dwordCount - 1u);
    const int active = activeCompactEepromBlock(nullptr);
    if (active < 0) {
        fillBytes(destination, 0xFFu, 8u);
        return 0u;
    }
    const int record = latestCompactEepromRecord(static_cast<u32>(active), dword);
    if (record < 0) {
        fillBytes(destination, 0xFFu, 8u);
        return 0u;
    }
    const auto *encoded = reinterpret_cast<const volatile u8 *>(
        compactEepromRecordAddress(static_cast<u32>(active), dword, static_cast<u32>(record)));
    for (u32 index = 0u; index < 8u; ++index)
        destination[index] = encoded[7u - index];
    return 0u;
}

static u32 programCompactEepromDword(u32 dwordAddress, const u8 *source)
{
    if (source == nullptr)
        return 1u;
    const u32 dwordCount = compactEepromDwordCount();
    if (dwordCount == 0u || (dwordCount & (dwordCount - 1u)) != 0u || compactEepromRecordsPerDword() < 2u)
        return 1u;
    const u32 dword = dwordAddress & (dwordCount - 1u);

    u8 encoded[8];
    for (u32 index = 0u; index < 8u; ++index)
        encoded[7u - index] = source[index];

    u32 generation = 0u;
    int active = activeCompactEepromBlock(&generation);
    if (active < 0) {
        // Fresh output images carry a tiny 0-bit flasher anchor so stale target
        // contents cannot be silently skipped. Erase block A once before its
        // first generation header is published.
        const u32 result = initialiseCompactEepromBlock(0u, 1u, false);
        if (result != 0u)
            return result;
        active = 0;
        generation = 1u;
    }

    u8 current[8];
    compactEncodedDword(static_cast<u32>(active), dword, current);
    bool identical = true;
    for (u32 index = 0u; index < 8u; ++index) {
        if (current[index] != encoded[index]) {
            identical = false;
            break;
        }
    }
    if (identical)
        return 0u;

    int record = virginCompactEepromRecord(static_cast<u32>(active), dword);
    if (record < 0) {
        u32 newActive = 0u;
        const u32 result = compactEepromGeneration(static_cast<u32>(active), generation, &newActive);
        if (result != 0u)
            return result;
        active = static_cast<int>(newActive);
        record = virginCompactEepromRecord(newActive, dword);
        if (record < 0)
            return 0xE456u;
    }
    return programCompactEepromRecord(static_cast<u32>(active), dword, static_cast<u32>(record), encoded);
}

__attribute__((section(".text.runtime"), used))
u32 gbashReadEepromDword(u32 dwordAddress, u8 *destination)
{
    if (usesCompactEeprom())
        return readCompactEepromDword(dwordAddress, destination);
    if (destination == nullptr)
        return 1u;
    const u32 dwordCount = gRuntimeConfig.logicalSaveSizeBytes >> 3u;
    if (dwordCount == 0u || (dwordCount & (dwordCount - 1u)) != 0u)
        return 1u;
    // Nintendo EEPROM APIs carry address bits above the physical geometry.
    // Hardware-proven R13G masks them before forming the byte address.
    const u32 byteOffset = (dwordAddress & (dwordCount - 1u)) << 3u;

    const u32 unit = byteOffset >> 9u; // 512-byte physical/logical grouping
    const u32 localDword = (byteOffset & 0x1FFu) >> 3u;
    if (!validUnit(unit) || localDword >= kEepromDwordsPerUnit)
        return 1u;

    const int record = latestEepromRecord(unit, localDword);
    if (record < 0) {
        fillBytes(destination, 0xFFu, 8u);
        return 0u;
    }

    const u32 address = eepromRecordAddress(unit, localDword, static_cast<u32>(record));
    const auto *encoded = reinterpret_cast<const volatile u8 *>(address);
    for (u32 index = 0; index < 8u; ++index)
        destination[index] = encoded[7u - index];
    return 0u;
}

__attribute__((section(".text.runtime"), used))
u32 gbashProgramEepromDword(u32 dwordAddress, const u8 *source)
{
    if (usesCompactEeprom())
        return programCompactEepromDword(dwordAddress, source);
    if (source == nullptr)
        return 1u;
    const u32 dwordCount = gRuntimeConfig.logicalSaveSizeBytes >> 3u;
    if (dwordCount == 0u || (dwordCount & (dwordCount - 1u)) != 0u)
        return 1u;
    // Nintendo EEPROM APIs carry address bits above the physical geometry.
    // Hardware-proven R13G masks them before forming the byte address.
    const u32 byteOffset = (dwordAddress & (dwordCount - 1u)) << 3u;

    const u32 unit = byteOffset >> 9u;
    const u32 localDword = (byteOffset & 0x1FFu) >> 3u;
    if (!validUnit(unit) || localDword >= kEepromDwordsPerUnit)
        return 1u;

    u8 encoded[8];
    for (u32 index = 0; index < 8u; ++index)
        encoded[7u - index] = source[index];

    // EEPROM libraries are allowed to program the same dword repeatedly. Do
    // not spend an append-only record when the logical value is unchanged;
    // this is protocol semantics/wear reduction, not a title heuristic. It is
    // especially important for format/check loops on larger EEPROM geometry.
    const int currentRecord = latestEepromRecord(unit, localDword);
    if (currentRecord >= 0) {
        const u32 currentAddress = eepromRecordAddress(unit, localDword, static_cast<u32>(currentRecord));
        const auto *current = reinterpret_cast<const volatile u8 *>(currentAddress);
        bool identical = true;
        for (u32 index = 0u; index < 8u; ++index) {
            if (current[index] != encoded[index]) {
                identical = false;
                break;
            }
        }
        if (identical)
            return 0u;
    }

    const int record = virginEepromRecord(unit, localDword);
    if (record < 0)
        return 0xE401u;

    const u32 address = eepromRecordAddress(unit, localDword, static_cast<u32>(record));
    copyRamWorker();
    u32 result = programBytesWorker()(address, encoded, 8u, gRuntimeConfig.norProgramCommand);
    if (result != 0u)
        return result;
    const auto *stored = reinterpret_cast<const volatile u8 *>(address);
    for (u32 index = 0; index < 8u; ++index) {
        if (stored[index] != encoded[index])
            return 0xE402u;
    }

    const u16 marker = kSlotDataTag;
    copyRamWorker();
    result = programBytesWorker()(address + kEepromRecordTagOffset, reinterpret_cast<const u8 *>(&marker), 2u, gRuntimeConfig.norProgramCommand);
    if (result != 0u)
        return result;
    return *reinterpret_cast<const volatile u16 *>(address + kEepromRecordTagOffset) == kSlotDataTag ? 0u : 0xE403u;
}

// ---------- Nintendo SRAM hotkey adapter ----------
//
// The game works against an EWRAM shadow. Normal ReadSram/WriteSram traffic
// never enters NOR command mode. L+R+SELECT+B commits the complete shadow to
// the persistent NOR mirror. This keeps SRAM wear proportional to explicit
// user commits rather than to every game-side SRAM update.

extern void gbashSramHotkeyIrqEntry();
extern void gbashSramOwnedShadowInitIrqEntry();
extern void gbashSramOwnedShadowHotkeyIrqEntry();
extern void gbashSramOwnedShadowHotkeyLatchedIrqEntry();
extern void gbashSramDummyIrq();

constexpr uptr kBiosUserIrqVector = 0x03007FFCu;

static inline bool usesGameOwnedSramSnapshot()
{
    return (gRuntimeConfig.reservedSramConfig & kSramConfigGameOwnedShadowSnapshot) != 0u;
}

static inline bool usesFullReadSramRefresh()
{
    return (gRuntimeConfig.reservedSramConfig & kSramConfigFullReadRefresh) != 0u;
}

static inline bool usesSramProgramOnlyJournal()
{
    return gRuntimeConfig.storageMode == 4u;
}

// Dynamic SRAM IRQ chaining state. The host selects a collision-free EWRAM
// slot; BIOS-reserved 03007Fxx memory is never used.
constexpr u32 kSramRuntimeStateMagic = 0x4D415253u; // "SRAM" little-endian

struct SramRuntimeState {
    u32 magic;
    u32 initialized;
    u32 hotkeyLatched;
    u32 chainTarget;
};

static volatile SramRuntimeState &sramRuntimeState()
{
    return *reinterpret_cast<volatile SramRuntimeState *>(gRuntimeConfig.sramRuntimeStateAddress);
}

static inline bool plausibleIrqChainTarget(uptr target)
{
    return (target >= 0x02000000u && target < 0x02040000u) ||
           (target >= 0x03000000u && target < 0x03008000u) ||
           (target >= 0x08000000u && target < 0x0E000000u);
}

__attribute__((section(".text.runtime"), used))
u32 gbashSramIrqChainTarget()
{
    if (usesGameOwnedSramSnapshot() && gRuntimeConfig.sramRuntimeStateAddress != 0u) {
        const uptr target = sramRuntimeState().chainTarget;
        if (plausibleIrqChainTarget(target))
            return target;
    }
    return gRuntimeConfig.sramIrqDispatcherAddress;
}

static void copyFromVolatile(void *destination, const volatile void *source, u32 byteCount)
{
    auto *out8 = static_cast<u8 *>(destination);
    const auto *in8 = static_cast<const volatile u8 *>(source);

    if (((reinterpret_cast<uptr>(out8) | reinterpret_cast<uptr>(in8)) & 3u) == 0u) {
        auto *out32 = reinterpret_cast<u32 *>(out8);
        const auto *in32 = reinterpret_cast<const volatile u32 *>(in8);
        while (byteCount >= 4u) {
            *out32++ = *in32++;
            byteCount -= 4u;
        }
        out8 = reinterpret_cast<u8 *>(out32);
        in8 = reinterpret_cast<const volatile u8 *>(in32);
    }
    while (byteCount--)
        *out8++ = *in8++;
}


// Full-size/internal-hole SRAM mode. The game already owns a structurally
// proven 32 KiB EWRAM image; GBASaveHandler must not reserve another EWRAM
// shadow or a fixed EWRAM NOR worker. Two verified 64 KiB NOR blocks alternate
// complete snapshots. Data is programmed in 4 KiB bounded chunks and the
// generation header is committed last.
constexpr u32 kSramSnapshotMagic = 0x36315253u; // "SR16"
constexpr u32 kSramSnapshotVersion = 1u;
constexpr u32 kSramSnapshotHeaderOffset = 0xFF00u;
constexpr u32 kSramSnapshotChunkBytes = 0x1000u;
constexpr u32 kSramSnapshotHostAnchorOffset = 0xFFFCu;
constexpr u16 kSramSnapshotCommit = 0xA55Au;

struct SramSnapshotHeader {
    u32 magic;
    u32 version;
    u32 saveBytes;
    u32 generation;
    u32 generationInverse;
    u32 dataHash;
    u32 dataHashInverse;
    u16 commit;
    u16 reserved;
};

struct SramSnapshotSelection {
    u32 blockAddress;
    u32 generation;
    bool valid;
};


static u32 fnv1aVolatile(const volatile u8 *data, u32 byteCount)
{
    u32 hash = 2166136261u;
    while (byteCount-- != 0u) {
        hash ^= *data++;
        hash *= 16777619u;
    }
    return hash;
}

static u32 fnv1aBytes(const u8 *data, u32 byteCount)
{
    u32 hash = 2166136261u;
    while (byteCount-- != 0u) {
        hash ^= *data++;
        hash *= 16777619u;
    }
    return hash;
}

static bool validOwnedSramSnapshot(u32 blockAddress, u32 &generation)
{
    const auto *header = reinterpret_cast<const volatile SramSnapshotHeader *>(
        blockAddress + kSramSnapshotHeaderOffset);
    if (header->commit != kSramSnapshotCommit ||
        header->magic != kSramSnapshotMagic ||
        header->version != kSramSnapshotVersion ||
        header->saveBytes != gRuntimeConfig.sramSizeBytes ||
        header->generationInverse != ~header->generation ||
        header->dataHashInverse != ~header->dataHash)
        return false;

    const u32 hash = fnv1aVolatile(
        reinterpret_cast<const volatile u8 *>(blockAddress),
        gRuntimeConfig.sramSizeBytes);
    if (hash != header->dataHash)
        return false;
    generation = header->generation;
    return true;
}

static bool generationIsNewer(u32 candidate, u32 reference)
{
    // Serial-number arithmetic: supports the one-step A/B generation rollover
    // without making 0x00000000 look older than 0xFFFFFFFF. Exactly half the
    // 32-bit space apart is deliberately not considered newer.
    const u32 delta = candidate - reference;
    return delta != 0u && delta < 0x80000000u;
}

struct SramJournalSelection {
    u32 recordIndex;
    u32 generation;
    bool valid;
};

constexpr u32 kSramJournalMagic = 0x4A525353u; // "SSRJ"
constexpr u32 kSramJournalVersion = 1u;

static bool journalLocate(u32 logicalOffset, u32 &physicalAddress, u32 &availableBytes)
{
    u32 cursor = logicalOffset;
    for (u32 index = 0u; index < gRuntimeConfig.sramJournalSpanCount; ++index) {
        const u32 spanBytes = gRuntimeConfig.sramJournalSpanLengths[index];
        if (cursor < spanBytes) {
            physicalAddress = gRuntimeConfig.sramJournalSpanAddresses[index] + cursor;
            availableBytes = spanBytes - cursor;
            return true;
        }
        cursor -= spanBytes;
    }
    return false;
}

static bool journalReadBytes(u32 logicalOffset, u8 *destination, u32 byteCount)
{
    while (byteCount != 0u) {
        u32 address = 0u, available = 0u;
        if (!journalLocate(logicalOffset, address, available))
            return false;
        const u32 chunk = available < byteCount ? available : byteCount;
        copyFromVolatile(destination, reinterpret_cast<const volatile void *>(address), chunk);
        destination += chunk;
        logicalOffset += chunk;
        byteCount -= chunk;
    }
    return true;
}

static bool journalRangeIsErased(u32 logicalOffset, u32 byteCount)
{
    while (byteCount != 0u) {
        u32 address = 0u, available = 0u;
        if (!journalLocate(logicalOffset, address, available))
            return false;
        const u32 chunk = available < byteCount ? available : byteCount;
        const auto *source = reinterpret_cast<const volatile u8 *>(address);
        for (u32 index = 0u; index < chunk; ++index) {
            if (source[index] != 0xFFu)
                return false;
        }
        logicalOffset += chunk;
        byteCount -= chunk;
    }
    return true;
}

static u32 journalProgramBytes(u32 logicalOffset, const u8 *source, u32 byteCount)
{
    while (byteCount != 0u) {
        u32 address = 0u, available = 0u;
        if (!journalLocate(logicalOffset, address, available))
            return 0xE731u;
        const u32 chunk = available < byteCount ? available : byteCount;
        const u32 result = gbashRunSramStackProgram(address, source, chunk, gRuntimeConfig.norProgramCommand);
        if (result != 0u)
            return result;
        if (!volatileRangeMatches(address, source, chunk))
            return 0xE732u;
        source += chunk;
        logicalOffset += chunk;
        byteCount -= chunk;
    }
    return 0u;
}

static u32 journalHash(u32 logicalOffset, u32 byteCount)
{
    u32 hash = 2166136261u;
    while (byteCount != 0u) {
        u32 address = 0u, available = 0u;
        if (!journalLocate(logicalOffset, address, available))
            return 0u;
        const u32 chunk = available < byteCount ? available : byteCount;
        const auto *source = reinterpret_cast<const volatile u8 *>(address);
        for (u32 index = 0u; index < chunk; ++index) {
            hash ^= source[index];
            hash *= 16777619u;
        }
        logicalOffset += chunk;
        byteCount -= chunk;
    }
    return hash;
}

static bool validSramJournalRecord(u32 recordIndex, u32 &generation)
{
    if (recordIndex >= gRuntimeConfig.sramJournalRecordCount ||
        gRuntimeConfig.sramJournalRecordBytes < gRuntimeConfig.sramSizeBytes + sizeof(SramSnapshotHeader))
        return false;
    const u32 base = recordIndex * gRuntimeConfig.sramJournalRecordBytes;
    SramSnapshotHeader header;
    if (!journalReadBytes(base + gRuntimeConfig.sramSizeBytes,
            reinterpret_cast<u8 *>(&header), sizeof(header)))
        return false;
    if (header.commit != kSramSnapshotCommit ||
        header.magic != kSramJournalMagic ||
        header.version != kSramJournalVersion ||
        header.saveBytes != gRuntimeConfig.sramSizeBytes ||
        header.generationInverse != ~header.generation ||
        header.dataHashInverse != ~header.dataHash)
        return false;
    if (journalHash(base, gRuntimeConfig.sramSizeBytes) != header.dataHash)
        return false;
    generation = header.generation;
    return true;
}

static SramJournalSelection newestSramJournalRecord()
{
    SramJournalSelection newest{0u, 0u, false};
    for (u32 record = 0u; record < gRuntimeConfig.sramJournalRecordCount; ++record) {
        u32 generation = 0u;
        if (!validSramJournalRecord(record, generation))
            continue;
        if (!newest.valid || generationIsNewer(generation, newest.generation))
            newest = {record, generation, true};
    }
    return newest;
}

static bool sramJournalRecordMatchesShadow(const SramJournalSelection &record)
{
    if (!record.valid)
        return false;
    u32 logicalOffset = record.recordIndex * gRuntimeConfig.sramJournalRecordBytes;
    const auto *shadow = reinterpret_cast<const u8 *>(gRuntimeConfig.sramShadowAddress);
    u32 remaining = gRuntimeConfig.sramSizeBytes;
    while (remaining != 0u) {
        u32 address = 0u, available = 0u;
        if (!journalLocate(logicalOffset, address, available))
            return false;
        const u32 chunk = available < remaining ? available : remaining;
        if (!volatileRangeMatches(address, shadow, chunk))
            return false;
        shadow += chunk;
        logicalOffset += chunk;
        remaining -= chunk;
    }
    return true;
}

static u32 commitSramProgramOnlyJournal()
{
    const SramJournalSelection current = newestSramJournalRecord();
    if (sramJournalRecordMatchesShadow(current))
        return 0u;

    u32 targetRecord = gRuntimeConfig.sramJournalRecordCount;
    for (u32 record = 0u; record < gRuntimeConfig.sramJournalRecordCount; ++record) {
        const u32 base = record * gRuntimeConfig.sramJournalRecordBytes;
        // A torn record is never reused. Only a completely virgin record can
        // accept a new commit, so this mode never requires a sector erase.
        if (journalRangeIsErased(base, gRuntimeConfig.sramJournalRecordBytes)) {
            targetRecord = record;
            break;
        }
    }
    if (targetRecord >= gRuntimeConfig.sramJournalRecordCount)
        return 0xE73Au; // journal exhausted; deliberately fail without erase

    const u32 base = targetRecord * gRuntimeConfig.sramJournalRecordBytes;
    const auto *shadow = reinterpret_cast<const u8 *>(gRuntimeConfig.sramShadowAddress);
    u32 result = journalProgramBytes(base, shadow, gRuntimeConfig.sramSizeBytes);
    if (result != 0u)
        return result;

    const u32 generation = current.valid ? current.generation + 1u : 1u;
    SramSnapshotHeader header{};
    header.magic = kSramJournalMagic;
    header.version = kSramJournalVersion;
    header.saveBytes = gRuntimeConfig.sramSizeBytes;
    header.generation = generation;
    header.generationInverse = ~generation;
    header.dataHash = fnv1aBytes(shadow, gRuntimeConfig.sramSizeBytes);
    header.dataHashInverse = ~header.dataHash;
    header.commit = 0xFFFFu;
    header.reserved = 0xFFFFu;

    constexpr u32 headerBodyBytes = 7u * sizeof(u32);
    result = journalProgramBytes(base + gRuntimeConfig.sramSizeBytes,
        reinterpret_cast<const u8 *>(&header), headerBodyBytes);
    if (result != 0u)
        return result;
    const u16 commit = kSramSnapshotCommit;
    result = journalProgramBytes(base + gRuntimeConfig.sramSizeBytes + headerBodyBytes,
        reinterpret_cast<const u8 *>(&commit), sizeof(commit));
    if (result != 0u)
        return result;

    u32 verifiedGeneration = 0u;
    if (!validSramJournalRecord(targetRecord, verifiedGeneration) || verifiedGeneration != generation)
        return 0xE73Bu;
    return 0u;
}

static SramSnapshotSelection newestOwnedSramSnapshot()
{
    u32 generationA = 0u;
    u32 generationB = 0u;
    const bool validA = validOwnedSramSnapshot(gRuntimeConfig.sramMirrorAddress, generationA);
    const bool validB = validOwnedSramSnapshot(gRuntimeConfig.sramBackupAddress, generationB);
    if (!validA && !validB)
        return {0u, 0u, false};
    if (validA && (!validB || !generationIsNewer(generationB, generationA)))
        return {gRuntimeConfig.sramMirrorAddress, generationA, true};
    return {gRuntimeConfig.sramBackupAddress, generationB, true};
}

static bool ownedSnapshotMatchesShadow(const SramSnapshotSelection &snapshot)
{
    if (!snapshot.valid)
        return false;
    const auto *stored = reinterpret_cast<const volatile u8 *>(snapshot.blockAddress);
    const auto *shadow = reinterpret_cast<const u8 *>(gRuntimeConfig.sramShadowAddress);
    for (u32 index = 0u; index < gRuntimeConfig.sramSizeBytes; ++index) {
        if (stored[index] != shadow[index])
            return false;
    }
    return true;
}

static bool ownedSnapshotWritableAreaIsBlank(u32 blockAddress)
{
    // The distributed ROM deliberately contains one 0xAAAA host-flasher
    // anchor at +FFFC. Apart from those two bytes, require the *entire* erase
    // block to be FF before skipping erase. This catches stale data from any
    // previous experimental layout, not just stale snapshot data/header.
    const auto *block = reinterpret_cast<const volatile u8 *>(blockAddress);
    for (u32 index = 0u; index < gRuntimeConfig.physicalBlockBytes; ++index) {
        if (index == kSramSnapshotHostAnchorOffset || index == kSramSnapshotHostAnchorOffset + 1u)
            continue;
        if (block[index] != 0xFFu)
            return false;
    }
    const u8 anchorLow = block[kSramSnapshotHostAnchorOffset];
    const u8 anchorHigh = block[kSramSnapshotHostAnchorOffset + 1u];
    return (anchorLow == 0xFFu && anchorHigh == 0xFFu) ||
           (anchorLow == 0xAAu && anchorHigh == 0xAAu);
}

static bool volatileRangeMatches(u32 address, const u8 *source, u32 byteCount)
{
    const auto *stored = reinterpret_cast<const volatile u8 *>(address);
    for (u32 index = 0u; index < byteCount; ++index) {
        if (stored[index] != source[index])
            return false;
    }
    return true;
}

static void restoreOwnedSramShadowFromSnapshot()
{
    bool restored = false;
    if (usesSramProgramOnlyJournal()) {
        const SramJournalSelection record = newestSramJournalRecord();
        if (record.valid) {
            restored = journalReadBytes(
                record.recordIndex * gRuntimeConfig.sramJournalRecordBytes,
                reinterpret_cast<u8 *>(gRuntimeConfig.sramShadowAddress),
                gRuntimeConfig.sramSizeBytes);
        }
    } else {
        const SramSnapshotSelection snapshot = newestOwnedSramSnapshot();
        if (snapshot.valid) {
            copyFromVolatile(
                reinterpret_cast<void *>(gRuntimeConfig.sramShadowAddress),
                reinterpret_cast<const volatile void *>(snapshot.blockAddress),
                gRuntimeConfig.sramSizeBytes);
            restored = true;
        }
    }
    if (!restored) {
        fillBytes(
            reinterpret_cast<void *>(gRuntimeConfig.sramShadowAddress),
            0xFFu,
            gRuntimeConfig.sramSizeBytes);
    }

}

static void ensureOwnedSramShadowInitialized()
{
    volatile SramRuntimeState &state = sramRuntimeState();
    const uptr vector = *reinterpret_cast<const volatile u32 *>(kBiosUserIrqVector);
    const uptr init = reinterpret_cast<uptr>(&gbashSramOwnedShadowInitIrqEntry);
    const uptr armed = reinterpret_cast<uptr>(&gbashSramOwnedShadowHotkeyIrqEntry);
    const uptr latched = reinterpret_cast<uptr>(&gbashSramOwnedShadowHotkeyLatchedIrqEntry);

    // Match the hardware-proven one-shot lifecycle: BIOS_USER_IRQ_VECTOR itself
    // is the durable INIT/ARMED state. Unlike EWRAM, it survives ordinary game
    // EWRAM clears. Validated game writes that would replace 03007FFC are
    // redirected to state.chainTarget, so dynamic IRQ chaining remains intact.
    // A cleared EWRAM state can therefore never trigger a second NOR restore.
    if (vector != init) {
        if (vector != armed && vector != latched && plausibleIrqChainTarget(vector)) {
            state.chainTarget = vector;
            *reinterpret_cast<volatile u32 *>(kBiosUserIrqVector) = armed;
        }
        return;
    }

    if (!plausibleIrqChainTarget(state.chainTarget))
        state.chainTarget = gRuntimeConfig.sramIrqDispatcherAddress;

    restoreOwnedSramShadowFromSnapshot();
    state.hotkeyLatched = 0u;
    state.initialized = 1u;
    state.magic = kSramRuntimeStateMagic;
    *reinterpret_cast<volatile u32 *>(kBiosUserIrqVector) = armed;
}

constexpr u32 kSramBreadcrumbOffset = 0xFF40u;

__attribute__((noinline)) static u32 beginSramBreadcrumb(u32 blockAddress, u16 subject)
{
    const u32 address = blockAddress + kSramBreadcrumbOffset;
    if (!rangeIsErased(address, sizeof(BreadcrumbRecord)))
        return 0xE72Au;
    BreadcrumbRecord record{kBreadcrumbMagic, kBreadcrumbOpSramCommit, subject, kBreadcrumbStageEnter};
    const u32 result = gbashRunSramStackProgram(address, reinterpret_cast<const u8 *>(&record),
        sizeof(record), gRuntimeConfig.norProgramCommand);
    if (result != 0u)
        return result;
    return volatileRangeMatches(address, reinterpret_cast<const u8 *>(&record), sizeof(record)) ? 0u : 0xE72Bu;
}

__attribute__((noinline)) static u32 advanceSramBreadcrumb(u32 blockAddress, u16 stageBits)
{
    const u16 value = stageBits;
    return gbashRunSramStackProgram(blockAddress + kSramBreadcrumbOffset + 6u,
        reinterpret_cast<const u8 *>(&value), sizeof(value), gRuntimeConfig.norProgramCommand);
}

__attribute__((noinline)) static u32 commitOwnedSramSnapshot()
{
    if (usesSramProgramOnlyJournal())
        return commitSramProgramOnlyJournal();

    const SramSnapshotSelection current = newestOwnedSramSnapshot();
    if (ownedSnapshotMatchesShadow(current))
        return 0u;

    const u32 targetBlock = !current.valid || current.blockAddress == gRuntimeConfig.sramBackupAddress
        ? gRuntimeConfig.sramMirrorAddress
        : gRuntimeConfig.sramBackupAddress;
    const u32 nextGeneration = current.valid ? current.generation + 1u : 1u;

    u32 result = 0u;
    if (!ownedSnapshotWritableAreaIsBlank(targetBlock)) {
        result = gbashRunSramStackErase(targetBlock, gRuntimeConfig.norProgramCommand);
        if (result != 0u)
            return result;
        if (!ownedSnapshotWritableAreaIsBlank(targetBlock))
            return 0xE721u;
    }

    result = beginSramBreadcrumb(targetBlock, static_cast<u16>(nextGeneration));
    if (result != 0u)
        return result;

    const auto *shadow = reinterpret_cast<const u8 *>(gRuntimeConfig.sramShadowAddress);
    for (u32 offset = 0u; offset < gRuntimeConfig.sramSizeBytes; offset += kSramSnapshotChunkBytes) {
        const u32 remaining = gRuntimeConfig.sramSizeBytes - offset;
        const u32 chunkBytes = remaining < kSramSnapshotChunkBytes ? remaining : kSramSnapshotChunkBytes;
        result = gbashRunSramStackProgram(targetBlock + offset, shadow + offset, chunkBytes, gRuntimeConfig.norProgramCommand);
        if (result != 0u)
            return result;
        if (!volatileRangeMatches(targetBlock + offset, shadow + offset, chunkBytes))
            return 0xE722u;
    }

    result = advanceSramBreadcrumb(targetBlock, kBreadcrumbStageDataWritten);
    if (result != 0u)
        return result;
    result = advanceSramBreadcrumb(targetBlock, kBreadcrumbStageDataVerified);
    if (result != 0u)
        return result;

    SramSnapshotHeader header{};
    header.magic = kSramSnapshotMagic;
    header.version = kSramSnapshotVersion;
    header.saveBytes = gRuntimeConfig.sramSizeBytes;
    header.generation = nextGeneration;
    header.generationInverse = ~nextGeneration;
    header.dataHash = fnv1aBytes(shadow, gRuntimeConfig.sramSizeBytes);
    header.dataHashInverse = ~header.dataHash;
    header.commit = 0xFFFFu;
    header.reserved = 0xFFFFu;

    constexpr u32 headerBodyBytes = 7u * sizeof(u32);
    result = gbashRunSramStackProgram(
        targetBlock + kSramSnapshotHeaderOffset,
        reinterpret_cast<const u8 *>(&header),
        headerBodyBytes,
        gRuntimeConfig.norProgramCommand);
    if (result != 0u)
        return result;
    if (!volatileRangeMatches(
            targetBlock + kSramSnapshotHeaderOffset,
            reinterpret_cast<const u8 *>(&header),
            headerBodyBytes))
        return 0xE723u;
    result = advanceSramBreadcrumb(targetBlock, kBreadcrumbStageMetadataWritten);
    if (result != 0u)
        return result;

    const u16 commit = kSramSnapshotCommit;
    result = gbashRunSramStackProgram(
        targetBlock + kSramSnapshotHeaderOffset + headerBodyBytes,
        reinterpret_cast<const u8 *>(&commit),
        sizeof(commit),
        gRuntimeConfig.norProgramCommand);
    if (result != 0u)
        return result;
    result = advanceSramBreadcrumb(targetBlock, kBreadcrumbStageCommitted);
    if (result != 0u)
        return result;

    u32 verifiedGeneration = 0u;
    if (!validOwnedSramSnapshot(targetBlock, verifiedGeneration) ||
        verifiedGeneration != nextGeneration)
        return 0xE724u;
    return advanceSramBreadcrumb(targetBlock, kBreadcrumbStageFinalVerified);
}

static void ensureSramShadowInitialized()
{
    if (usesGameOwnedSramSnapshot()) {
        ensureOwnedSramShadowInitialized();
        return;
    }

    volatile SramRuntimeState &state = sramRuntimeState();
    if (state.magic == kSramRuntimeStateMagic && state.initialized == 1u) {
        *reinterpret_cast<volatile u32 *>(kBiosUserIrqVector) =
            reinterpret_cast<uptr>(&gbashSramHotkeyIrqEntry);
        return;
    }

    // Restore only after the game's startup has finished clearing EWRAM.
    // Program-only journal mode gathers the newest committed record across
    // safe FF fragments; the legacy mirror path remains a plain ROM-array read.
    if (usesSramProgramOnlyJournal()) {
        const SramJournalSelection record = newestSramJournalRecord();
        if (!record.valid || !journalReadBytes(
                record.recordIndex * gRuntimeConfig.sramJournalRecordBytes,
                reinterpret_cast<u8 *>(gRuntimeConfig.sramShadowAddress),
                gRuntimeConfig.sramSizeBytes)) {
            fillBytes(reinterpret_cast<void *>(gRuntimeConfig.sramShadowAddress), 0xFFu, gRuntimeConfig.sramSizeBytes);
        }
    } else {
        copyFromVolatile(
            reinterpret_cast<void *>(gRuntimeConfig.sramShadowAddress),
            reinterpret_cast<const volatile void *>(gRuntimeConfig.sramMirrorAddress),
            gRuntimeConfig.sramSizeBytes);
    }

    state.hotkeyLatched = 0u;
    state.initialized = 1u;
    state.magic = kSramRuntimeStateMagic;
    *reinterpret_cast<volatile u32 *>(kBiosUserIrqVector) =
        reinterpret_cast<uptr>(&gbashSramHotkeyIrqEntry);
}

static bool sramShadowOffset(const volatile u8 *address, u32 byteCount, u32 &offset)
{
    if (address == nullptr)
        return false;
    const uptr raw = reinterpret_cast<uptr>(address);

    if (raw >= gRuntimeConfig.originalSramBaseAddress &&
        raw < gRuntimeConfig.originalSramBaseAddress + gRuntimeConfig.sramSizeBytes) {
        offset = raw - gRuntimeConfig.originalSramBaseAddress;
    } else if (raw >= gRuntimeConfig.sramShadowAddress &&
               raw < gRuntimeConfig.sramShadowAddress + gRuntimeConfig.sramSizeBytes) {
        offset = raw - gRuntimeConfig.sramShadowAddress;
    } else {
        return false;
    }

    return offset <= gRuntimeConfig.sramSizeBytes &&
           byteCount <= gRuntimeConfig.sramSizeBytes - offset;
}

static bool sramRangeCanProgram(u32 targetAddress, const u8 *source, u32 byteCount)
{
    const auto *existing = reinterpret_cast<const volatile u8 *>(targetAddress);
    for (u32 index = 0; index < byteCount; ++index) {
        if ((existing[index] & source[index]) != source[index])
            return false;
    }
    return true;
}

static bool sramMirrorMatchesShadow()
{
    const auto *mirror = reinterpret_cast<const volatile u8 *>(gRuntimeConfig.sramMirrorAddress);
    const auto *shadow = reinterpret_cast<const u8 *>(gRuntimeConfig.sramShadowAddress);
    for (u32 index = 0; index < gRuntimeConfig.sramSizeBytes; ++index) {
        if (mirror[index] != shadow[index])
            return false;
    }
    return true;
}

static u32 commitSramShadowToMirror()
{
    if (usesSramProgramOnlyJournal())
        return commitSramProgramOnlyJournal();

    const auto *shadow = reinterpret_cast<const u8 *>(gRuntimeConfig.sramShadowAddress);
    if (sramMirrorMatchesShadow())
        return 0u;

    if (sramRangeCanProgram(gRuntimeConfig.sramMirrorAddress, shadow, gRuntimeConfig.sramSizeBytes)) {
        copyRamWorker();
        const u32 result = programBytesWorker()(
            gRuntimeConfig.sramMirrorAddress,
            shadow,
            gRuntimeConfig.sramSizeBytes,
            gRuntimeConfig.norProgramCommand);
        if (result != 0u)
            return result;
        return sramMirrorMatchesShadow() ? 0u : 0xE503u;
    }

    // A 0->1 update is intentionally allowed only from the explicit hotkey
    // path. Preserve the old mirror in the private backup before rebuilding.
    copyRamWorker();
    u32 result = eraseBlockWorker()(gRuntimeConfig.sramBackupAddress, gRuntimeConfig.norProgramCommand);
    if (result != 0u)
        return result;
    result = cloneWithPatchWorker()(
        gRuntimeConfig.sramBackupAddress,
        gRuntimeConfig.sramMirrorAddress,
        nullptr,
        0u,
        0u,
        gRuntimeConfig.sramSizeBytes,
        gRuntimeConfig.norProgramCommand);
    if (result != 0u)
        return result;

    copyRamWorker();
    result = eraseBlockWorker()(gRuntimeConfig.sramMirrorAddress, gRuntimeConfig.norProgramCommand);
    if (result != 0u)
        return result;
    result = cloneWithPatchWorker()(
        gRuntimeConfig.sramMirrorAddress,
        gRuntimeConfig.sramBackupAddress,
        shadow,
        0u,
        gRuntimeConfig.sramSizeBytes,
        gRuntimeConfig.sramSizeBytes,
        gRuntimeConfig.norProgramCommand);
    if (result != 0u)
        return result;
    return sramMirrorMatchesShadow() ? 0u : 0xE504u;
}

struct SramHotkeyHardwareState {
    u16 ime;
    u16 timerControl[4];
    u16 dmaControl[4];
    u16 disabledDmaMask;
};

static void enterSramHotkeyCritical(SramHotkeyHardwareState &state)
{
    volatile u16 &ime = reg16(0x04000208u);
    state.ime = ime;
    ime = 0u;

    volatile u16 *timerControl[4] = {
        reinterpret_cast<volatile u16 *>(0x04000102u),
        reinterpret_cast<volatile u16 *>(0x04000106u),
        reinterpret_cast<volatile u16 *>(0x0400010Au),
        reinterpret_cast<volatile u16 *>(0x0400010Eu),
    };
    for (u32 index = 0; index < 4u; ++index) {
        state.timerControl[index] = *timerControl[index];
        *timerControl[index] = 0u;
    }

    volatile u16 *dmaControl[4] = {
        reinterpret_cast<volatile u16 *>(0x040000BAu),
        reinterpret_cast<volatile u16 *>(0x040000C6u),
        reinterpret_cast<volatile u16 *>(0x040000D2u),
        reinterpret_cast<volatile u16 *>(0x040000DEu),
    };
    state.disabledDmaMask = 0u;
    for (u32 index = 0; index < 4u; ++index) {
        const u16 control = *dmaControl[index];
        state.dmaControl[index] = control;
        if ((control & 0x8000u) == 0u)
            continue;

        // DMA1/2 SPECIAL timing is normally DirectSound FIFO. Timers are
        // stopped above, so leave those channels enabled to preserve their
        // hidden current-source latch across the commit/resume cycle.
        const bool directSound = (index == 1u || index == 2u) && (control & 0x3000u) == 0x3000u;
        if (directSound)
            continue;

        *dmaControl[index] = static_cast<u16>(control & 0x7FFFu);
        state.disabledDmaMask = static_cast<u16>(state.disabledDmaMask | static_cast<u16>(1u << index));
    }
}

static void leaveSramHotkeyCritical(const SramHotkeyHardwareState &state)
{
    volatile u16 *dmaControl[4] = {
        reinterpret_cast<volatile u16 *>(0x040000BAu),
        reinterpret_cast<volatile u16 *>(0x040000C6u),
        reinterpret_cast<volatile u16 *>(0x040000D2u),
        reinterpret_cast<volatile u16 *>(0x040000DEu),
    };
    for (u32 index = 0; index < 4u; ++index) {
        if ((state.disabledDmaMask & static_cast<u16>(1u << index)) != 0u)
            *dmaControl[index] = state.dmaControl[index];
    }

    volatile u16 *timerControl[4] = {
        reinterpret_cast<volatile u16 *>(0x04000102u),
        reinterpret_cast<volatile u16 *>(0x04000106u),
        reinterpret_cast<volatile u16 *>(0x0400010Au),
        reinterpret_cast<volatile u16 *>(0x0400010Eu),
    };
    for (u32 index = 0; index < 4u; ++index)
        *timerControl[index] = state.timerControl[index];

    reg16(0x04000208u) = state.ime;
}

__attribute__((section(".text.runtime"), used))
u32 gbashSramBootInit()
{
    volatile SramRuntimeState &state = sramRuntimeState();
    state.magic = 0u;
    state.initialized = 0u;
    state.hotkeyLatched = 0u;
    state.chainTarget = gRuntimeConfig.sramIrqDispatcherAddress;

    if (usesGameOwnedSramSnapshot()) {
        // Install only the transparent init interposer. Nintendo's validated
        // startup/dynamic writes of 03007FFC have been redirected to
        // state.chainTarget, so the BIOS vector remains ours while the game is
        // still free to change its real dispatcher target. EWRAM may be cleared
        // after this point; the redirected startup writes repopulate chainTarget
        // and the IRQ helper falls back to the immutable dispatcher if needed.
        *reinterpret_cast<volatile u32 *>(kBiosUserIrqVector) =
            reinterpret_cast<uptr>(&gbashSramOwnedShadowInitIrqEntry);
    }
    return gRuntimeConfig.sramBootResumeAddress;
}

__attribute__((section(".text.runtime"), used))
u32 gbashSramOwnedShadowEnsureInitialized()
{
    if (usesGameOwnedSramSnapshot())
        ensureOwnedSramShadowInitialized();
    return 0u;
}

__attribute__((section(".text.runtime"), used))
u32 gbashSramReadMirror(const volatile u8 *source, u8 *destination, u32 byteCount)
{
    if (destination == nullptr || byteCount == 0u)
        return 0u;

    const bool extendedResetSemantics = usesFullReadSramRefresh() ||
        gRuntimeConfig.sramRefreshTriggerOffset != 0xFFFFFFFFu;
    if (extendedResetSemantics && usesGameOwnedSramSnapshot() &&
        usesFullReadSramRefresh() &&
        byteCount == gRuntimeConfig.sramSizeBytes &&
        reinterpret_cast<uptr>(destination) == gRuntimeConfig.sramShadowAddress &&
        (reinterpret_cast<uptr>(source) == gRuntimeConfig.originalSramBaseAddress ||
         reinterpret_cast<uptr>(source) == gRuntimeConfig.sramShadowAddress)) {
        // Explicitly proven internal-soft-reset refresh only.
        restoreOwnedSramShadowFromSnapshot();
    } else {
        ensureSramShadowInitialized();
    }

    u32 offset = 0u;
    if (!sramShadowOffset(source, byteCount, offset))
        return 1u;
    copyBytes(destination, reinterpret_cast<const void *>(gRuntimeConfig.sramShadowAddress + offset), byteCount);
    return 0u;
}

static bool readableSramWriteSource(const u8 *source, u32 byteCount)
{
    if (internalRamRange(source, byteCount))
        return true;
    const uptr start = reinterpret_cast<uptr>(source);
    const uptr end = start + byteCount;
    return end >= start && start >= 0x08000000u && end <= 0x0E000000u;
}

__attribute__((section(".text.runtime"), used))
u32 gbashSramWriteMirror(const u8 *source, volatile u8 *destination, u32 byteCount)
{
    if (source == nullptr || destination == nullptr || byteCount == 0u)
        return 0u;

    const bool extendedResetSemantics = usesFullReadSramRefresh() ||
        gRuntimeConfig.sramRefreshTriggerOffset != 0xFFFFFFFFu;
    // Hardware-proven v0.16 contract for normal SRAM games: SramWrite accepts
    // an internal-RAM source only. ROM-source self-test writes are admitted
    // solely when a separate structural reset/preload proof exists.
    if (extendedResetSemantics) {
        if (!readableSramWriteSource(source, byteCount))
            return 1u;
    } else if (!internalRamRange(source, byteCount)) {
        return 1u;
    }

    u32 offset = 0u;
    if (!sramShadowOffset(destination, byteCount, offset))
        return 1u;

    if (extendedResetSemantics && usesGameOwnedSramSnapshot() &&
        gRuntimeConfig.sramRefreshTriggerOffset != 0xFFFFFFFFu &&
        offset == gRuntimeConfig.sramRefreshTriggerOffset) {
        restoreOwnedSramShadowFromSnapshot();
    } else {
        ensureSramShadowInitialized();
    }

    copyBytes(reinterpret_cast<void *>(gRuntimeConfig.sramShadowAddress + offset), source, byteCount);
    return 0u;
}

static bool sramVirtualizedReadAddress(const volatile u8 *address, u32 byteCount, const volatile u8 *&mapped)
{
    u32 offset = 0u;
    if (!sramShadowOffset(address, byteCount, offset))
        return false;
    mapped = reinterpret_cast<const volatile u8 *>(gRuntimeConfig.sramShadowAddress + offset);
    return true;
}

__attribute__((section(".text.runtime"), used))
u32 gbashSramVerifyMirror(const u8 *source, const volatile u8 *target, u32 byteCount)
{
    if (source == nullptr || target == nullptr || byteCount == 0u)
        return 0u;
    ensureSramShadowInitialized();

    const bool extendedResetSemantics = usesFullReadSramRefresh() ||
        gRuntimeConfig.sramRefreshTriggerOffset != 0xFFFFFFFFu;
    if (!extendedResetSemantics) {
        // Exact v0.16 semantics: virtualize argument #2 (the SRAM target) only.
        u32 offset = 0u;
        if (!sramShadowOffset(target, byteCount, offset))
            return 1u;
        const auto *stored = reinterpret_cast<const u8 *>(gRuntimeConfig.sramShadowAddress + offset);
        for (u32 index = 0u; index < byteCount; ++index) {
            if (stored[index] != source[index])
                return gRuntimeConfig.originalSramBaseAddress + offset + index;
        }
        return 0u;
    }

    // Only structurally proven reset/self-test lifecycles need the broader
    // bidirectional compare virtualization required by that proven lifecycle.
    const volatile u8 *mappedSource = source;
    const volatile u8 *mappedTarget = target;
    const bool sourceVirtual = sramVirtualizedReadAddress(
        reinterpret_cast<const volatile u8 *>(source), byteCount, mappedSource);
    const bool targetVirtual = sramVirtualizedReadAddress(target, byteCount, mappedTarget);

    if (!sourceVirtual && !readableSramWriteSource(source, byteCount))
        return reinterpret_cast<uptr>(target);

    const uptr targetStart = reinterpret_cast<uptr>(target);
    const uptr targetEnd = targetStart + byteCount;
    const bool targetInternal = targetEnd >= targetStart &&
        ((targetStart >= 0x02000000u && targetEnd <= 0x02040000u) ||
         (targetStart >= 0x03000000u && targetEnd <= 0x03008000u));
    if (!targetVirtual && !targetInternal)
        return reinterpret_cast<uptr>(target);

    for (u32 index = 0u; index < byteCount; ++index) {
        if (mappedTarget[index] != mappedSource[index])
            return reinterpret_cast<uptr>(target) + index;
    }
    return 0u;
}

__attribute__((section(".text.runtime"), used))
u32 gbashOwnedSramHotkeyCommit()
{
    // Full-size/internal-hole SRAM has a separate IRQ entry and transaction
    // entry so the established appended-tail hotkey path does not inherit
    // any A/B-snapshot stack frame or control flow.
    ensureSramShadowInitialized();
    const SramSnapshotSelection current = newestOwnedSramSnapshot();
    if (ownedSnapshotMatchesShadow(current))
        return 2u;

    SramHotkeyHardwareState hardware;
    enterSramHotkeyCritical(hardware);
    const u32 result = commitOwnedSramSnapshot();
    leaveSramHotkeyCritical(hardware);
    return result;
}

__attribute__((section(".text.runtime"), used))
u32 gbashSramHotkeyCommit()
{
    // Established appended-tail path only. Full-size SRAM never enters here.
    ensureSramShadowInitialized();
    if (sramMirrorMatchesShadow())
        return 2u;

    SramHotkeyHardwareState hardware;
    enterSramHotkeyCritical(hardware);
    const u32 result = commitSramShadowToMirror();
    leaveSramHotkeyCritical(hardware);
    return result;
}

__attribute__((section(".text.runtime"), used))
u32 gbashNoOp()
{
    return 0u;
}

} // extern "C"
