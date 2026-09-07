.syntax unified
.cpu arm7tdmi
.arm
.section .text,"ax",%progbits
.global payload_start
.global payload_entry
.global config_original_entry
.global config_save_block_off
.global config_save_block1_off
.global config_save_size
.global config_ram_backup_sram_off
.global flash_worker_start
.global flash_worker_end
.global eeprom_write_auto

/* Position-independent M36 EEPROM/SRAM -> NOR payload for GBABF FIX78.
 * Config words are patched by the NDS flasher before the virtual image is read.
 */
payload_start:
    .ascii "GBF168M36EEP2D1"
    .byte 0
    .align 2
config_original_entry:
    .word 0x080000C0
config_save_block_off:
    .word 0x00FC0000
config_save_block1_off:
    .word 0x00FE0000
config_save_size:
    .word 0x00008000
config_ram_backup_sram_off:
    /* EEPROM64K only: unused physical SRAM area used to preserve the RAM
     * occupied by the temporary NOR worker.  The NDS flasher patches this to
     * 0x6000 for EEPROM modes and leaves 0xFFFFFFFF for full SRAM saves. */
    .word 0xFFFFFFFF

/* Small PIC config loaders.  Keeping ADR close to the config table avoids ARM
 * immediate-encoding sensitivity when later payload features grow. */
load_cfg_save_block_r0:
    adr r0, config_save_block_off
    ldr r0, [r0]
    bx lr
load_cfg_save_block1_r3:
    adr r3, config_save_block1_off
    ldr r3, [r3]
    bx lr
load_cfg_save_size_r2:
    adr r2, config_save_size
    ldr r2, [r2]
    bx lr
load_cfg_backup_r5:
    adr r5, config_ram_backup_sram_off
    ldr r5, [r5]
    bx lr

/* ARM entrypoint installed into GBA header. */
payload_entry:
    /* FIX78 EEPROM journal restore.
     * EEPROM64K stores 8 KiB logical snapshots in 15 append-only slots inside
     * the same 128 KiB M36 erase block.  Each slot is 0x2100 bytes:
     *   +0x00 32-byte validity header (written LAST)
     *   +0x20 32-byte reservation marker (written FIRST)
     *   +0x40 8 KiB EEPROM/SRAM image
     * Power loss before the validity header therefore leaves the previous slot
     * authoritative.  On boot validate every committed slot and restore the
     * newest sequence.  Non-EEPROM SRAM mode keeps the legacy GBS2 restore.
     */
    adr r6, config_save_size
    ldr r6, [r6]
    ldr r7, =0x00002000
    cmp r6, r7
    bne legacy_restore

    adr r8, config_save_block_off
    ldr r0, [r8]
    ldr r9, =0x08000000
    add r9, r9, r0              /* r9 = block0 absolute base */
    mov r10, #0                 /* best sequence */
    mov r11, #0                 /* best slot pointer */
    mov r12, #0                 /* global slot index 0..29 */

journal_restore_scan:
    cmp r12, #30
    beq journal_restore_done
    cmp r12, #15
    movlo r0, r12
    movlo r1, r9
    subhs r0, r12, #15
    ldrhs r1, [r8, #4]          /* block1 ROM offset */
    ldrhs r2, =0x08000000
    addhs r1, r1, r2
    ldr r2, =0x00002100
    mul r0, r2, r0
    add r1, r1, r0              /* slot base */
    ldr r0, [r1, #0]
    ldr r2, =0x334A4247         /* 'GBJ3' */
    cmp r0, r2
    bne journal_restore_next
    ldr r0, [r1, #4]
    cmp r0, #1
    bne journal_restore_next
    ldr r0, [r1, #8]
    ldr r2, =0x00002000
    cmp r0, r2
    bne journal_restore_next
    ldr r2, [r1, #16]           /* sequence */
    ldr r3, [r1, #20]
    mvn r4, r2
    cmp r3, r4
    bne journal_restore_next

    /* Validate payload FNV before considering this slot committed. */
    ldr r5, [r1, #12]
    add r0, r1, #64
    ldr r3, =0x811C9DC5
    ldr r4, =0x01000193
    ldr r6, =0x00002000
journal_restore_hash:
    ldrb r7, [r0], #1
    eor r3, r3, r7
    mul r3, r4, r3
    subs r6, r6, #1
    bne journal_restore_hash
    cmp r3, r5
    bne journal_restore_next
    cmp r11, #0
    beq journal_restore_take
    cmp r2, r10
    bls journal_restore_next
journal_restore_take:
    mov r10, r2
    mov r11, r1
journal_restore_next:
    add r12, r12, #1
    b journal_restore_scan

journal_restore_done:
    cmp r11, #0
    bne journal_restore_copy

    /* Backward-compatible restore of FIX77 GBS2 records.  FIX168A migrates the
     * first changed commit into the new inactive block1 without erasing block0. */
    ldr r0, [r9, #0]
    ldr r1, =0x32534247          /* 'GBS2' */
    cmp r0, r1
    bne restore_done
    ldr r0, [r9, #4]
    cmp r0, #1
    bne restore_done
    ldr r0, [r9, #8]
    ldr r1, =0x00008000
    cmp r0, r1
    bne restore_done
    ldr r5, [r9, #12]
    add r0, r9, #64
    ldr r3, =0x811C9DC5
    ldr r4, =0x01000193
    ldr r6, =0x00008000
legacy_eeprom_hash:
    ldrb r7, [r0], #1
    eor r3, r3, r7
    mul r3, r4, r3
    subs r6, r6, #1
    bne legacy_eeprom_hash
    cmp r3, r5
    bne restore_done
    add r0, r9, #64
    mov r1, #0x0E000000
    ldr r2, =0x00002000         /* only logical EEPROM64K bytes */
legacy_eeprom_copy:
    ldrb r3, [r0], #1
    strb r3, [r1], #1
    subs r2, r2, #1
    bne legacy_eeprom_copy
    b restore_done

journal_restore_copy:
    add r0, r11, #64
    mov r1, #0x0E000000
    ldr r2, =0x00002000
journal_restore_copy_loop:
    ldrb r3, [r0], #1
    strb r3, [r1], #1
    subs r2, r2, #1
    bne journal_restore_copy_loop
    b restore_done

legacy_restore:
    /* Legacy SRAM hotkey format (32 KiB GBS2 snapshot). */
    adr r8, config_save_block_off
    ldr r8, [r8]
    ldr r9, =0x08000000
    add r9, r9, r8
    ldr r0, [r9, #0]
    ldr r1, =0x32534247
    cmp r0, r1
    bne restore_done
    ldr r0, [r9, #4]
    cmp r0, #1
    bne restore_done
    ldr r0, [r9, #8]
    adr r1, config_save_size
    ldr r1, [r1]
    cmp r0, r1
    bne restore_done
    ldr r10, [r9, #12]
    add r0, r9, #64
    mov r1, #0x0E000000
    adr r2, config_save_size
    ldr r2, [r2]
    ldr r3, =0x811C9DC5
legacy_restore_hash:
    ldrb r4, [r0], #1
    eor r3, r3, r4
    ldr r5, =0x01000193
    mul r3, r5, r3
    subs r2, r2, #1
    bne legacy_restore_hash
    cmp r3, r10
    bne restore_done
    add r0, r9, #64
    mov r1, #0x0E000000
    adr r2, config_save_size
    ldr r2, [r2]
legacy_restore_copy:
    ldrb r3, [r0], #1
    strb r3, [r1], #1
    subs r2, r2, #1
    bne legacy_restore_copy

restore_done:
    /* Journal diagnostic mailbox: clear result on each cold boot. */
    ldr r0, =0x0E007F00
    ldr r1, =0x38374A47          /* 'GJ78' */
    str r1, [r0, #0]
    mov r1, #0
    str r1, [r0, #4]
    mvn r1, #0
    str r1, [r0, #8]
    mov r1, #0
    str r1, [r0, #12]
    str r1, [r0, #16]

    /* EEPROM auto-commit countdown lives beside the IRQ vector, following the
     * standard batteryless-patcher convention. */
    ldr r0, =0x03007FFA
    mov r1, #0
    strh r1, [r0]

    /* Install our IRQ trampoline. Games normally write their handler through
     * literals patched from 03007FFC to 03007FF4 by GBABF. */
    ldr r0, =0x04000000
    adr r1, keypad_irq_handler
    str r1, [r0, #-4]            /* 03FFFFFC mirror of 03007FFC */
    adr r0, config_original_entry
    ldr pc, [r0]

/* EEPROM auto-save writer. The ROM-side EEPROM write handler is reduced to an
 * 8-byte Thumb trampoline into this routine. Each SDK EEPROM write resets a
 * one-second VBlank countdown; the journal is committed only after writes go
 * quiet. The existing hotkey remains a fallback. */
.thumb
.thumb_func
eeprom_write_auto:
    lsls r0, r0, #22
    lsrs r0, r0, #19
    ldr r2, 9f
    adds r2, r2, r0
    adds r2, #8
    movs r0, #8
1:  ldrb r3, [r1]
    adds r1, #1
    subs r2, #1
    strb r3, [r2]
    subs r0, #1
    bne 1b
    ldr r2, 8f
    movs r3, #60
    strh r3, [r2]
    movs r0, #0
    bx lr
    .align 2
9:  .word 0x0E000000
8:  .word 0x03007FFA

.arm
/* BIOS calls the user IRQ handler with r0 = 04000000 on common GBA IRQ paths.
 * This follows the same IRQ-vector interposition used by the upstream
 * batteryless patcher: original game handler lives at 03FFFFF4. */
keypad_irq_handler:
    ldr r3, [r0, #0x130]
    lsl r3, r3, #22
    lsr r3, r3, #22
    teq r3, #0x00F3             /* L+R+START+SELECT, active low */
    beq keypad_irq_commit

    /* Auto path: only tick on VBlank.  Each EEPROM write resets this value to
     * 60, so a multi-block game save produces one NOR journal commit. */
    ldr r3, [r0, #0x200]        /* IE | (IF << 16) */
    tst r3, #0x00010000         /* VBlank IF */
    beq keypad_irq_chain
    ldr r2, =0x03007FFA
    ldrh r3, [r2]
    cmp r3, #0
    beq keypad_irq_chain
    subs r3, r3, #1
    strh r3, [r2]
    bne keypad_irq_chain

keypad_irq_commit:
    /* Use system-mode stack; preserve all shared GPRs around a long flash operation. */
    mov r3, #0x9F
    msr cpsr_c, r3
    stmdb sp!, {r0-r12, lr}
    bl flush_sram_to_m36
    /* Preserve the worker result in condition flags across the register restore.
     * Success returns 1; failure returns 0. LDM/MSR below do not alter NZCV. */
    cmp r0, #0
    ldmia sp!, {r0-r12, lr}
    mov r3, #0x92
    msr cpsr_c, r3

    ldr r0, =0x04000000
    /* Do not disturb the game's display on success.  The old payload enabled
     * GREENSWAP for the whole NOR transaction, which caused the visible frame
     * distortion reported on affected commercial titles. Keep it as failure-only
     * diagnostics. */
    moveq r1, #1
    strheq r1, [r0, #2]
    /* Only a manual hotkey needs key-release debouncing. Auto commits arrive
     * here with ordinary key state and chain immediately. */
    ldr r3, [r0, #0x130]
    lsl r3, r3, #22
    lsr r3, r3, #22
    teq r3, #0x00F3
    bne keypad_irq_chain
3:  ldr r3, [r0, #0x130]
    lsl r3, r3, #22
    lsr r3, r3, #22
    teq r3, #0x00F3
    beq 3b
keypad_irq_chain:
    ldr pc, [r0, #-12]

/* Copy the self-contained flash worker below SP and execute it from RAM.
 * FIX77+ preserves/restores the exact occupied game-RAM bytes through spare SRAM,
 * so this no longer assumes the destination region is unused.  Running from RAM
 * avoids fetching ROM code while the M36 is in status/program mode. */
flush_sram_to_m36:
    stmdb sp!, {r4-r11, lr}

    /* FIX78 zero-wear fast return.  This runs while NOR is still in normal
     * read-array mode and BEFORE muting/stopping any hardware.  If the latest
     * committed EEPROM snapshot is byte-identical to SRAM, return immediately:
     * no worker copy, no timer/DMA stop, no VBlank wait, and no NOR command. */
    adr r0, config_save_size
    ldr r0, [r0]
    adr r1, config_save_block_off
    ldr r2, [r1, #4]
    ldr r1, [r1]
    bl journal_quick_unchanged
    cmp r0, #0
    beq flush_changed_save
    ldmia sp!, {r4-r11, lr}
    mov r0, #2
    bx lr

flush_changed_save:
    /* SuperFW-style pause/resume discipline for a long in-game NOR commit.
     *
     * Do NOT clear SOUNDCNT_X (0x84): bit 7 is the GBA master sound enable and
     * clearing it resets PSG state.  FIX75 did that, so restoring only the bit
     * could not restore the game's audio and left a screech/frozen sound state.
     * Instead save/mute SOUNDCNT_L/H, stop all four timers and all four DMA
     * channels, do the ROM-offline worker, then restore at a VBlank boundary.
     */
    sub sp, sp, #44
    ldr r4, =0x04000000

    /* Sound mixer/routing (0x80/0x82), master sound (0x84) left untouched. */
    ldr r5, [r4, #0x80]
    str r5, [sp, #0]
    mov r6, #0
    str r6, [r4, #0x80]

    /* Timer control words only.  Counter/reload registers remain intact. */
    add r7, r4, #0x100
    ldrh r5, [r7, #0x02]
    str r5, [sp, #4]
    ldrh r5, [r7, #0x06]
    str r5, [sp, #8]
    ldrh r5, [r7, #0x0A]
    str r5, [sp, #12]
    ldrh r5, [r7, #0x0E]
    str r5, [sp, #16]
    strh r6, [r7, #0x02]
    strh r6, [r7, #0x06]
    strh r6, [r7, #0x0A]
    strh r6, [r7, #0x0E]

    /* DMA control words.  Source/destination/count values are preserved by HW. */
    ldrh r5, [r4, #0xBA]
    str r5, [sp, #20]
    ldrh r5, [r4, #0xC6]
    str r5, [sp, #24]
    ldrh r5, [r4, #0xD2]
    str r5, [sp, #28]
    ldrh r5, [r4, #0xDE]
    str r5, [sp, #32]
    strh r6, [r4, #0xBA]
    strh r6, [r4, #0xC6]
    strh r6, [r4, #0xD2]
    strh r6, [r4, #0xDE]

    adr r4, flash_worker_start
    /* Keep the payload position-independent without assuming the worker end is
     * encodable by a one-instruction ADR after future runtime fixes grow it. */
    adr r5, flash_worker_size_word
    ldr r6, [r5]
    add r6, r6, #3
    bic r6, r6, #3
    sub r7, sp, r6
    sub r7, r7, #0x200          /* worker stack safety gap */
    bic r7, r7, #3

    /* FIX77: the old code assumed [r7, r7+worker_size) was unused game RAM.
     * V10/V11 proved that false: real/live IWRAM can sit there.  For EEPROM
     * modes the first 8 KiB of SRAM is the complete logical EEPROM64K image,
     * so 0x0E006000 is unused staging space.  Preserve every byte that the
     * worker will occupy there, run the worker, then restore it before the
     * original game IRQ handler is resumed. */
    bl load_cfg_backup_r5
    mvn r12, #0
    cmp r5, r12
    beq 4f
    mov r8, #0x0E000000
    add r8, r8, r5              /* SRAM backup destination */
    mov r9, r7                  /* game RAM source */
    mov r10, r6
3:  cmp r10, #0
    beq 4f
    ldrb r11, [r9], #1
    strb r11, [r8], #1
    subs r10, r10, #1
    bne 3b
4:
    mov r8, r7
    mov r9, r4
    mov r10, r6
5:  cmp r10, #0
    beq 6f
    ldrb r11, [r9], #1
    strb r11, [r8], #1
    subs r10, r10, #1
    bne 5b
6:
    bl load_cfg_save_block_r0
    bl load_cfg_save_size_r2
    bl load_cfg_save_block1_r3
    mov r1, r7
    mov lr, pc
    bx r1
    str r0, [sp, #40]           /* preserve worker success across restore */

    /* Restore the exact game RAM bytes that were temporarily occupied by the
     * worker.  This happens after the NOR has returned to read-array mode but
     * before any timer/DMA/IRQ state is resumed. */
    bl load_cfg_backup_r5
    mvn r12, #0
    cmp r5, r12
    beq 8f
    mov r8, #0x0E000000
    add r8, r8, r5              /* SRAM backup source */
    mov r9, r7                  /* game RAM destination */
    mov r10, r6
7:  cmp r10, #0
    beq 8f
    ldrb r11, [r8], #1
    strb r11, [r9], #1
    subs r10, r10, #1
    bne 7b
8:

    ldr r4, =0x04000000
    /* Restore sound mixer/routing first; SOUNDCNT_X was never reset. */
    ldr r5, [sp, #0]
    str r5, [r4, #0x80]

    /* Re-enter the game on a clean frame boundary, as SuperFW's IGM does. */
6:  ldrh r5, [r4, #0x06]
    cmp r5, #159
    bne 6b
7:  ldrh r5, [r4, #0x06]
    cmp r5, #160
    bne 7b

    /* Resume timers first, then DMA (sound FIFO DMA depends on timers). */
    add r7, r4, #0x100
    ldr r5, [sp, #4]
    strh r5, [r7, #0x02]
    ldr r5, [sp, #8]
    strh r5, [r7, #0x06]
    ldr r5, [sp, #12]
    strh r5, [r7, #0x0A]
    ldr r5, [sp, #16]
    strh r5, [r7, #0x0E]
    ldr r5, [sp, #20]
    strh r5, [r4, #0xBA]
    ldr r5, [sp, #24]
    strh r5, [r4, #0xC6]
    ldr r5, [sp, #28]
    strh r5, [r4, #0xD2]
    ldr r5, [sp, #32]
    strh r5, [r4, #0xDE]

    ldr r0, [sp, #40]
    add sp, sp, #44
    ldmia sp!, {r4-r11, pc}

.align 2
journal_quick_unchanged:
    /* r0=configured save size, r1=block0 ROM offset, r2=block1 ROM offset.
     * Return 1 only when the latest dual GBJ3 snapshot exactly matches SRAM. */
    ldr r3, =0x00002000
    cmp r0, r3
    bne journal_quick_no
    ldr r9, =0x08000000
    add r1, r9, r1              /* block0 absolute */
    add r9, r9, r2              /* configured block1 absolute */
    mov r2, #0                  /* best seq */
    mov r3, #0                  /* best ptr */
    mov r4, #0                  /* global index */
journal_quick_scan:
    cmp r4, #30
    beq journal_quick_scan_done
    cmp r4, #15
    movlo r6, r4
    movlo r5, r1
    subhs r6, r4, #15
    movhs r5, r9
    ldr r7, =0x00002100
    mul r6, r7, r6
    add r5, r5, r6
    ldr r6, [r5, #0]
    ldr r7, =0x334A4247
    cmp r6, r7
    bne journal_quick_next
    ldr r6, [r5, #4]
    cmp r6, #1
    bne journal_quick_next
    ldr r6, [r5, #8]
    ldr r7, =0x00002000
    cmp r6, r7
    bne journal_quick_next
    ldr r6, [r5, #16]
    ldr r7, [r5, #20]
    mvn r8, r6
    cmp r7, r8
    bne journal_quick_next
    cmp r3, #0
    beq journal_quick_take
    cmp r6, r2
    bls journal_quick_next
journal_quick_take:
    mov r2, r6
    mov r3, r5
journal_quick_next:
    add r4, r4, #1
    b journal_quick_scan
journal_quick_scan_done:
    cmp r3, #0
    beq journal_quick_no
    /* Hash current SRAM before the full byte compare. */
    mov r4, #0x0E000000
    ldr r5, =0x00002000
    ldr r6, =0x811C9DC5
    ldr r7, =0x01000193
journal_quick_hash:
    ldrb r8, [r4], #1
    eor r6, r6, r8
    mul r6, r7, r6
    subs r5, r5, #1
    bne journal_quick_hash
    ldr r5, [r3, #12]
    cmp r6, r5
    bne journal_quick_no
    mov r4, #0x0E000000
    add r5, r3, #64
    ldr r7, =0x00002000
journal_quick_compare:
    ldrb r8, [r4], #1
    ldrb r9, [r5], #1
    cmp r8, r9
    bne journal_quick_no
    subs r7, r7, #1
    bne journal_quick_compare
    ldr r4, =0x0E007F00
    ldr r5, =0x38374A47
    str r5, [r4, #0]
    mov r5, #2
    str r5, [r4, #4]
    /* Derive global slot index by walking both 15-slot blocks. */
    mov r5, #0
    mov r7, r1
journal_quick_slot_index:
    cmp r7, r3
    beq journal_quick_slot_found
    add r5, r5, #1
    cmp r5, #15
    moveq r7, r9
    beq journal_quick_slot_index
    add r7, r7, #0x2100
    cmp r5, #30
    blo journal_quick_slot_index
    mvn r5, #0
journal_quick_slot_found:
    str r5, [r4, #8]
    str r2, [r4, #12]
    str r6, [r4, #16]
    mov r0, #1
    bx lr
journal_quick_no:
    mov r0, #0
    bx lr
    .ltorg

.align 2
flash_worker_size_word:
    .word flash_worker_end - flash_worker_start
.align 2
flash_worker_start:
    /* r0 = save block ROM offset. Return r0=1 success, 0 failure.
     * Self-contained: no calls outside [flash_worker_start, flash_worker_end). */
    stmdb sp!, {r4-r12, lr}
    sub sp, sp, #112            /* 64-byte header + locals + dual-block state */
    mov r12, r0                 /* block0 ROM offset */
    str r3, [sp, #96]           /* block1 ROM offset */

    /* Save WAITCNT and force conservative access. */
    ldr r4, =0x04000204
    ldrh r5, [r4]
    str r5, [sp, #64]
    /* Match the hardware-proven M36 timing used by the NDS flasher.
     * Commands/status use 0x000F; E8 payload halfwords use 0x000B. */
    mov r6, #0x0F
    strh r6, [r4]

    /* FIX78 EEPROM64K journal path. r2 is the logical save size passed by the
     * ROM payload.  SRAM mode (32 KiB) falls through to the proven FIX77 path. */
    ldr r6, =0x00002000
    cmp r2, r6
    beq journal_worker
    b legacy_worker

journal_worker:
    /* Diagnostic mailbox at unused EEPROM staging SRAM. */
    ldr r4, =0x0E007F00
    ldr r6, =0x38374A47          /* 'GJ78' */
    str r6, [r4, #0]
    mov r6, #0
    str r6, [r4, #4]
    mvn r6, #0
    str r6, [r4, #8]
    mov r6, #0
    str r6, [r4, #12]
    str r6, [r4, #16]

    /* Hash current 8 KiB logical EEPROM image. */
    mov r4, #0x0E000000
    ldr r6, =0x00002000
    ldr r7, =0x811C9DC5
    ldr r8, =0x01000193
journal_hash_sram:
    ldrb r9, [r4], #1
    eor r7, r7, r9
    mul r7, r8, r7
    subs r6, r6, #1
    bne journal_hash_sram
    str r7, [sp, #68]           /* current hash */

    /* Absolute dual save-block bases. */
    ldr r11, =0x08000000
    add r11, r11, r12           /* block0 absolute */
    ldr r5, [sp, #96]
    ldr r6, =0x08000000
    add r5, r5, r6
    str r5, [sp, #104]          /* block1 absolute */
    mov r6, #0
    str r6, [sp, #72]           /* best sequence */
    str r6, [sp, #76]           /* best pointer */
    mvn r6, #0
    str r6, [sp, #80]           /* best global slot 0..29 */
    mov r6, #0
    str r6, [sp, #84]           /* first free pointer */
    str r6, [sp, #100]          /* first free block base */
    mvn r6, #0
    str r6, [sp, #88]           /* first free global slot */
    mov r10, #0                 /* global slot index */

journal_scan_headers:
    cmp r10, #30
    beq journal_scan_done
    cmp r10, #15
    movlo r6, r10
    movlo r5, r11
    subhs r6, r10, #15
    ldrhs r5, [sp, #104]
    mov r9, r5                  /* current block base */
    ldr r7, =0x00002100
    mul r6, r7, r6
    add r5, r5, r6
    ldr r6, [r5, #0]
    ldr r7, =0x334A4247         /* 'GBJ3' */
    cmp r6, r7
    beq journal_scan_valid
    /* A free slot has both validity header and reservation marker erased.  A
     * power-failed partial append has the marker programmed and is skipped. */
    mvn r7, #0
    cmp r6, r7
    bne journal_scan_next
    ldr r6, [r5, #32]
    cmp r6, r7
    bne journal_scan_next
    ldr r6, [sp, #84]
    cmp r6, #0
    streq r5, [sp, #84]
    streq r10, [sp, #88]
    streq r9, [sp, #100]
    b journal_scan_next
journal_scan_valid:
    ldr r6, [r5, #4]
    cmp r6, #1
    bne journal_scan_next
    ldr r6, [r5, #8]
    ldr r7, =0x00002000
    cmp r6, r7
    bne journal_scan_next
    ldr r6, [r5, #16]           /* sequence */
    ldr r7, [r5, #20]
    mvn r8, r6
    cmp r7, r8
    bne journal_scan_next
    ldr r7, [sp, #76]
    cmp r7, #0
    beq journal_scan_take
    ldr r7, [sp, #72]
    cmp r6, r7
    bls journal_scan_next
journal_scan_take:
    str r6, [sp, #72]
    str r5, [sp, #76]
    str r10, [sp, #80]
journal_scan_next:
    add r10, r10, #1
    b journal_scan_headers

journal_scan_done:
    /* If this block still holds the legacy FIX77 GBS2 snapshot and no FIX78
     * journal slot exists yet, migrate atomically on the first commit: erase
     * once and write the already-restored current SRAM image into slot 0. */
    ldr r6, [sp, #76]
    cmp r6, #0
    bne journal_scan_post_migration
    ldr r6, [r11, #0]
    ldr r7, =0x32534247
    cmp r6, r7
    bne journal_scan_post_migration
    /* Preserve the legacy authoritative GBS2 block0.  The analyzer/installer
     * guarantees a clean inactive block1, so migrate by appending there with
     * zero erase instead of rewriting or erasing block0. */
    ldr r6, [sp, #104]
    str r6, [sp, #84]
    str r6, [sp, #100]
    mov r6, #15
    str r6, [sp, #88]
journal_scan_post_migration:
    /* If latest committed snapshot is byte-identical, return immediately with
     * zero NOR commands, zero wear and only a very short pause. */
    ldr r5, [sp, #76]
    cmp r5, #0
    beq journal_need_write
    ldr r6, [r5, #12]
    ldr r7, [sp, #68]
    cmp r6, r7
    bne journal_need_write
    mov r4, #0x0E000000
    add r5, r5, #64
    ldr r6, =0x00002000
journal_compare_latest:
    ldrb r7, [r4], #1
    ldrb r8, [r5], #1
    cmp r7, r8
    bne journal_need_write
    subs r6, r6, #1
    bne journal_compare_latest
    ldr r4, =0x0E007F00
    mov r6, #2                  /* unchanged / zero wear */
    str r6, [r4, #4]
    ldr r6, [sp, #80]
    str r6, [r4, #8]
    ldr r6, [sp, #72]
    str r6, [r4, #12]
    ldr r6, [sp, #68]
    str r6, [r4, #16]
    b journal_return_success

journal_need_write:
    /* Prefer any virgin slot across both 128 KiB blocks.  Only when all 30
     * slots are consumed erase the block that does NOT contain the newest
     * committed record.  The previous save therefore remains authoritative
     * throughout erase/program/header-last rollover. */
    ldr r5, [sp, #84]
    cmp r5, #0
    bne journal_have_target
    ldr r6, [sp, #76]           /* latest pointer; required when both full */
    cmp r6, #0
    beq worker_fail_finish
    /* Health-first rollover guard: headers are cheap to scan during normal
     * commits, but before erasing either full block validate the apparent
     * newest record's complete 8 KiB FNV.  If metadata/data was corrupted,
     * fail closed and preserve both blocks for NDS recovery. */
    add r4, r6, #64
    ldr r5, =0x00002000
    ldr r7, =0x811C9DC5
    ldr r8, =0x01000193
journal_rollover_hash:
    ldrb r9, [r4], #1
    eor r7, r7, r9
    mul r7, r8, r7
    subs r5, r5, #1
    bne journal_rollover_hash
    ldr r5, [r6, #12]
    cmp r7, r5
    bne worker_fail_finish
    ldr r7, [sp, #104]          /* block1 absolute */
    cmp r6, r7
    blo journal_full_latest_b0
    mov r11, r12                /* latest is block1: erase block0 */
    ldr r8, =0x08000000
    add r11, r11, r8
    mov r5, r11
    mov r6, #0                  /* global slot 0 */
    b journal_full_target
journal_full_latest_b0:
    mov r11, r7                 /* latest is block0: erase block1 */
    mov r5, r11
    mov r6, #15                 /* global slot 15 */
journal_full_target:
    str r5, [sp, #84]
    str r6, [sp, #88]
    str r11, [sp, #100]
    mov r6, #3                  /* inactive-block erase + append */
    str r6, [sp, #92]
    bl journal_unlock_block
    cmp r0, #0
    beq worker_fail_finish
    bl journal_erase_block
    cmp r0, #0
    beq worker_fail_finish
    bl journal_unlock_block
    cmp r0, #0
    beq worker_fail_finish
    b journal_target_ready
journal_have_target:
    mov r6, #1                  /* append without erase */
    str r6, [sp, #92]
    ldr r11, [sp, #100]         /* unlock the block containing free slot */
    bl journal_unlock_block
    cmp r0, #0
    beq worker_fail_finish
journal_target_ready:
    ldr r5, [sp, #84]           /* target slot base */
    ldr r6, [sp, #72]
    add r6, r6, #1
    /* Numeric sequence ordering is intentionally simple and shared with the
     * NDS reader.  Never create the reserved 0/FFFFFFFF values or erase an
     * authoritative block merely to wrap an astronomically old journal. */
    cmp r6, #0
    beq worker_fail_finish
    mvn r7, #0
    cmp r6, r7
    beq worker_fail_finish
    str r6, [sp, #72]           /* new sequence */

    /* Reservation marker is written first. If power dies after this point,
     * the slot is never mistaken for erased and its validity header remains FF. */
    add r4, sp, #0
    mvn r6, #0
    mov r7, #8
journal_reserve_fill:
    str r6, [r4], #4
    subs r7, r7, #1
    bne journal_reserve_fill
    ldr r6, =0x31565352         /* 'RSV1' */
    str r6, [sp, #0]
    add r4, sp, #0
    add r5, r5, #32
    bl journal_program32
    cmp r0, #0
    beq worker_fail_finish

    /* Program only the logical 8 KiB EEPROM image, 32 bytes per proven E8 tx. */
    mov r4, #0x0E000000
    ldr r5, [sp, #84]
    add r5, r5, #64
    ldr r10, =0x00002000
journal_program_payload:
    bl journal_program32
    cmp r0, #0
    beq worker_fail_finish
    add r4, r4, #32
    add r5, r5, #32
    subs r10, r10, #32
    bne journal_program_payload

    /* Verify complete data before making the slot valid. */
    ldr r4, [sp, #84]
    add r4, r4, #64
    ldr r6, =0x00002000
    ldr r7, =0x811C9DC5
    ldr r8, =0x01000193
journal_verify_hash:
    ldrb r9, [r4], #1
    eor r7, r7, r9
    mul r7, r8, r7
    subs r6, r6, #1
    bne journal_verify_hash
    ldr r6, [sp, #68]
    cmp r7, r6
    bne worker_fail_reset

    /* Build and write the 32-byte validity header LAST. */
    add r4, sp, #0
    mvn r6, #0
    mov r7, #8
journal_header_fill:
    str r6, [r4], #4
    subs r7, r7, #1
    bne journal_header_fill
    ldr r6, =0x334A4247         /* 'GBJ3' */
    str r6, [sp, #0]
    mov r6, #1
    str r6, [sp, #4]
    ldr r6, =0x00002000
    str r6, [sp, #8]
    ldr r6, [sp, #68]
    str r6, [sp, #12]
    ldr r6, [sp, #72]
    str r6, [sp, #16]
    mvn r6, r6
    str r6, [sp, #20]
    mov r6, #1
    str r6, [sp, #24]
    add r4, sp, #0
    ldr r5, [sp, #84]
    bl journal_program32
    cmp r0, #0
    beq worker_fail_finish

    /* Header readback. Data hash was already checked before validity. */
    ldr r5, [sp, #84]
    ldr r6, [r5, #0]
    ldr r7, =0x334A4247
    cmp r6, r7
    bne worker_fail_reset
    ldr r6, [r5, #12]
    ldr r7, [sp, #68]
    cmp r6, r7
    bne worker_fail_reset
    ldr r6, [r5, #16]
    ldr r7, [sp, #72]
    cmp r6, r7
    bne worker_fail_reset
    ldr r6, [r5, #20]
    mvn r7, r7
    cmp r6, r7
    bne worker_fail_reset

    /* Export compact diagnostics to spare SRAM for V13 and future field logs. */
    ldr r4, =0x0E007F00
    ldr r6, [sp, #92]
    str r6, [r4, #4]
    ldr r6, [sp, #88]
    str r6, [r4, #8]
    ldr r6, [sp, #72]
    str r6, [r4, #12]
    ldr r6, [sp, #68]
    str r6, [r4, #16]

journal_return_success:
    ldr r4, =0x04000204
    ldr r5, [sp, #64]
    strh r5, [r4]
    add sp, sp, #112
    ldmia sp!, {r4-r12, lr}
    /* Mailbox result remains 1 append, 2 unchanged, or 3 wrap+erase. */
    ldr r1, =0x0E007F04
    ldr r0, [r1]
    bx lr

/* Helpers below are copied together with the worker, so all calls remain valid
 * while the M36 is in status/program mode. */
journal_unlock_block:
    mov r6, #0x50
    strh r6, [r11]
    mov r6, #0x60
    strh r6, [r11]
    mov r6, #0xD0
    strh r6, [r11]
    ldr r9, =2000000
journal_unlock_wait:
    ldrh r6, [r11]
    tst r6, #0x80
    bne journal_unlock_ready
    subs r9, r9, #1
    bne journal_unlock_wait
    mov r6, #0xFF
    strh r6, [r11]
    mov r0, #0
    bx lr
journal_unlock_ready:
    tst r6, #0x3A
    bne journal_unlock_error
    mov r6, #0xFF
    strh r6, [r11]
    mov r0, #1
    bx lr
journal_unlock_error:
    mov r6, #0x50
    strh r6, [r11]
    mov r6, #0xFF
    strh r6, [r11]
    mov r0, #0
    bx lr

journal_erase_block:
    mov r6, #0x50
    strh r6, [r11]
    mov r6, #0x20
    strh r6, [r11]
    mov r6, #0xD0
    strh r6, [r11]
    ldr r9, =12000000
journal_erase_wait:
    ldrh r6, [r11]
    tst r6, #0x80
    bne journal_erase_ready
    subs r9, r9, #1
    bne journal_erase_wait
    mov r6, #0xFF
    strh r6, [r11]
    mov r0, #0
    bx lr
journal_erase_ready:
    tst r6, #0x3A
    bne journal_erase_error
    mov r6, #0xFF
    strh r6, [r11]
    mov r0, #1
    bx lr
journal_erase_error:
    mov r6, #0x50
    strh r6, [r11]
    mov r6, #0xFF
    strh r6, [r11]
    mov r0, #0
    bx lr

/* r4=32-byte source, r5=32-byte aligned NOR destination. */
journal_program32:
    mov r6, #0x50
    strh r6, [r5]
    mov r6, #0xE8
    strh r6, [r5]
    ldr r9, =1000000
journal_program_ready_wait:
    ldrh r6, [r5]
    tst r6, #0x80
    bne journal_program_ready
    subs r9, r9, #1
    bne journal_program_ready_wait
    mov r6, #0xFF
    strh r6, [r5]
    mov r0, #0
    bx lr
journal_program_ready:
    tst r6, #0x3A
    bne journal_program_error
    mov r6, #15
    strh r6, [r5]
    ldr r6, =0x04000204
    mov r8, #0x0B
    strh r8, [r6]
    mov r7, #0
journal_program_data:
    ldrb r8, [r4, r7]
    add r9, r7, #1
    ldrb r9, [r4, r9]
    orr r8, r8, r9, lsl #8
    strh r8, [r5, r7]
    add r7, r7, #2
    cmp r7, #32
    blo journal_program_data
    mov r7, #64
journal_program_settle:
    subs r7, r7, #1
    bne journal_program_settle
    ldr r6, =0x04000204
    mov r8, #0x0F
    strh r8, [r6]
    mov r6, #0xD0
    strh r6, [r5]
    ldr r9, =2000000
journal_program_done_wait:
    ldrh r6, [r5]
    tst r6, #0x80
    bne journal_program_done
    subs r9, r9, #1
    bne journal_program_done_wait
    mov r6, #0xFF
    strh r6, [r5]
    mov r0, #0
    bx lr
journal_program_done:
    tst r6, #0x3A
    bne journal_program_error
    mov r6, #0xFF
    strh r6, [r5]
    mov r0, #1
    bx lr
journal_program_error:
    mov r6, #0x50
    strh r6, [r5]
    mov r6, #0xFF
    strh r6, [r5]
    mov r0, #0
    bx lr

legacy_worker:
    /* Build header in RAM; valid header is programmed last. */
    add r4, sp, #0
    ldr r6, =0xFFFFFFFF
    mov r7, #16
6:  str r6, [r4], #4
    subs r7, r7, #1
    bne 6b
    ldr r6, =0x32534247
    str r6, [sp, #0]
    mov r6, #1
    str r6, [sp, #4]
    ldr r6, =0x00008000
    str r6, [sp, #8]

    /* Hash SRAM before changing flash. */
    mov r4, #0x0E000000
    ldr r6, =0x00008000
    ldr r7, =0x811C9DC5
    ldr r8, =0x01000193
7:  ldrb r9, [r4], #1
    eor r7, r7, r9
    mul r7, r8, r7
    subs r6, r6, #1
    bne 7b
    str r7, [sp, #12]

    /* base absolute ROM address */
    ldr r11, =0x08000000
    add r11, r11, r12

    /* Unlock block: 50,60,D0, wait ready, reject error bits, FF. */
    mov r6, #0x50
    strh r6, [r11]
    mov r6, #0x60
    strh r6, [r11]
    mov r6, #0xD0
    strh r6, [r11]
    ldr r9, =2000000
8:  ldrh r6, [r11]
    tst r6, #0x80
    bne 9f
    subs r9, r9, #1
    bne 8b
    b worker_fail_reset
9:  tst r6, #0x3A
    bne worker_fail_clear
    mov r6, #0xFF
    strh r6, [r11]

    /* Erase 128 KiB block. */
    mov r6, #0x50
    strh r6, [r11]
    mov r6, #0x20
    strh r6, [r11]
    mov r6, #0xD0
    strh r6, [r11]
    ldr r9, =12000000
10: ldrh r6, [r11]
    tst r6, #0x80
    bne 11f
    subs r9, r9, #1
    bne 10b
    b worker_fail_reset
11: tst r6, #0x3A
    bne worker_fail_clear
    mov r6, #0xFF
    strh r6, [r11]

    /* Unlock again after erase. */
    mov r6, #0x50
    strh r6, [r11]
    mov r6, #0x60
    strh r6, [r11]
    mov r6, #0xD0
    strh r6, [r11]
    ldr r9, =2000000
12: ldrh r6, [r11]
    tst r6, #0x80
    bne 13f
    subs r9, r9, #1
    bne 12b
    b worker_fail_reset
13: tst r6, #0x3A
    bne worker_fail_clear
    mov r6, #0xFF
    strh r6, [r11]

    /* Program 32 KiB SRAM payload at block+64 using proven 32-byte E8 transactions. */
    mov r4, #0x0E000000          /* src */
    add r5, r11, #64             /* dst absolute */
    ldr r10, =0x00008000
14: cmp r10, #0
    beq 22f
    mov r6, #0x50
    strh r6, [r5]
    mov r6, #0xE8
    strh r6, [r5]
    ldr r9, =1000000
15: ldrh r6, [r5]
    tst r6, #0x80
    bne 16f
    subs r9, r9, #1
    bne 15b
    b worker_fail_reset_at_r5
16: tst r6, #0x3A
    bne worker_fail_clear_at_r5
    mov r6, #15
    strh r6, [r5]
    ldr r6, =0x04000204
    mov r8, #0x0B
    strh r8, [r6]
    mov r7, #0
17: ldrb r8, [r4, r7]
    add r9, r7, #1
    ldrb r9, [r4, r9]
    orr r8, r8, r9, lsl #8
    strh r8, [r5, r7]
    add r7, r7, #2
    cmp r7, #32
    blo 17b
    /* Data settle before switching back to command/status timing. */
    mov r7, #64
18: subs r7, r7, #1
    bne 18b
    ldr r6, =0x04000204
    mov r8, #0x0F
    strh r8, [r6]
    mov r6, #0xD0
    strh r6, [r5]
    ldr r9, =2000000
30: ldrh r6, [r5]
    tst r6, #0x80
    bne 31f
    subs r9, r9, #1
    bne 30b
    b worker_fail_reset_at_r5
31: tst r6, #0x3A
    bne worker_fail_clear_at_r5
    mov r6, #0xFF
    strh r6, [r5]
    /* immediate 32-byte verify */
    mov r7, #0
20: ldrb r8, [r4, r7]
    ldrb r9, [r5, r7]
    cmp r8, r9
    bne worker_fail_reset_at_r5
    add r7, r7, #1
    cmp r7, #32
    blo 20b
    add r4, r4, #32
    add r5, r5, #32
    subs r10, r10, #32
    b 14b

22: /* Program 64-byte header last (two E8 transactions). */
    add r4, sp, #0
    mov r5, r11
    mov r10, #64
23: mov r6, #0x50
    strh r6, [r5]
    mov r6, #0xE8
    strh r6, [r5]
    ldr r9, =1000000
24: ldrh r6, [r5]
    tst r6, #0x80
    bne 25f
    subs r9, r9, #1
    bne 24b
    b worker_fail_reset_at_r5
25: tst r6, #0x3A
    bne worker_fail_clear_at_r5
    mov r6, #15
    strh r6, [r5]
    ldr r6, =0x04000204
    mov r8, #0x0B
    strh r8, [r6]
    mov r7, #0
26: ldrh r8, [r4, r7]
    strh r8, [r5, r7]
    add r7, r7, #2
    cmp r7, #32
    blo 26b
    mov r7, #64
32: subs r7, r7, #1
    bne 32b
    ldr r6, =0x04000204
    mov r8, #0x0F
    strh r8, [r6]
    mov r6, #0xD0
    strh r6, [r5]
    ldr r9, =2000000
27: ldrh r6, [r5]
    tst r6, #0x80
    bne 28f
    subs r9, r9, #1
    bne 27b
    b worker_fail_reset_at_r5
28: tst r6, #0x3A
    bne worker_fail_clear_at_r5
    mov r6, #0xFF
    strh r6, [r5]
    add r4, r4, #32
    add r5, r5, #32
    subs r10, r10, #32
    bne 23b

    /* Validate header hash + payload after returning to array mode. */
    ldr r6, [r11, #0]
    ldr r7, =0x32534247
    cmp r6, r7
    bne worker_fail_reset
    ldr r7, [sp, #12]
    ldr r6, [r11, #12]
    cmp r6, r7
    bne worker_fail_reset
    add r4, r11, #64
    ldr r6, =0x00008000
    ldr r8, =0x811C9DC5
    ldr r9, =0x01000193
29: ldrb r10, [r4], #1
    eor r8, r8, r10
    mul r8, r9, r8
    subs r6, r6, #1
    bne 29b
    cmp r8, r7
    bne worker_fail_reset

    ldr r4, =0x04000204
    ldr r5, [sp, #64]
    strh r5, [r4]
    add sp, sp, #112
    ldmia sp!, {r4-r12, lr}
    mov r0, #1
    bx lr

worker_fail_clear_at_r5:
    mov r6, #0x50
    strh r6, [r5]
worker_fail_reset_at_r5:
    mov r6, #0xFF
    strh r6, [r5]
    b worker_fail_finish
worker_fail_clear:
    mov r6, #0x50
    strh r6, [r11]
worker_fail_reset:
    mov r6, #0xFF
    strh r6, [r11]
worker_fail_finish:
    ldr r4, =0x04000204
    ldr r5, [sp, #64]
    strh r5, [r4]
    add sp, sp, #112
    ldmia sp!, {r4-r12, lr}
    mov r0, #0
    bx lr
    .ltorg
flash_worker_end:

    .ltorg
payload_end:
