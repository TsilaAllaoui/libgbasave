#include "gbasave/embedded/exact_rom_plan.h"
#include "gbabr_plan_db.h"

namespace {
using namespace gbasave::generated_gbabr_plan;

static void zero_bytes(void *ptr, uint32_t bytes)
{
    uint8_t *p = static_cast<uint8_t *>(ptr);
    while (bytes-- != 0u)
        *p++ = 0u;
}

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

static uint32_t crc32_update(uint32_t crc, const uint8_t *data, uint32_t bytes)
{
    while (bytes-- != 0u) {
        uint32_t bit;
        crc ^= *data++;
        for (bit = 0u; bit < 8u; ++bit)
            crc = (crc >> 1u) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return crc;
}

static int sample_crc(
    const GbasaveRandomAccessRom *rom,
    uint32_t offset,
    uint32_t bytes,
    uint8_t *scratch,
    uint32_t scratch_bytes,
    uint32_t *out)
{
    uint32_t crc = 0xFFFFFFFFu;
    uint32_t done = 0u;
    if (!rom || !rom->read_at || !scratch || scratch_bytes == 0u || !out)
        return 0;
    if (offset > rom->size_bytes || bytes > rom->size_bytes - offset)
        return 0;
    while (done < bytes) {
        uint32_t take = bytes - done;
        if (take > scratch_bytes)
            take = scratch_bytes;
        if (!rom->read_at(rom->context, offset + done, scratch, take))
            return 0;
        crc = crc32_update(crc, scratch, take);
        done += take;
    }
    *out = ~crc;
    return 1;
}

static int header_matches(const Entry &entry, const uint8_t *header, uint32_t rom_size)
{
    if (rom_size != entry.romSize || entry.romSize < 0xC0u)
        return 0;
    if (read32le(header + 0xACu) != entry.gameCode)
        return 0;
    if (header[0xBCu] != entry.revision || header[0xBDu] != entry.headerChecksum ||
        header[0xB2u] != entry.fixed96)
        return 0;
    return read32le(header) == entry.entryWord;
}

static int copy_entry(const Entry &entry, GbasaveExactRomPlan *out)
{
    uint32_t i;
    if (!out || entry.opCount > SFW_MAX_SAVE_OPS || entry.irqCount > SFW_MAX_IRQ_OPS ||
        entry.regionCount > GBASAVE_EXACT_ROM_PLAN_MAX_REGIONS)
        return 0;
    zero_bytes(out, (uint32_t)sizeof(*out));
    out->flags = entry.flags;
    out->rom_size = entry.romSize;
    out->game_code = entry.gameCode;
    out->entry_word = entry.entryWord;
    out->sample_offset[0] = entry.sample0Off;
    out->sample_offset[1] = entry.sample1Off;
    out->sample_offset[2] = entry.sample2Off;
    out->sample_crc32[0] = entry.sample0Crc;
    out->sample_crc32[1] = entry.sample1Crc;
    out->sample_crc32[2] = entry.sample2Crc;
    out->full_crc32 = entry.crc32;
    out->tail_start = entry.tailStart;
    out->tail_bytes = entry.tailBytes;
    out->revision = entry.revision;
    out->header_checksum = entry.headerChecksum;
    out->fixed96 = entry.fixed96;
    out->save_type = entry.saveType;
    out->source_kind = entry.sourceKind;
    out->reject_code = entry.rejectCode;
    out->ready = entry.ready;
    out->region_count = entry.regionCount;

    sfw_saveplan_init(&out->save_plan, entry.romSize);
    out->save_plan.save_type = entry.saveType;
    out->save_plan.source_db = 1u;
    out->save_plan.op_count = (uint8_t)entry.opCount;
    out->save_plan.irq_count = (uint8_t)entry.irqCount;
    for (i = 0u; i < entry.opCount; ++i) {
        const Op &src = kOps[entry.firstOp + i];
        SfwSaveOp *dst = &out->save_plan.op[i];
        dst->offset = src.offset;
        dst->kind = src.kind;
        dst->raw_len = (uint8_t)src.rawSize;
        if (src.rawSize > SFW_MAX_RAW_BYTES || src.rawOffset > sizeof(kRawBytes) ||
            src.rawSize > sizeof(kRawBytes) - src.rawOffset)
            return 0;
        if (src.rawSize != 0u)
            copy_bytes(dst->raw, kRawBytes + src.rawOffset, src.rawSize);
    }
    for (i = 0u; i < entry.irqCount; ++i)
        out->save_plan.irq_offset[i] = kIrqs[entry.firstIrq + i];
    for (i = 0u; i < entry.regionCount; ++i) {
        const Region &src = kRegions[entry.firstRegion + i];
        GbasaveExactRomRegion *dst = &out->region[i];
        dst->start = src.start;
        dst->size = src.size;
        dst->fill = src.fill;
        dst->flags = src.flags;
        dst->alignment_log2 = src.alignmentLog2;
        dst->source = src.source;
    }
    return 1;
}

} // namespace

extern "C" {

int gbasave_exact_rom_plan_lookup(
    const GbasaveRandomAccessRom *rom,
    const uint8_t *header,
    uint32_t header_bytes,
    uint8_t *scratch,
    uint32_t scratch_bytes,
    GbasaveExactRomPlan *out)
{
    uint32_t i;
    uint32_t sample_bytes;
    if (!rom || !rom->read_at || !header || header_bytes < 0xC0u || !scratch ||
        scratch_bytes == 0u || !out || rom->size_bytes < 0xC0u)
        return GBASAVE_EXACT_ROM_PLAN_INVALID;
    sample_bytes = rom->size_bytes < 4096u ? rom->size_bytes : 4096u;

    for (i = 0u; i < (uint32_t)(sizeof(kEntries) / sizeof(kEntries[0])); ++i) {
        const Entry &entry = kEntries[i];
        uint32_t crc;
        uint32_t sample;
        if (!header_matches(entry, header, rom->size_bytes))
            continue;
        for (sample = 0u; sample < 3u; ++sample) {
            const uint32_t expected = sample == 0u ? entry.sample0Crc :
                                      sample == 1u ? entry.sample1Crc : entry.sample2Crc;
            const uint32_t offset = sample == 0u ? entry.sample0Off :
                                    sample == 1u ? entry.sample1Off : entry.sample2Off;
            if (!sample_crc(rom, offset, sample_bytes, scratch, scratch_bytes, &crc))
                return GBASAVE_EXACT_ROM_PLAN_IO_ERROR;
            if (crc != expected)
                break;
        }
        if (sample != 3u)
            continue;
        if (!copy_entry(entry, out))
            return GBASAVE_EXACT_ROM_PLAN_INVALID;
        return entry.ready ? GBASAVE_EXACT_ROM_PLAN_READY : GBASAVE_EXACT_ROM_PLAN_BLOCKED;
    }
    return GBASAVE_EXACT_ROM_PLAN_NOT_FOUND;
}

uint32_t gbasave_exact_rom_plan_database_version(void)
{
    return kFormatVersion;
}

uint32_t gbasave_exact_rom_plan_database_count(void)
{
    return (uint32_t)(sizeof(kEntries) / sizeof(kEntries[0]));
}

uint32_t gbasave_exact_rom_plan_identity_candidate_count(
    uint32_t game_code,
    uint8_t revision,
    uint8_t header_checksum,
    uint8_t fixed96)
{
    uint32_t count = 0u;
    uint32_t i;
    for (i = 0u; i < (uint32_t)(sizeof(kEntries) / sizeof(kEntries[0])); ++i) {
        const Entry &entry = kEntries[i];
        if (entry.gameCode == game_code && entry.revision == revision &&
            entry.headerChecksum == header_checksum && entry.fixed96 == fixed96)
            ++count;
    }
    return count;
}

int gbasave_exact_rom_plan_identity_candidate_get(
    uint32_t game_code,
    uint8_t revision,
    uint8_t header_checksum,
    uint8_t fixed96,
    uint32_t candidate_index,
    GbasaveExactRomPlan *out)
{
    uint32_t seen = 0u;
    uint32_t i;
    if (!out)
        return 0;
    for (i = 0u; i < (uint32_t)(sizeof(kEntries) / sizeof(kEntries[0])); ++i) {
        const Entry &entry = kEntries[i];
        if (entry.gameCode != game_code || entry.revision != revision ||
            entry.headerChecksum != header_checksum || entry.fixed96 != fixed96)
            continue;
        if (seen++ != candidate_index)
            continue;
        return copy_entry(entry, out);
    }
    return 0;
}

int gbasave_exact_rom_plan_verify_full_crc(
    const GbasaveRandomAccessRom *rom,
    uint8_t *scratch,
    uint32_t scratch_bytes,
    uint32_t expected_crc32,
    uint32_t *actual_crc32)
{
    uint32_t actual;
    if (!rom || !rom->read_at || !scratch || scratch_bytes == 0u)
        return 0;
    if (!sample_crc(rom, 0u, rom->size_bytes, scratch, scratch_bytes, &actual))
        return 0;
    if (actual_crc32)
        *actual_crc32 = actual;
    return actual == expected_crc32;
}

} // extern "C"
