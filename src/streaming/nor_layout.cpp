#include "gbasave/streaming/nor_layout.h"

#include <stddef.h>

static void zero_bytes(void *ptr, uint32_t bytes)
{
    uint8_t *p = static_cast<uint8_t *>(ptr);
    while (bytes-- != 0u) *p++ = 0u;
}

namespace {

constexpr uint32_t kSramPayloadReservationBytes = 0x4000u;
constexpr uint32_t kEepromPayloadReservationBytes = 0x1400u;
constexpr uint32_t kFlashCompactPayloadReservationBytes = 0x2000u;
constexpr uint32_t kFlashCompactStorageBlockCount = 6u;

uint32_t alignUp(uint32_t value, uint32_t alignment)
{
    if (alignment == 0u) return value;
    const uint32_t mask = alignment - 1u;
    if ((alignment & mask) != 0u || value > UINT32_MAX - mask) return UINT32_MAX;
    return (value + mask) & ~mask;
}

bool sameBank(uint32_t start, uint32_t bytes, uint32_t bankBytes)
{
    if (bytes == 0u || bankBytes == 0u || start > UINT32_MAX - (bytes - 1u)) return false;
    return start / bankBytes == (start + bytes - 1u) / bankBytes;
}

bool regionHasRange(const GbasaveExactRomRegion &region, uint32_t address, uint32_t bytes)
{
    if (bytes == 0u || address > UINT32_MAX - bytes) return false;
    const uint32_t end = address + bytes;
    if (region.start > UINT32_MAX - region.size) return false;
    return address >= region.start && end <= region.start + region.size;
}

bool executableFfRegion(const GbasaveExactRomRegion &region)
{
    if ((region.flags & GBASAVE_ROM_REGION_FF) == 0u) return false;
    if ((region.flags & GBASAVE_ROM_REGION_PROTECTED_NEARBY) != 0u) return false;
    return (region.flags & (GBASAVE_ROM_REGION_SAFE_CODE | GBASAVE_ROM_REGION_SUPERFW)) != 0u;
}

bool saveBankFfRegion(const GbasaveExactRomRegion &region)
{
    if ((region.flags & GBASAVE_ROM_REGION_FF) == 0u) return false;
    if ((region.flags & GBASAVE_ROM_REGION_PROTECTED_NEARBY) != 0u) return false;
    return (region.flags & (GBASAVE_ROM_REGION_SAFE_CODE |
                            GBASAVE_ROM_REGION_POINTER_FREE |
                            GBASAVE_ROM_REGION_SUPERFW)) != 0u;
}

bool chooseInternalPayload(const GbasaveExactRomPlan &plan,
                           uint32_t span,
                           uint32_t bankBytes,
                           uint32_t alignment,
                           uint32_t &payload,
                           uint32_t *regionStart,
                           uint32_t *regionBytes)
{
    uint32_t best = UINT32_MAX;
    uint32_t bestRegionStart = 0u;
    uint32_t bestRegionBytes = 0u;
    for (uint32_t i = 0u; i < plan.region_count; ++i) {
        const auto &region = plan.region[i];
        if (!executableFfRegion(region)) continue;
        uint32_t candidate = alignUp(region.start, alignment);
        if (candidate == UINT32_MAX) continue;
        if (!regionHasRange(region, candidate, span) || !sameBank(candidate, span, bankBytes)) {
            candidate = alignUp(candidate, bankBytes);
            if (candidate == UINT32_MAX || !regionHasRange(region, candidate, span) ||
                !sameBank(candidate, span, bankBytes)) continue;
        }
        if (candidate < best) {
            best = candidate;
            bestRegionStart = region.start;
            bestRegionBytes = region.size;
        }
    }
    if (best == UINT32_MAX) return false;
    payload = best;
    if (regionStart) *regionStart = bestRegionStart;
    if (regionBytes) *regionBytes = bestRegionBytes;
    return true;
}

bool chooseInternalSramLayout(const GbasaveExactRomPlan &plan,
                              uint32_t span,
                              uint32_t bankBytes,
                              uint32_t &payload,
                              uint32_t &saveBank,
                              uint32_t &regionStart,
                              uint32_t &regionBytes)
{
    for (uint32_t pi = 0u; pi < plan.region_count; ++pi) {
        const auto &payloadRegion = plan.region[pi];
        if (!executableFfRegion(payloadRegion)) continue;
        uint32_t candidatePayload = alignUp(payloadRegion.start, 4u);
        if (candidatePayload == UINT32_MAX ||
            !regionHasRange(payloadRegion, candidatePayload, span)) continue;
        if (!sameBank(candidatePayload, span, bankBytes)) {
            candidatePayload = alignUp(candidatePayload, bankBytes);
            if (candidatePayload == UINT32_MAX ||
                !regionHasRange(payloadRegion, candidatePayload, span) ||
                !sameBank(candidatePayload, span, bankBytes)) continue;
        }
        if (candidatePayload > plan.rom_size || span > plan.rom_size - candidatePayload) continue;

        for (uint32_t si = 0u; si < plan.region_count; ++si) {
            const auto &saveRegion = plan.region[si];
            if (!saveBankFfRegion(saveRegion) || saveRegion.size < bankBytes) continue;
            uint32_t candidateSave = alignUp(saveRegion.start, bankBytes);
            if (candidateSave == UINT32_MAX || saveRegion.start > UINT32_MAX - saveRegion.size) continue;
            const uint32_t saveRegionEnd = saveRegion.start + saveRegion.size;
            while (candidateSave <= plan.rom_size && candidateSave <= saveRegionEnd &&
                   bankBytes <= saveRegionEnd - candidateSave) {
                if (candidateSave / bankBytes != candidatePayload / bankBytes &&
                    candidateSave <= plan.rom_size && bankBytes <= plan.rom_size - candidateSave) {
                    payload = candidatePayload;
                    saveBank = candidateSave;
                    regionStart = payloadRegion.start;
                    regionBytes = payloadRegion.size;
                    return true;
                }
                if (candidateSave > UINT32_MAX - bankBytes) break;
                candidateSave += bankBytes;
            }
        }
    }
    return false;
}

bool rangesOverlap(uint32_t a, uint32_t aBytes, uint32_t b, uint32_t bBytes)
{
    if (aBytes == 0u || bBytes == 0u || a > UINT32_MAX - aBytes || b > UINT32_MAX - bBytes) return true;
    return a < b + bBytes && b < a + aBytes;
}

bool chooseInternalSparseStorage(const GbasaveExactRomPlan &plan,
                                 uint32_t payload,
                                 uint32_t payloadBytes,
                                 uint32_t blockBytes,
                                 uint32_t blockCount,
                                 uint32_t bankBytes,
                                 uint32_t *storage)
{
    if (!storage || blockBytes == 0u || blockCount == 0u || bankBytes == 0u ||
        blockCount > GBASAVE_NOR_SAVE_LAYOUT_MAX_STORAGE) return false;

    uint32_t found = 0u;
    while (found < blockCount) {
        uint32_t best = UINT32_MAX;
        for (uint32_t ri = 0u; ri < plan.region_count; ++ri) {
            const auto &region = plan.region[ri];
            if (!saveBankFfRegion(region) || region.start > UINT32_MAX - region.size) continue;
            const uint32_t regionEnd = region.start + region.size;
            uint32_t candidate = alignUp(region.start, blockBytes);
            if (candidate == UINT32_MAX) continue;
            while (candidate <= plan.rom_size && candidate <= regionEnd &&
                   blockBytes <= regionEnd - candidate && blockBytes <= plan.rom_size - candidate) {
                bool usable = candidate / bankBytes != payload / bankBytes &&
                              !rangesOverlap(candidate, blockBytes, payload, payloadBytes);
                for (uint32_t i = 0u; usable && i < found; ++i)
                    if (rangesOverlap(candidate, blockBytes, storage[i], blockBytes)) usable = false;
                if (usable && candidate < best) best = candidate;
                if (candidate > UINT32_MAX - blockBytes) break;
                candidate += blockBytes;
            }
        }
        if (best == UINT32_MAX) return false;
        storage[found++] = best;
    }
    return true;
}

bool chooseSparseStorage(uint32_t romSize,
                         uint32_t payload,
                         uint32_t blockBytes,
                         uint32_t blockCount,
                         uint32_t bankBytes,
                         uint32_t capacity,
                         uint32_t *storage)
{
    if (!storage || blockBytes == 0u || blockCount == 0u || bankBytes == 0u ||
        blockCount > GBASAVE_NOR_SAVE_LAYOUT_MAX_STORAGE) return false;
    uint32_t start = alignUp(romSize, bankBytes);
    if (start == UINT32_MAX) return false;
    if (start / bankBytes == payload / bankBytes) {
        if (start > UINT32_MAX - bankBytes) return false;
        start += bankBytes;
    }
    if (blockCount > UINT32_MAX / blockBytes) return false;
    const uint32_t storageBytes = blockCount * blockBytes;
    if (start > capacity || storageBytes > capacity - start) return false;
    for (uint32_t i = 0u; i < blockCount; ++i) storage[i] = start + i * blockBytes;
    return true;
}

bool resolveSram(const GbasaveExactRomPlan &plan,
                 const GbasaveNorSaveTarget &target,
                 GbasaveNorSaveLayout &out)
{
    if (target.rww_bank_bytes == 0u || target.erase_block_bytes == 0u ||
        plan.save_plan.irq_count == 0u) return false;

    uint32_t payload = 0u;
    uint32_t saveBank = 0u;
    uint32_t ffStart = 0u;
    uint32_t ffBytes = 0u;
    const uint32_t span = kSramPayloadReservationBytes;
    const uint32_t bank = target.rww_bank_bytes;

    if (chooseInternalSramLayout(plan, span, bank, payload, saveBank, ffStart, ffBytes)) {
        out.route = GBASAVE_NOR_SAVE_LAYOUT_RWW_SRAM;
        out.flags = GBASAVE_NOR_SAVE_LAYOUT_PAYLOAD_INTERNAL_FF |
                    GBASAVE_NOR_SAVE_LAYOUT_RWW_SEPARATE_BANK;
        out.payload_offset = payload;
        out.payload_reserved_bytes = span;
        out.storage_block_bytes = bank;
        out.storage_count = 1u;
        out.storage[0] = saveBank;
        out.output_bytes = plan.rom_size;
        out.minimum_capacity_bytes = plan.rom_size;
        out.selected_ff_region_start = ffStart;
        out.selected_ff_region_bytes = ffBytes;
        return true;
    }

    payload = alignUp(plan.rom_size, 4u);
    if (payload == UINT32_MAX) return false;
    if (!sameBank(payload, span, bank)) payload = alignUp(payload, bank);
    if (payload == UINT32_MAX || payload > target.capacity_bytes ||
        span > target.capacity_bytes - payload) return false;
    const uint32_t payloadEnd = payload + span;

    saveBank = alignUp(payloadEnd, bank);
    const uint32_t firstBankAfterRom = alignUp(plan.rom_size, bank);
    if (saveBank == UINT32_MAX || firstBankAfterRom == UINT32_MAX) return false;
    if (saveBank < firstBankAfterRom) saveBank = firstBankAfterRom;
    if (payload / bank == saveBank / bank) {
        if (saveBank > UINT32_MAX - bank) return false;
        saveBank += bank;
    }
    if (saveBank > target.capacity_bytes || bank > target.capacity_bytes - saveBank) return false;

    out.route = GBASAVE_NOR_SAVE_LAYOUT_RWW_SRAM;
    out.flags = GBASAVE_NOR_SAVE_LAYOUT_PAYLOAD_APPENDED |
                GBASAVE_NOR_SAVE_LAYOUT_RWW_SEPARATE_BANK;
    out.payload_offset = payload;
    out.payload_reserved_bytes = span;
    out.storage_block_bytes = bank;
    out.storage_count = 1u;
    out.storage[0] = saveBank;
    out.output_bytes = saveBank + bank;
    out.minimum_capacity_bytes = out.output_bytes;
    return true;
}

bool resolveProtocolSave(const GbasaveExactRomPlan &plan,
                         const GbasaveNorSaveTarget &target,
                         GbasaveNorSaveLayout &out)
{
    if (target.erase_block_bytes == 0u || target.rww_bank_bytes == 0u) return false;

    uint32_t payloadSpan = 0u;
    uint32_t storageCount = 0u;
    uint8_t route = GBASAVE_NOR_SAVE_LAYOUT_NONE;
    switch (plan.save_type) {
    case SFW_SAVE_EEPROM4K:
    case SFW_SAVE_EEPROM64K:
        payloadSpan = kEepromPayloadReservationBytes;
        storageCount = 2u;
        route = GBASAVE_NOR_SAVE_LAYOUT_RWW_EEPROM;
        break;
    case SFW_SAVE_FLASH512K:
    case SFW_SAVE_FLASH1024K:
        payloadSpan = kFlashCompactPayloadReservationBytes;
        storageCount = kFlashCompactStorageBlockCount;
        route = GBASAVE_NOR_SAVE_LAYOUT_RWW_FLASH_COMPACT;
        break;
    default:
        return false;
    }

    const uint32_t block = target.erase_block_bytes;
    const uint32_t bank = target.rww_bank_bytes;
    uint32_t payload = alignUp(plan.rom_size, block);
    bool payloadEraseBlock = false;
    bool storageInternalFf = false;

    if (payload != UINT32_MAX && payload >= plan.rom_size &&
        payload <= target.capacity_bytes && payloadSpan <= target.capacity_bytes - payload &&
        sameBank(payload, payloadSpan, bank) &&
        chooseSparseStorage(plan.rom_size, payload, block, storageCount, bank,
                            target.capacity_bytes, out.storage)) {
        payloadEraseBlock = true;
    } else {
        uint32_t ffStart = 0u;
        uint32_t ffBytes = 0u;
        if (!chooseInternalPayload(plan, payloadSpan, bank, block, payload, &ffStart, &ffBytes)) return false;
        if (!chooseSparseStorage(plan.rom_size, payload, block, storageCount, bank,
                                 target.capacity_bytes, out.storage)) {
            /* Internal journal storage is accepted only from an exact analyzer plan:
             * streaming plans carry no trusted region table and therefore remain
             * fail-closed when there is no physical tail capacity. */
            if (plan.region_count == 0u ||
                !chooseInternalSparseStorage(plan, payload, payloadSpan, block, storageCount, bank,
                                             out.storage)) return false;
            storageInternalFf = true;
        }
        out.selected_ff_region_start = ffStart;
        out.selected_ff_region_bytes = ffBytes;
    }

    out.route = route;
    out.flags = GBASAVE_NOR_SAVE_LAYOUT_STORAGE_SPARSE |
                (payloadEraseBlock ? GBASAVE_NOR_SAVE_LAYOUT_PAYLOAD_ERASE_BLOCK
                                   : GBASAVE_NOR_SAVE_LAYOUT_PAYLOAD_INTERNAL_FF) |
                (storageInternalFf ? static_cast<uint32_t>(GBASAVE_NOR_SAVE_LAYOUT_STORAGE_INTERNAL_FF) : 0u);
    out.payload_offset = payload;
    out.payload_reserved_bytes = payloadSpan;
    out.storage_block_bytes = block;
    out.storage_count = static_cast<uint8_t>(storageCount);

    uint32_t requiredEnd = payload + payloadSpan;
    for (uint32_t i = 0u; i < storageCount; ++i) {
        if (out.storage[i] > UINT32_MAX - block) return false;
        const uint32_t end = out.storage[i] + block;
        if (end > requiredEnd) requiredEnd = end;
    }
    if (plan.rom_size > requiredEnd) requiredEnd = plan.rom_size;
    out.output_bytes = plan.rom_size;
    out.minimum_capacity_bytes = requiredEnd;
    return true;
}

} // namespace

extern "C" int gbasave_nor_save_layout_resolve(
    const GbasaveExactRomPlan *romPlan,
    const GbasaveNorSaveTarget *target,
    GbasaveNorSaveLayout *out)
{
    if (!romPlan || !target || !out || romPlan->rom_size == 0u ||
        target->capacity_bytes < romPlan->rom_size) return GBASAVE_NOR_SAVE_LAYOUT_INVALID;

    zero_bytes(out, sizeof(*out));
    for (uint32_t i = 0u; i < GBASAVE_NOR_SAVE_LAYOUT_MAX_STORAGE; ++i) out->storage[i] = UINT32_MAX;
    out->selected_ff_region_start = UINT32_MAX;

    if (romPlan->source_kind == GBASAVE_ROM_PLAN_SOURCE_SUPERFW)
        out->flags |= GBASAVE_NOR_SAVE_LAYOUT_SOURCE_SUPERFW;

    bool ready = false;
    if (romPlan->save_type == SFW_SAVE_SRAM)
        ready = resolveSram(*romPlan, *target, *out);
    else
        ready = resolveProtocolSave(*romPlan, *target, *out);

    if (!ready) {
        zero_bytes(out, sizeof(*out));
        return GBASAVE_NOR_SAVE_LAYOUT_UNSUPPORTED;
    }
    if (romPlan->source_kind == GBASAVE_ROM_PLAN_SOURCE_SUPERFW)
        out->flags |= GBASAVE_NOR_SAVE_LAYOUT_SOURCE_SUPERFW;
    return GBASAVE_NOR_SAVE_LAYOUT_READY;
}
