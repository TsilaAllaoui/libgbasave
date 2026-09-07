#ifndef GBABF_SUPERFW_SAVEPATCH_PORT_H
#define GBABF_SUPERFW_SAVEPATCH_PORT_H

#include <stdint.h>
#include <stddef.h>

/* Save patch discovery ported from SuperFW 0.20 (GPL-3.0-or-later),
 * Copyright (C) David Guillen Fandos.
 * GBABF adaptation: discovery only; runtime/storage backend is GBABF-specific. */

typedef enum {
    SFW_SAVE_NONE = 0,
    SFW_SAVE_SRAM = 1,
    SFW_SAVE_EEPROM4K = 2,
    SFW_SAVE_EEPROM64K = 3,
    SFW_SAVE_FLASH512K = 4,
    SFW_SAVE_FLASH1024K = 5
} SfwSaveType;

typedef enum {
    SFW_OP_EEPROM_READ = 1,
    SFW_OP_EEPROM_WRITE,
    SFW_OP_FLASH_READ,
    SFW_OP_FLASH_ERASE_CHIP,
    SFW_OP_FLASH_ERASE_SECTOR,
    SFW_OP_FLASH_WRITE_SECTOR,
    SFW_OP_FLASH_WRITE_BYTE,
    SFW_OP_FLASH_IDENT,
    SFW_OP_FLASH_VERIFY,
    SFW_OP_RAW_THUMB_RET0,
    SFW_OP_RAW_BYTES,
    /* Appended to preserve the numeric ABI of all existing SuperFW-derived
     * op kinds. Nintendo FLASH1M uses an explicit two-bank selector that must
     * be virtualized by direct-NOR backends. */
    SFW_OP_FLASH_SWITCH_BANK
} SfwSaveOpKind;

#define SFW_MAX_SAVE_OPS 96u
#define SFW_MAX_RAW_BYTES 60u
#define SFW_MAX_IRQ_OPS 64u
#define SFW_MAX_COMPAT_PATCHES 32u
#define SFW_MAX_COMPAT_BYTES 60u

typedef struct {
    uint32_t offset;
    uint8_t kind;
    uint8_t raw_len;
    uint8_t raw[SFW_MAX_RAW_BYTES];
} SfwSaveOp;

typedef struct {
    uint32_t offset;
    uint8_t len;
    uint8_t bytes[SFW_MAX_COMPAT_BYTES];
} SfwCompatOp;

typedef struct {
    uint32_t filesize;
    uint32_t virtual_end;
    uint8_t source_db;
    uint8_t overflow;
    uint8_t op_count;
    uint8_t _pad0;
    SfwCompatOp op[SFW_MAX_COMPAT_PATCHES];
} SfwCompatPlan;

typedef struct {
    uint32_t filesize;
    uint32_t save_guess;
    uint16_t flash64cnt;
    uint16_t flash128cnt;
    uint8_t save_type;
    uint8_t source_db;
    uint8_t overflow;
    uint8_t op_count;
    uint8_t irq_count;
    uint8_t _pad0;
    uint32_t irq_offset[SFW_MAX_IRQ_OPS];
    SfwSaveOp op[SFW_MAX_SAVE_OPS];
} SfwSavePlan;

void sfw_saveplan_init(SfwSavePlan *p, uint32_t filesize);
#define SFW_SCAN_EEPROM 1u
#define SFW_SCAN_FLASH  2u
#define SFW_SCAN_ALL    (SFW_SCAN_EEPROM|SFW_SCAN_FLASH)
void sfw_saveplan_scan(SfwSavePlan *p, const uint8_t *data, uint32_t len, uint32_t base_off, uint32_t accept_start);
void sfw_saveplan_scan_filtered(SfwSavePlan *p, const uint8_t *data, uint32_t len, uint32_t base_off, uint32_t accept_start, uint8_t family_mask);
void sfw_saveplan_finalize(SfwSavePlan *p);
int sfw_saveplan_db_lookup(SfwSavePlan *p, const uint8_t *db, uint32_t db_size, const uint8_t gamecode5[5]);
int sfw_waitcnt_db_lookup(SfwCompatPlan *p, const uint8_t *db, uint32_t db_size, const uint8_t gamecode5[5]);
uint32_t sfw_save_type_size(uint8_t save_type);
int sfw_saveplan_has(const SfwSavePlan *p, uint8_t kind);
int sfw_saveplan_usable(const SfwSavePlan *p);
const char *sfw_save_type_name(uint8_t save_type);

#endif
