#include "gbasave/streaming/analyzer.h"

static void copy_bytes(uint8_t *dst, const uint8_t *src, uint32_t bytes)
{
    while (bytes-- != 0u)
        *dst++ = *src++;
}

static uint32_t read32le(const uint8_t *p)
{
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8u) |
           ((uint32_t)p[2] << 16u) |
           ((uint32_t)p[3] << 24u);
}

static void push_irq_literal(SfwSavePlan *plan, uint32_t offset)
{
    uint32_t i;
    if (!plan)
        return;
    for (i = 0u; i < plan->irq_count; ++i)
        if (plan->irq_offset[i] == offset)
            return;
    if (plan->irq_count >= SFW_MAX_IRQ_OPS) {
        plan->overflow = 1u;
        return;
    }
    plan->irq_offset[plan->irq_count++] = offset;
}

void gbasave_save_plan_stream_begin(
    GbasaveSavePlanStream *stream,
    uint32_t rom_bytes,
    uint8_t family_mask)
{
    if (!stream)
        return;
    sfw_saveplan_init(&stream->plan, rom_bytes);
    stream->tail_bytes = 0u;
    stream->consumed_bytes = 0u;
    stream->family_mask = (uint8_t)(family_mask & SFW_SCAN_ALL);
    if (stream->family_mask == 0u)
        stream->family_mask = SFW_SCAN_ALL;
    stream->finalized = 0u;
    stream->_reserved[0] = stream->_reserved[1] = 0u;
}

void gbasave_save_plan_stream_set_family_mask(
    GbasaveSavePlanStream *stream,
    uint8_t family_mask)
{
    if (!stream || stream->finalized)
        return;
    stream->family_mask = (uint8_t)(family_mask & SFW_SCAN_ALL);
    if (stream->family_mask == 0u)
        stream->family_mask = SFW_SCAN_ALL;
}

int gbasave_save_plan_stream_feed(
    GbasaveSavePlanStream *stream,
    uint32_t chunk_offset,
    const uint8_t *chunk,
    uint32_t chunk_bytes)
{
    uint8_t boundary[512];
    uint32_t prefix;
    uint32_t new_tail;

    if (!stream || stream->finalized || (!chunk && chunk_bytes != 0u))
        return 0;
    if (chunk_offset != stream->consumed_bytes)
        return 0;
    /* SuperFW signatures are scanned on 32-bit ROM offsets.  Embedded
     * consumers normally feed sector/block sized chunks; require the same
     * alignment for every non-final feed so transport chunking cannot shift
     * the signature grid silently. */
    if ((chunk_offset & 3u) != 0u)
        return 0;
    if (chunk_bytes > stream->plan.filesize - stream->consumed_bytes)
        return 0;
    if (chunk_offset + chunk_bytes < stream->plan.filesize && (chunk_bytes & 3u) != 0u)
        return 0;
    if (chunk_bytes == 0u)
        return 1;

    sfw_saveplan_scan_filtered(
        &stream->plan,
        chunk,
        chunk_bytes,
        chunk_offset,
        chunk_offset,
        stream->family_mask);

    /* Generic IRQ-chain literal ownership belongs to libgbasave, not to any
     * consumer. Embedded consumers historically re-scanned ROM chunks for
     * aligned 0x03007FFC words before preparing SRAM/EEPROM NOR routes. Keep
     * that discovery here so exact and streaming users share one bounded,
     * deduplicated source of truth. Chunks are required to begin on a 4-byte
     * boundary and every non-final chunk is 4-byte-sized, so an aligned word
     * cannot straddle two accepted feeds. */
    {
        uint32_t i;
        for (i = 0u; i + 3u < chunk_bytes; i += 4u) {
            if (read32le(chunk + i) == 0x03007FFCu)
                push_irq_literal(&stream->plan, chunk_offset + i);
        }
    }

    if (stream->tail_bytes != 0u) {
        prefix = chunk_bytes < 256u ? chunk_bytes : 256u;
        copy_bytes(boundary, stream->tail, stream->tail_bytes);
        copy_bytes(boundary + stream->tail_bytes, chunk, prefix);
        sfw_saveplan_scan_filtered(
            &stream->plan,
            boundary,
            stream->tail_bytes + prefix,
            chunk_offset - stream->tail_bytes,
            chunk_offset - stream->tail_bytes,
            stream->family_mask);
    }

    new_tail = chunk_bytes < 256u ? chunk_bytes : 256u;
    copy_bytes(stream->tail, chunk + chunk_bytes - new_tail, new_tail);
    stream->tail_bytes = new_tail;
    stream->consumed_bytes += chunk_bytes;
    return 1;
}

int gbasave_save_plan_stream_finish(GbasaveSavePlanStream *stream)
{
    if (!stream || stream->finalized)
        return 0;
    if (stream->consumed_bytes != stream->plan.filesize)
        return 0;
    sfw_saveplan_finalize(&stream->plan);
    stream->finalized = 1u;
    return sfw_saveplan_usable(&stream->plan);
}

const SfwSavePlan *gbasave_save_plan_stream_plan(const GbasaveSavePlanStream *stream)
{
    return stream ? &stream->plan : 0;
}

int gbasave_save_plan_offsets_in_bounds(const SfwSavePlan *plan, uint32_t rom_bytes)
{
    uint32_t i;
    if (!plan || plan->overflow)
        return 0;
    for (i = 0u; i < plan->op_count; ++i) {
        uint32_t need = plan->op[i].kind == SFW_OP_RAW_BYTES ? plan->op[i].raw_len : 128u;
        uint32_t off = plan->op[i].offset;
        if (need == 0u)
            need = 4u;
        if (off >= rom_bytes || need > rom_bytes - off)
            return 0;
    }
    for (i = 0u; i < plan->irq_count; ++i) {
        uint32_t off = plan->irq_offset[i];
        if (off >= rom_bytes || 4u > rom_bytes - off)
            return 0;
    }
    return 1;
}

int gbasave_validate_eeprom_db_plan(
    const GbasaveRandomAccessRom *rom,
    const SfwSavePlan *plan)
{
    uint32_t i;
    uint32_t j;
    uint8_t window[128];
    uint8_t irq[4];
    SfwSavePlan probe;

    if (!rom || !rom->read_at || !plan)
        return 0;
    if (plan->save_type != SFW_SAVE_EEPROM4K && plan->save_type != SFW_SAVE_EEPROM64K)
        return 0;
    if (!gbasave_save_plan_offsets_in_bounds(plan, rom->size_bytes))
        return 0;

    for (i = 0u; i < plan->op_count; ++i) {
        const SfwSaveOp *op = &plan->op[i];
        int found = 0;
        if (op->kind != SFW_OP_EEPROM_READ && op->kind != SFW_OP_EEPROM_WRITE)
            continue;
        if (op->offset > rom->size_bytes || sizeof(window) > rom->size_bytes - op->offset)
            return 0;
        if (!rom->read_at(rom->context, op->offset, window, (uint32_t)sizeof(window)))
            return 0;
        sfw_saveplan_init(&probe, rom->size_bytes);
        sfw_saveplan_scan_filtered(
            &probe,
            window,
            (uint32_t)sizeof(window),
            op->offset,
            op->offset,
            SFW_SCAN_EEPROM);
        for (j = 0u; j < probe.op_count; ++j) {
            if (probe.op[j].kind == op->kind && probe.op[j].offset == op->offset) {
                found = 1;
                break;
            }
        }
        if (!found)
            return 0;
    }

    for (i = 0u; i < plan->irq_count; ++i) {
        if (!rom->read_at(rom->context, plan->irq_offset[i], irq, 4u))
            return 0;
        if (read32le(irq) != 0x03007FFCu)
            return 0;
    }

    return sfw_saveplan_has(plan, SFW_OP_EEPROM_READ) &&
           sfw_saveplan_has(plan, SFW_OP_EEPROM_WRITE) &&
           plan->irq_count != 0u;
}
