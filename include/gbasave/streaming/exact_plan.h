#pragma once

#include "gbasave/streaming/analyzer.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GBASAVE_EXACT_ROM_PLAN_MAX_REGIONS 255u

/* Stable analyzer-region/source bits carried by the compiled exact-plan DB.
 * Consumers should not duplicate private GBABR constants for these facts. */
#define GBASAVE_ROM_PLAN_SOURCE_NONE     0u
#define GBASAVE_ROM_PLAN_SOURCE_SUPERFW  1u
#define GBASAVE_ROM_PLAN_SOURCE_STATIC   2u

#define GBASAVE_ROM_REGION_INTERNAL          0x01u
#define GBASAVE_ROM_REGION_TAIL              0x02u
#define GBASAVE_ROM_REGION_SAFE_CODE         0x04u
#define GBASAVE_ROM_REGION_POINTER_FREE      0x08u
#define GBASAVE_ROM_REGION_SUPERFW           0x10u
#define GBASAVE_ROM_REGION_FF                0x20u
#define GBASAVE_ROM_REGION_ZERO              0x40u
#define GBASAVE_ROM_REGION_PROTECTED_NEARBY  0x80u

typedef struct GbasaveExactRomRegion {
    uint32_t start;
    uint32_t size;
    uint8_t fill;
    uint8_t flags;
    uint8_t alignment_log2;
    uint8_t source;
} GbasaveExactRomRegion;

typedef struct GbasaveExactRomPlan {
    uint32_t flags;
    uint32_t rom_size;
    uint32_t game_code;
    uint32_t entry_word;
    uint32_t sample_offset[3];
    uint32_t sample_crc32[3];
    uint32_t full_crc32;
    uint32_t tail_start;
    uint32_t tail_bytes;
    uint8_t revision;
    uint8_t header_checksum;
    uint8_t fixed96;
    uint8_t save_type;
    uint8_t source_kind;
    uint8_t reject_code;
    uint8_t ready;
    uint8_t region_count;
    SfwSavePlan save_plan;
    GbasaveExactRomRegion region[GBASAVE_EXACT_ROM_PLAN_MAX_REGIONS];
} GbasaveExactRomPlan;

typedef enum GbasaveExactRomPlanResult {
    GBASAVE_EXACT_ROM_PLAN_IO_ERROR = -2,
    GBASAVE_EXACT_ROM_PLAN_INVALID = -1,
    GBASAVE_EXACT_ROM_PLAN_NOT_FOUND = 0,
    GBASAVE_EXACT_ROM_PLAN_READY = 1,
    GBASAVE_EXACT_ROM_PLAN_BLOCKED = 2
} GbasaveExactRomPlanResult;

/*
 * Look up an analyzer-generated exact ROM plan without loading the ROM into RAM.
 *
 * The compiled database belongs to libgbasave. The consumer supplies only a
 * random-access ROM callback, the already-read GBA header, and scratch memory.
 * Matching uses immutable header fields plus the same three bounded sample CRCs
 * recorded by gba_static_analyzer. No cartridge mutation is performed.
 */
int gbasave_exact_rom_plan_lookup(
    const GbasaveRandomAccessRom *rom,
    const uint8_t *header,
    uint32_t header_bytes,
    uint8_t *scratch,
    uint32_t scratch_bytes,
    GbasaveExactRomPlan *out);

/* Full-source CRC gate used immediately before destructive consumer actions. */
int gbasave_exact_rom_plan_verify_full_crc(
    const GbasaveRandomAccessRom *rom,
    uint8_t *scratch,
    uint32_t scratch_bytes,
    uint32_t expected_crc32,
    uint32_t *actual_crc32);

/* Metadata/introspection for consumers that need to resolve an already-patched
 * installed ROM by immutable GBA header identity. The returned candidates are
 * still analyzer-owned libgbasave records; the consumer must validate its
 * installed payload/layout before accepting one. */
uint32_t gbasave_exact_rom_plan_database_version(void);
uint32_t gbasave_exact_rom_plan_database_count(void);
uint32_t gbasave_exact_rom_plan_identity_candidate_count(
    uint32_t game_code,
    uint8_t revision,
    uint8_t header_checksum,
    uint8_t fixed96);
int gbasave_exact_rom_plan_identity_candidate_get(
    uint32_t game_code,
    uint8_t revision,
    uint8_t header_checksum,
    uint8_t fixed96,
    uint32_t candidate_index,
    GbasaveExactRomPlan *out);

#ifdef __cplusplus
}
#endif
