#pragma once

#include "gbasave/embedded/nor_patch.h"
#include "gbasave/embedded/save_plan_stream.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Complete libgbasave-owned preparation result for a direct-RWW NOR target.
 *
 * Consumers provide only immutable ROM facts / a completed libgbasave stream
 * plus target geometry. libgbasave owns normalization, route selection,
 * payload/storage placement and ROM patch composition policy.
 *
 * A consumer may copy addresses from this structure into its own operation
 * state, but must not synthesize or alter them.
 */
typedef struct GbasaveNorPreparedPatch {
    GbasaveExactRomPlan rom_plan;
    GbasaveNorSaveLayout layout;
    GbasaveNorPatchPlan patch_plan;
} GbasaveNorPreparedPatch;

typedef enum GbasaveNorPrepareResult {
    GBASAVE_NOR_PREPARE_INVALID = -1,
    GBASAVE_NOR_PREPARE_UNSUPPORTED = 0,
    GBASAVE_NOR_PREPARE_READY = 1
} GbasaveNorPrepareResult;

/* Prepare directly from an analyzer-generated exact libgbasave record. */
int gbasave_nor_prepare_from_exact(
    const GbasaveExactRomPlan *exact_plan,
    const GbasaveNorSaveTarget *target,
    uint16_t hotkey_raw,
    uint8_t *payload_out,
    uint32_t payload_capacity,
    GbasaveNorPreparedPatch *out);

/*
 * Prepare from a completed libgbasave streaming analysis.
 *
 * No internal ROM holes are inferred or trusted here. libgbasave itself
 * creates the conservative normalized ROM plan and therefore chooses only a
 * layout that is valid without exact analyzer region metadata.
 *
 * header must contain the standard 0xC0-byte GBA header. full_crc32 is the
 * CRC of exactly the bytes consumed by stream.
 */
int gbasave_nor_prepare_from_stream(
    const GbasaveSavePlanStream *stream,
    const uint8_t *header,
    uint32_t header_bytes,
    uint32_t full_crc32,
    const GbasaveNorSaveTarget *target,
    uint16_t hotkey_raw,
    uint8_t *payload_out,
    uint32_t payload_capacity,
    GbasaveNorPreparedPatch *out);

/* Apply the exact libgbasave-owned prepared plan to one streamed ROM range. */
int gbasave_nor_prepared_patch_apply_overlay(
    const GbasaveNorPreparedPatch *prepared,
    const uint8_t *payload,
    uint32_t range_offset,
    uint8_t *data,
    uint32_t bytes);

#ifdef __cplusplus
}
#endif
