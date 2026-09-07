#pragma once

#include "gbasave/streaming/save_plan.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Allocation-free streaming save-plan analyzer.
 *
 * Consumers own filesystem/transport I/O and feed source ROM bytes in order.
 * libgbasave owns chunk-boundary handling, SuperFW signature-plan assembly,
 * and generic aligned 0x03007FFC IRQ-chain literal discovery. This keeps
 * NDS/ESP32 consumers from reimplementing save/IRQ scan state machines.
 */

typedef struct GbasaveSavePlanStream {
    SfwSavePlan plan;
    uint8_t tail[256];
    uint32_t tail_bytes;
    uint32_t consumed_bytes;
    uint8_t family_mask;
    uint8_t finalized;
    uint8_t _reserved[2];
} GbasaveSavePlanStream;

void gbasave_save_plan_stream_begin(
    GbasaveSavePlanStream *stream,
    uint32_t rom_bytes,
    uint8_t family_mask);

/* Change the handler family accepted by subsequent feeds without resetting
 * already discovered operations. Useful when an embedded consumer narrows its
 * scan after finding a save-library marker. */
void gbasave_save_plan_stream_set_family_mask(
    GbasaveSavePlanStream *stream,
    uint8_t family_mask);

/* Returns 1 on success, 0 for invalid/non-contiguous input. */
int gbasave_save_plan_stream_feed(
    GbasaveSavePlanStream *stream,
    uint32_t chunk_offset,
    const uint8_t *chunk,
    uint32_t chunk_bytes);

/* Returns sfw_saveplan_usable(plan): 1 usable, 0 incomplete/unsupported. */
int gbasave_save_plan_stream_finish(GbasaveSavePlanStream *stream);

const SfwSavePlan *gbasave_save_plan_stream_plan(const GbasaveSavePlanStream *stream);

/* Generic DB-plan structural validation shared by all consumers. */
int gbasave_save_plan_offsets_in_bounds(const SfwSavePlan *plan, uint32_t rom_bytes);

typedef int (*GbasaveReadAtFn)(void *context, uint32_t offset, uint8_t *dst, uint32_t bytes);

typedef struct GbasaveRandomAccessRom {
    void *context;
    uint32_t size_bytes;
    GbasaveReadAtFn read_at;
} GbasaveRandomAccessRom;

/*
 * Strict EEPROM DB-fast-path validation.  It re-scans each referenced handler
 * from the exact source bytes and verifies every IRQ-vector literal is still
 * 0x03007FFC.  Modified/hacked ROMs therefore fall back safely to streaming
 * signature analysis instead of trusting a stale DB entry.
 */
int gbasave_validate_eeprom_db_plan(
    const GbasaveRandomAccessRom *rom,
    const SfwSavePlan *plan);

#ifdef __cplusplus
}
#endif
