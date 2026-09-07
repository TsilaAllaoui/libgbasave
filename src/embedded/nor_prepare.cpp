#include "gbasave/embedded/nor_prepare.h"

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
