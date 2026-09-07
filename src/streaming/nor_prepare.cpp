#include "gbasave/streaming/nor_prepare.h"

#include <stddef.h>

namespace {

void zeroBytes(void *ptr, uint32_t bytes)
{
    uint8_t *p = static_cast<uint8_t *>(ptr);
    while (bytes-- != 0u) *p++ = 0u;
}

void copyBytes(void *dst, const void *src, uint32_t bytes)
{
    uint8_t *d = static_cast<uint8_t *>(dst);
    const uint8_t *s = static_cast<const uint8_t *>(src);
    while (bytes-- != 0u) *d++ = *s++;
}

uint32_t read32(const uint8_t *p)
{
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8u) |
           (static_cast<uint32_t>(p[2]) << 16u) |
           (static_cast<uint32_t>(p[3]) << 24u);
}


uint32_t crcByte(uint32_t crc, uint8_t value)
{
    crc ^= value;
    for (uint32_t bit = 0u; bit < 8u; ++bit)
        crc = (crc >> 1u) ^ (0xEDB88320u & (0u - (crc & 1u)));
    return crc;
}

void crcU8(uint32_t &crc, uint8_t v) { crc = crcByte(crc, v); }
void crcU16(uint32_t &crc, uint16_t v)
{
    crcU8(crc, static_cast<uint8_t>(v));
    crcU8(crc, static_cast<uint8_t>(v >> 8u));
}
void crcU32(uint32_t &crc, uint32_t v)
{
    crcU8(crc, static_cast<uint8_t>(v));
    crcU8(crc, static_cast<uint8_t>(v >> 8u));
    crcU8(crc, static_cast<uint8_t>(v >> 16u));
    crcU8(crc, static_cast<uint8_t>(v >> 24u));
}

void crcSavePlan(uint32_t &crc, const SfwSavePlan &p)
{
    crcU32(crc, p.filesize); crcU32(crc, p.save_guess);
    crcU16(crc, p.flash64cnt); crcU16(crc, p.flash128cnt);
    crcU8(crc, p.save_type); crcU8(crc, p.source_db);
    crcU8(crc, p.overflow); crcU8(crc, p.op_count); crcU8(crc, p.irq_count);
    const uint32_t irqCount = p.irq_count > SFW_MAX_IRQ_OPS ? SFW_MAX_IRQ_OPS : p.irq_count;
    for (uint32_t i = 0u; i < irqCount; ++i) crcU32(crc, p.irq_offset[i]);
    const uint32_t opCount = p.op_count > SFW_MAX_SAVE_OPS ? SFW_MAX_SAVE_OPS : p.op_count;
    for (uint32_t i = 0u; i < opCount; ++i) {
        const SfwSaveOp &op = p.op[i];
        crcU32(crc, op.offset); crcU8(crc, op.kind); crcU8(crc, op.raw_len);
        const uint32_t rawCount = op.raw_len > SFW_MAX_RAW_BYTES ? SFW_MAX_RAW_BYTES : op.raw_len;
        for (uint32_t n = 0u; n < rawCount; ++n) crcU8(crc, op.raw[n]);
    }
}

int prepareNormalized(const GbasaveExactRomPlan *romPlan,
                      const GbasaveNorSaveTarget *target,
                      uint16_t hotkeyRaw,
                      uint8_t *payloadOut,
                      uint32_t payloadCapacity,
                      GbasaveNorPreparedPatch *out)
{
    if (!romPlan || !target || !payloadOut || !out) return GBASAVE_NOR_PREPARE_INVALID;
    zeroBytes(out, sizeof(*out));
    copyBytes(&out->rom_plan, romPlan, sizeof(*romPlan));

    const int layoutResult = gbasave_nor_save_layout_resolve(&out->rom_plan, target, &out->layout);
    if (layoutResult == GBASAVE_NOR_SAVE_LAYOUT_INVALID) return GBASAVE_NOR_PREPARE_INVALID;
    if (layoutResult != GBASAVE_NOR_SAVE_LAYOUT_READY) return GBASAVE_NOR_PREPARE_UNSUPPORTED;

    const int patchResult = gbasave_nor_patch_plan_build(
        &out->rom_plan, &out->layout, hotkeyRaw,
        payloadOut, payloadCapacity, &out->patch_plan);
    if (patchResult == GBASAVE_NOR_PATCH_INVALID) return GBASAVE_NOR_PREPARE_INVALID;
    if (patchResult != GBASAVE_NOR_PATCH_READY) return GBASAVE_NOR_PREPARE_UNSUPPORTED;
    return GBASAVE_NOR_PREPARE_READY;
}

} // namespace

extern "C" int gbasave_nor_prepare_from_exact(
    const GbasaveExactRomPlan *exactPlan,
    const GbasaveNorSaveTarget *target,
    uint16_t hotkeyRaw,
    uint8_t *payloadOut,
    uint32_t payloadCapacity,
    GbasaveNorPreparedPatch *out)
{
    if (!exactPlan || !exactPlan->ready || exactPlan->reject_code != 0u ||
        !sfw_saveplan_usable(&exactPlan->save_plan)) return GBASAVE_NOR_PREPARE_UNSUPPORTED;
    return prepareNormalized(exactPlan, target, hotkeyRaw, payloadOut, payloadCapacity, out);
}

extern "C" int gbasave_nor_prepare_from_stream(
    const GbasaveSavePlanStream *stream,
    const uint8_t *header,
    uint32_t headerBytes,
    uint32_t fullCrc32,
    const GbasaveNorSaveTarget *target,
    uint16_t hotkeyRaw,
    uint8_t *payloadOut,
    uint32_t payloadCapacity,
    GbasaveNorPreparedPatch *out)
{
    if (!stream || !header || headerBytes < 0xC0u || !target || !payloadOut || !out)
        return GBASAVE_NOR_PREPARE_INVALID;
    if (!stream->finalized || stream->consumed_bytes == 0u ||
        !sfw_saveplan_usable(&stream->plan) ||
        !gbasave_save_plan_offsets_in_bounds(&stream->plan, stream->consumed_bytes))
        return GBASAVE_NOR_PREPARE_UNSUPPORTED;

    GbasaveExactRomPlan normalized;
    zeroBytes(&normalized, sizeof(normalized));
    normalized.rom_size = stream->consumed_bytes;
    normalized.game_code = read32(header + 0xACu);
    normalized.entry_word = read32(header);
    normalized.full_crc32 = fullCrc32;
    normalized.revision = header[0xBCu];
    normalized.header_checksum = header[0xBDu];
    normalized.fixed96 = header[0xB2u];
    normalized.save_type = stream->plan.save_type;
    normalized.source_kind = GBASAVE_ROM_PLAN_SOURCE_NONE;
    normalized.reject_code = 0u;
    normalized.ready = 1u;
    normalized.region_count = 0u; /* conservative: no inferred/trusted internal holes */
    copyBytes(&normalized.save_plan, &stream->plan, sizeof(stream->plan));
    normalized.save_plan.filesize = stream->consumed_bytes;
    normalized.save_plan.source_db = 0u;

    return prepareNormalized(&normalized, target, hotkeyRaw, payloadOut, payloadCapacity, out);
}

extern "C" int gbasave_nor_prepared_patch_apply_overlay(
    const GbasaveNorPreparedPatch *prepared,
    const uint8_t *payload,
    uint32_t rangeOffset,
    uint8_t *data,
    uint32_t bytes)
{
    if (!prepared) return 0;
    return gbasave_nor_patch_apply_overlay(
        &prepared->rom_plan, &prepared->patch_plan, payload,
        rangeOffset, data, bytes);
}


extern "C" uint32_t gbasave_nor_prepared_patch_crc32(
    const GbasaveNorPreparedPatch *prepared)
{
    if (!prepared) return 0u;
    uint32_t crc = 0xFFFFFFFFu;
    const GbasaveExactRomPlan &r = prepared->rom_plan;
    crcU32(crc, r.flags); crcU32(crc, r.rom_size); crcU32(crc, r.game_code);
    crcU32(crc, r.entry_word); crcU32(crc, r.full_crc32);
    crcU8(crc, r.revision); crcU8(crc, r.header_checksum); crcU8(crc, r.fixed96);
    crcU8(crc, r.save_type); crcU8(crc, r.source_kind); crcU8(crc, r.reject_code); crcU8(crc, r.ready);
    crcSavePlan(crc, r.save_plan);

    const GbasaveNorSaveLayout &l = prepared->layout;
    crcU8(crc, l.route); crcU8(crc, l.storage_count); crcU32(crc, l.flags);
    crcU32(crc, l.payload_offset); crcU32(crc, l.payload_reserved_bytes);
    crcU32(crc, l.storage_block_bytes);
    const uint32_t ls = l.storage_count > GBASAVE_NOR_SAVE_LAYOUT_MAX_STORAGE ? GBASAVE_NOR_SAVE_LAYOUT_MAX_STORAGE : l.storage_count;
    for (uint32_t i = 0u; i < ls; ++i) crcU32(crc, l.storage[i]);
    crcU32(crc, l.output_bytes); crcU32(crc, l.minimum_capacity_bytes);
    crcU32(crc, l.selected_ff_region_start); crcU32(crc, l.selected_ff_region_bytes);

    const GbasaveNorPatchPlan &p = prepared->patch_plan;
    crcU8(crc, p.route); crcU8(crc, p.save_type); crcU8(crc, p.irq_count); crcU8(crc, p.storage_count);
    crcU32(crc, p.source_rom_bytes); crcU32(crc, p.output_rom_bytes);
    crcU32(crc, p.minimum_target_capacity_bytes); crcU32(crc, p.payload_offset);
    crcU32(crc, p.payload_bytes); crcU32(crc, p.payload_entry_offset);
    crcU32(crc, p.payload_dispatch_offset); crcU32(crc, p.original_entry_address);
    crcU32(crc, p.new_entry_word); crcU32(crc, p.storage_block_bytes);
    const uint32_t ps = p.storage_count > GBASAVE_NOR_SAVE_LAYOUT_MAX_STORAGE ? GBASAVE_NOR_SAVE_LAYOUT_MAX_STORAGE : p.storage_count;
    for (uint32_t i = 0u; i < ps; ++i) crcU32(crc, p.storage[i]);
    crcU16(crc, p.hotkey_raw);
    return ~crc;
}
