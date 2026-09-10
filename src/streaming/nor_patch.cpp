#include "gbasave/streaming/nor_patch.h"
#include "m36_assets.h"

#include <stddef.h>

namespace {

constexpr uint32_t kGbaRomBase = 0x08000000u;
constexpr uint32_t kIrqChainLiteral = 0x03007FF4u;

void zeroBytes(void *ptr, uint32_t bytes)
{
    uint8_t *p = static_cast<uint8_t *>(ptr);
    while (bytes-- != 0u) *p++ = 0u;
}

void copyBytes(uint8_t *dst, const uint8_t *src, uint32_t bytes)
{
    while (bytes-- != 0u) *dst++ = *src++;
}

void write32(uint8_t *dst, uint32_t value)
{
    dst[0] = static_cast<uint8_t>(value);
    dst[1] = static_cast<uint8_t>(value >> 8u);
    dst[2] = static_cast<uint8_t>(value >> 16u);
    dst[3] = static_cast<uint8_t>(value >> 24u);
}

uint32_t decodeResetTarget(uint32_t instruction)
{
    if ((instruction & 0xFF000000u) != 0xEA000000u) return 0u;
    int32_t displacement = static_cast<int32_t>((instruction & 0x00FFFFFFu) << 8u) >> 6u;
    return static_cast<uint32_t>(static_cast<int32_t>(0x08000008u) + displacement);
}

bool makeArmBranch(uint32_t from, uint32_t to, uint32_t *out)
{
    if (!out) return false;
    const int64_t delta = static_cast<int64_t>(to) - static_cast<int64_t>(from + 8u);
    if ((delta & 3ll) != 0ll || delta < -0x02000000ll || delta > 0x01FFFFFCl) return false;
    *out = 0xEA000000u | (static_cast<uint32_t>(delta >> 2u) & 0x00FFFFFFu);
    return true;
}

bool overlayOne(uint32_t rangeOffset, uint8_t *dst, uint32_t bytes,
                uint32_t patchOffset, const uint8_t *src, uint32_t srcBytes)
{
    if (!dst || !src || bytes == 0u || srcBytes == 0u) return true;
    if (rangeOffset > UINT32_MAX - bytes || patchOffset > UINT32_MAX - srcBytes) return false;
    const uint32_t rangeEnd = rangeOffset + bytes;
    const uint32_t patchEnd = patchOffset + srcBytes;
    const uint32_t start = rangeOffset > patchOffset ? rangeOffset : patchOffset;
    const uint32_t end = rangeEnd < patchEnd ? rangeEnd : patchEnd;
    if (start >= end) return true;
    copyBytes(dst + (start - rangeOffset), src + (start - patchOffset), end - start);
    return true;
}

struct StubDescriptor {
    uint32_t offset;
    uint32_t length;
    uint32_t dispatchOffset;
    bool hasDispatch;
};

StubDescriptor stubForKind(uint8_t kind, uint8_t saveType)
{
    using namespace gbasave::generated_m36_assets;
    switch (kind) {
    case SFW_OP_EEPROM_READ:
        return {static_cast<uint32_t>(kStubEepromReadOffset), static_cast<uint32_t>(kStubEepromReadLength), 0u, false};
    case SFW_OP_FLASH_READ:
        return {static_cast<uint32_t>(kStubFlashReadOffset), static_cast<uint32_t>(kStubFlashReadLength), static_cast<uint32_t>(kStubFlashReadDispatchOffset), true};
    case SFW_OP_FLASH_ERASE_CHIP:
        return {static_cast<uint32_t>(kStubFlashEraseChipOffset), static_cast<uint32_t>(kStubFlashEraseChipLength), static_cast<uint32_t>(kStubFlashEraseChipDispatchOffset), true};
    case SFW_OP_FLASH_ERASE_SECTOR:
        return {static_cast<uint32_t>(kStubFlashEraseSectorOffset), static_cast<uint32_t>(kStubFlashEraseSectorLength), static_cast<uint32_t>(kStubFlashEraseSectorDispatchOffset), true};
    case SFW_OP_FLASH_WRITE_SECTOR:
        return {static_cast<uint32_t>(kStubFlashWriteSectorOffset), static_cast<uint32_t>(kStubFlashWriteSectorLength), static_cast<uint32_t>(kStubFlashWriteSectorDispatchOffset), true};
    case SFW_OP_FLASH_WRITE_BYTE:
        return {static_cast<uint32_t>(kStubFlashWriteByteOffset), static_cast<uint32_t>(kStubFlashWriteByteLength), static_cast<uint32_t>(kStubFlashWriteByteDispatchOffset), true};
    case SFW_OP_FLASH_IDENT:
        return saveType == SFW_SAVE_FLASH1024K
            ? StubDescriptor{static_cast<uint32_t>(kStubFlashIdent1mOffset), static_cast<uint32_t>(kStubFlashIdent1mLength), 0u, false}
            : StubDescriptor{static_cast<uint32_t>(kStubFlashIdent512Offset), static_cast<uint32_t>(kStubFlashIdent512Length), 0u, false};
    case SFW_OP_FLASH_VERIFY:
    case SFW_OP_RAW_THUMB_RET0:
        return {static_cast<uint32_t>(kStubThumbRet0Offset), static_cast<uint32_t>(kStubThumbRet0Length), 0u, false};
    default:
        return {0u,0u,0u,false};
    }
}

bool overlayStub(uint32_t rangeOffset, uint8_t *data, uint32_t bytes,
                 uint32_t targetOffset, const StubDescriptor &stub, uint32_t dispatch)
{
    using namespace gbasave::generated_m36_assets;
    if (stub.length == 0u || stub.length > 64u || stub.offset > kSfwSaveStubsSize ||
        stub.length > kSfwSaveStubsSize - stub.offset) return false;
    uint8_t temp[64];
    copyBytes(temp, kSfwSaveStubs + stub.offset, stub.length);
    if (stub.hasDispatch) {
        if (stub.dispatchOffset > stub.length || 4u > stub.length - stub.dispatchOffset) return false;
        write32(temp + stub.dispatchOffset, dispatch);
    }
    return overlayOne(rangeOffset, data, bytes, targetOffset, temp, stub.length);
}

bool applySaveOps(const GbasaveExactRomPlan &romPlan, const GbasaveNorPatchPlan &patch,
                  uint32_t rangeOffset, uint8_t *data, uint32_t bytes)
{
    const SfwSavePlan &savePlan = romPlan.save_plan;
    const uint32_t dispatch = patch.payload_dispatch_offset == 0u ? 0u
        : kGbaRomBase + patch.payload_offset + patch.payload_dispatch_offset;

    for (uint32_t i = 0u; i < savePlan.op_count; ++i) {
        const SfwSaveOp &op = savePlan.op[i];
        if (op.offset >= romPlan.rom_size) return false;

        if (op.kind == SFW_OP_RAW_BYTES) {
            if (op.raw_len > SFW_MAX_RAW_BYTES || op.offset > romPlan.rom_size ||
                op.raw_len > romPlan.rom_size - op.offset) return false;
            if (!overlayOne(rangeOffset, data, bytes, op.offset, op.raw, op.raw_len)) return false;
            continue;
        }

        if (patch.route == GBASAVE_NOR_PATCH_RWW_EEPROM && op.kind == SFW_OP_EEPROM_WRITE) {
            /* Proven EEPROM write bridge: Thumb ldr/bx + literal to payload writer. */
            uint8_t thunk[8] = {0x00u,0x4Bu,0x18u,0x47u,0u,0u,0u,0u};
            const uint32_t target = kGbaRomBase + patch.payload_offset +
                static_cast<uint32_t>(gbasave::generated_m36_assets::kEepromEepromWriteAuto) + 1u;
            write32(thunk + 4u, target);
            if (op.offset > romPlan.rom_size || 8u > romPlan.rom_size - op.offset) return false;
            if (!overlayOne(rangeOffset, data, bytes, op.offset, thunk, sizeof(thunk))) return false;
            continue;
        }

        const StubDescriptor stub = stubForKind(op.kind, patch.save_type);
        if (stub.length != 0u) {
            if (op.offset > romPlan.rom_size || stub.length > romPlan.rom_size - op.offset) return false;
            if (!overlayStub(rangeOffset, data, bytes, op.offset, stub, dispatch)) return false;
        }
    }
    return true;
}

} // namespace

extern "C" int gbasave_nor_patch_plan_build(
    const GbasaveExactRomPlan *romPlan,
    const GbasaveNorSaveLayout *layout,
    uint16_t hotkeyRaw,
    uint8_t *payloadOut,
    uint32_t payloadCapacity,
    GbasaveNorPatchPlan *out)
{
    using namespace gbasave::generated_m36_assets;
    if (!romPlan || !layout || !payloadOut || !out || romPlan->rom_size == 0u) return GBASAVE_NOR_PATCH_INVALID;
    zeroBytes(out, sizeof(*out));
    for (uint32_t i=0u;i<GBASAVE_NOR_SAVE_LAYOUT_MAX_STORAGE;++i) out->storage[i]=UINT32_MAX;

    const uint32_t originalEntry = decodeResetTarget(romPlan->entry_word);
    if (originalEntry == 0u) return GBASAVE_NOR_PATCH_UNSUPPORTED;

    const uint8_t *asset = nullptr;
    uint32_t assetBytes = 0u;
    uint32_t entryOffset = 0u;
    uint32_t dispatchOffset = 0u;
    uint8_t route = GBASAVE_NOR_PATCH_NONE;

    if (layout->route == GBASAVE_NOR_SAVE_LAYOUT_RWW_SRAM && romPlan->save_type == SFW_SAVE_SRAM) {
        asset = kM36SramPayload; assetBytes = static_cast<uint32_t>(kM36SramPayloadSize);
        entryOffset = static_cast<uint32_t>(kSramEntryOffset); route = GBASAVE_NOR_PATCH_RWW_SRAM;
        if (layout->storage_count != 1u) return GBASAVE_NOR_PATCH_INVALID;
    } else if (layout->route == GBASAVE_NOR_SAVE_LAYOUT_RWW_EEPROM &&
               (romPlan->save_type == SFW_SAVE_EEPROM4K || romPlan->save_type == SFW_SAVE_EEPROM64K)) {
        asset = kM36EepromPayload; assetBytes = static_cast<uint32_t>(kM36EepromPayloadSize);
        entryOffset = static_cast<uint32_t>(kEepromEntryOffset); route = GBASAVE_NOR_PATCH_RWW_EEPROM;
        if (layout->storage_count != 2u || romPlan->save_plan.irq_count == 0u) return GBASAVE_NOR_PATCH_INVALID;
    } else if (layout->route == GBASAVE_NOR_SAVE_LAYOUT_RWW_FLASH_COMPACT &&
               (romPlan->save_type == SFW_SAVE_FLASH512K || romPlan->save_type == SFW_SAVE_FLASH1024K)) {
        asset = kM36CompactPayload; assetBytes = static_cast<uint32_t>(kM36CompactPayloadSize);
        dispatchOffset = static_cast<uint32_t>(kFlashCompactDispatchOffset); route = GBASAVE_NOR_PATCH_RWW_FLASH_COMPACT;
        if (layout->storage_count != 6u) return GBASAVE_NOR_PATCH_INVALID;
    } else return GBASAVE_NOR_PATCH_UNSUPPORTED;

    if (assetBytes > payloadCapacity || assetBytes > GBASAVE_NOR_PATCH_MAX_PAYLOAD_BYTES ||
        assetBytes > layout->payload_reserved_bytes) return GBASAVE_NOR_PATCH_INVALID;
    copyBytes(payloadOut, asset, assetBytes);

    out->route = route;
    out->save_type = romPlan->save_type;
    out->irq_count = romPlan->save_plan.irq_count;
    out->storage_count = layout->storage_count;
    out->source_rom_bytes = romPlan->rom_size;
    out->output_rom_bytes = layout->output_bytes;
    out->minimum_target_capacity_bytes = layout->minimum_capacity_bytes;
    out->payload_offset = layout->payload_offset;
    out->payload_bytes = assetBytes;
    out->payload_entry_offset = entryOffset;
    out->payload_dispatch_offset = dispatchOffset;
    out->original_entry_address = originalEntry;
    out->storage_block_bytes = layout->storage_block_bytes;
    out->hotkey_raw = hotkeyRaw;
    for (uint32_t i=0u;i<layout->storage_count;++i) out->storage[i]=layout->storage[i];

    if (route == GBASAVE_NOR_PATCH_RWW_SRAM) {
        write32(payloadOut + kSramConfigOriginalEntry, originalEntry);
        write32(payloadOut + kSramConfigSaveBlock, layout->storage[0]);
        write32(payloadOut + kSramConfigSaveSize, 0x00008000u);
        write32(payloadOut + kSramConfigRamBackup, 0xFFFFFFFFu);
        write32(payloadOut + kSramConfigHotkeyRaw, hotkeyRaw);
        if (!makeArmBranch(kGbaRomBase, kGbaRomBase + layout->payload_offset + entryOffset, &out->new_entry_word))
            return GBASAVE_NOR_PATCH_INVALID;
    } else if (route == GBASAVE_NOR_PATCH_RWW_EEPROM) {
        write32(payloadOut + kEepromConfigOriginalEntry, originalEntry);
        write32(payloadOut + kEepromConfigSaveBlock0, layout->storage[0]);
        write32(payloadOut + kEepromConfigSaveBlock1, layout->storage[1]);
        write32(payloadOut + kEepromConfigSaveSize, 0x00002000u);
        write32(payloadOut + kEepromConfigRamBackup, 0x00006000u);
        if (!makeArmBranch(kGbaRomBase, kGbaRomBase + layout->payload_offset + entryOffset, &out->new_entry_word))
            return GBASAVE_NOR_PATCH_INVALID;
    } else {
        for (uint32_t i=0u;i<6u;++i) write32(payloadOut + kFlashCompactConfigBlock0 + i*4u, layout->storage[i]);
        // FLASH512 exposes 16 logical 4 KiB sectors; FLASH1M exposes 32.
        // The compact runtime supports both through the same generated asset.
        const uint32_t logicalSectorCount =
            romPlan->save_type == SFW_SAVE_FLASH512K ? 16u : 32u;
        write32(payloadOut + kFlashCompactConfigSectorCount, logicalSectorCount);
        write32(payloadOut + kFlashCompactConfigLayoutMagic, static_cast<uint32_t>(kFlashCompactLayoutMagicValue));
    }
    return GBASAVE_NOR_PATCH_READY;
}

extern "C" int gbasave_nor_patch_apply_overlay(
    const GbasaveExactRomPlan *romPlan,
    const GbasaveNorPatchPlan *patchPlan,
    const uint8_t *payload,
    uint32_t rangeOffset,
    uint8_t *data,
    uint32_t bytes)
{
    if (!romPlan || !patchPlan || !payload || !data) return 0;
    if (bytes == 0u) return 1;
    if (rangeOffset > UINT32_MAX - bytes) return 0;

    if (!overlayOne(rangeOffset, data, bytes, patchPlan->payload_offset, payload, patchPlan->payload_bytes)) return 0;

    if (patchPlan->route == GBASAVE_NOR_PATCH_RWW_SRAM || patchPlan->route == GBASAVE_NOR_PATCH_RWW_EEPROM) {
        uint8_t branch[4]; write32(branch, patchPlan->new_entry_word);
        if (!overlayOne(rangeOffset, data, bytes, 0u, branch, sizeof(branch))) return 0;
        uint8_t irq[4]; write32(irq, kIrqChainLiteral);
        for (uint32_t i=0u;i<romPlan->save_plan.irq_count;++i) {
            const uint32_t off=romPlan->save_plan.irq_offset[i];
            if (off > romPlan->rom_size || 4u > romPlan->rom_size - off) return 0;
            if (!overlayOne(rangeOffset, data, bytes, off, irq, sizeof(irq))) return 0;
        }
    }

    if (patchPlan->route == GBASAVE_NOR_PATCH_RWW_EEPROM ||
        patchPlan->route == GBASAVE_NOR_PATCH_RWW_FLASH_COMPACT) {
        if (!applySaveOps(*romPlan, *patchPlan, rangeOffset, data, bytes)) return 0;
    }
    return 1;
}
