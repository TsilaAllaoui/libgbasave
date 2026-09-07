.syntax unified
.cpu arm7tdmi
.arm
.section .text,"ax",%progbits
.global payload_start
.global payload_entry
.global config_original_entry
.global config_save_block_off
.global config_save_size
.global config_ram_backup_sram_off
.global config_hotkey_raw
.global flash_worker_start
.global flash_worker_end

/* FIX166A generic SRAM32 RWW-safe wear/speed journal.
 *
 * SRAM remains the game's native 32 KiB working save at 0x0E000000.  Every
 * ROM patch/storage location is supplied by libgbasave-generated analyzer plan.
 * Runtime persistence keeps the proven full 32 KiB snapshot semantics, but
 * uses six append-only GBJ4 slots across two 128 KiB blocks in the already
 * plan-reserved RWW save bank. Most commits therefore avoid a block erase.
 *
 * The FIX163A safety property remains: the NOR worker is NEVER copied below the
 * interrupted game's stack and no game EWRAM/IWRAM is used as a workspace.
 * The worker executes directly from its payload ROM bank while M36 erases/
 * programs a plan-provided, dedicated DIFFERENT 1 MiB flash bank.  This uses
 * the M36 read-while-write bank architecture and removes the FIX162B BPPE
 * resume corruption mechanism entirely.
 */
payload_start:
    .ascii "GBF166AGENJ4RWW"
    .byte 0
    .align 2
config_original_entry:
    .word 0x080000C0
config_save_block_off:
    .word 0x00F00000
config_save_size:
    .word 0x00008000
/* Kept only for the NDS config ABI. FIX163A has no SRAM/EWRAM spill. */
config_ram_backup_sram_off:
    .word 0xFFFFFFFF
/* Patched by the NDS from GBABR.CFG. KEYINPUT active-low exact raw value. */
config_hotkey_raw:
    .word 0x000000F9

load_cfg_save_block_r0:
    adr r0, config_save_block_off
    ldr r0, [r0]
    bx lr
load_cfg_save_size_r2:
    adr r2, config_save_size
    ldr r2, [r2]
    bx lr

/* GBJ4 full-snapshot journal constants.  Two 128 KiB blocks live inside the
 * dedicated 1 MiB save bank supplied by the analyzer plan.  Each block has three
 * 0x8100-byte slots: 32-byte validity header, 32-byte reservation marker,
 * 32768-byte save image, then erased padding. */

/* r0 = block0 ROM offset. Return r0 = latest shaped GBJ4 slot absolute ROM
 * address or 0; r1 = sequence.  Header-last + reservation-first means shaped
 * headers are committed candidates. Data hash is checked by the caller. */
gbj4_find_latest_shape:
    stmdb sp!, {r4-r9, lr}
    ldr r4, =0x08000000
    add r4, r4, r0                 /* block0 absolute */
    mov r8, #0                     /* best pointer */
    mov r9, #0                     /* best sequence */
    mov r7, #0                     /* block index */
0:
    mov r5, r4                     /* slot pointer */
    mov r6, #0                     /* slot index */
1:
    ldr r0, [r5, #0]
    ldr r1, =0x344A4247            /* GBJ4 */
    cmp r0, r1
    bne 2f
    ldr r0, [r5, #4]
    cmp r0, #1
    bne 2f
    ldr r0, [r5, #8]
    ldr r1, =0x00008000
    cmp r0, r1
    bne 2f
    ldr r0, [r5, #32]
    ldr r1, =0x34565352            /* RSV4 */
    cmp r0, r1
    bne 2f
    ldr r0, [r5, #16]              /* seq */
    cmp r0, #0
    beq 2f
    mvn r1, #0
    cmp r0, r1
    beq 2f
    ldr r1, [r5, #20]
    mvn r2, r0
    cmp r1, r2
    bne 2f
    ldr r1, [r5, #24]
    ldr r2, =0x454E4F44            /* DONE */
    cmp r1, r2
    bne 2f
    ldr r1, [r5, #28]
    mvn r2, r2
    cmp r1, r2
    bne 2f
    cmp r8, #0
    beq 3f
    cmp r0, r9
    bls 2f
3:  mov r8, r5
    mov r9, r0
2:
    ldr r0, =0x00008100
    add r5, r5, r0
    add r6, r6, #1
    cmp r6, #3
    blo 1b
    add r7, r7, #1
    cmp r7, #2
    bhs 4f
    ldr r0, =0x00020000
    add r4, r4, r0
    b 0b
4:  mov r0, r8
    mov r1, r9
    ldmia sp!, {r4-r9, lr}
    bx lr

/* Boot: prefer the newest validated GBJ4 slot.  Legacy GBS2 remains readable
 * so an interrupted upgrade never strands the last proven full snapshot. */
payload_entry:
    adr r8, config_save_block_off
    ldr r8, [r8]
    mov r0, r8
    bl gbj4_find_latest_shape
    cmp r0, #0
    beq restore_try_legacy_gbs2
    mov r9, r0                      /* latest slot absolute */
    ldr r10, [r9, #12]              /* expected FNV */
    add r0, r9, #64
    ldr r2, =0x00008000
    ldr r3, =0x811C9DC5
    ldr r5, =0x01000193
restore_gbj4_hash:
    ldrb r4, [r0], #1
    eor r3, r3, r4
    mul r3, r5, r3
    subs r2, r2, #1
    bne restore_gbj4_hash
    cmp r3, r10
    bne restore_try_legacy_gbs2
    add r0, r9, #64
    mov r1, #0x0E000000
    ldr r2, =0x00008000
restore_gbj4_copy:
    ldrb r3, [r0], #1
    strb r3, [r1], #1
    subs r2, r2, #1
    bne restore_gbj4_copy
    b restore_done

restore_try_legacy_gbs2:
    ldr r9, =0x08000000
    add r9, r9, r8
    ldr r0, [r9, #0]
    ldr r1, =0x32534247          /* GBS2 */
    cmp r0, r1
    bne restore_done
    ldr r0, [r9, #4]
    cmp r0, #1
    bne restore_done
    ldr r0, [r9, #8]
    ldr r1, =0x00008000
    cmp r0, r1
    bne restore_done
    ldr r10, [r9, #12]
    add r0, r9, #64
    ldr r2, =0x00008000
    ldr r3, =0x811C9DC5
    ldr r5, =0x01000193
restore_legacy_hash:
    ldrb r4, [r0], #1
    eor r3, r3, r4
    mul r3, r5, r3
    subs r2, r2, #1
    bne restore_legacy_hash
    cmp r3, r10
    bne restore_done
    add r0, r9, #64
    mov r1, #0x0E000000
    ldr r2, =0x00008000
restore_legacy_copy:
    ldrb r3, [r0], #1
    strb r3, [r1], #1
    subs r2, r2, #1
    bne restore_legacy_copy

restore_done:
    /* 03007FFA low byte is the BIOS SoftReset return-target flag.  FIX163A
     * never writes it.  Only the high byte (03007FFB) is our one-shot latch. */
    ldr r0, =0x03007FFB
    mov r1, #0
    strb r1, [r0]

    /* Analyzer-proven ROM literals that originally targeted 03007FFC are
     * redirected to 03007FF4, so the game installs its original handler there.
     * Our trampoline stays at the real BIOS user-IRQ vector. */
    ldr r0, =0x04000000
    adr r2, generic_dummy_irq
    str r2, [r0, #-12]           /* 03FFFFF4 mirror */
    adr r1, generic_irq_handler
    str r1, [r0, #-4]            /* 03FFFFFC mirror */
    adr r0, config_original_entry
    ldr pc, [r0]

generic_dummy_irq:
    bx lr

/* Generic manual persistence.  Exact KEYINPUT matching is intentional: extra
 * held keys do not trigger a save.  The high-byte latch makes one commit per
 * press without blocking IRQ return while the chord remains held. */
generic_irq_handler:
    ldr r2, =0x04000130
    ldrh r3, [r2]
    lsl r3, r3, #22
    lsr r3, r3, #22              /* 10-bit live KEYINPUT */
    adr r2, config_hotkey_raw
    ldr r2, [r2]
    cmp r3, r2                    /* exact chord only */
    bne generic_irq_rearm_chain

    ldr r2, =0x03007FFB
    ldrb r3, [r2]
    cmp r3, #0
    bne generic_irq_chain         /* still held: already consumed */
    mov r3, #1
    strb r3, [r2]                 /* latch before the long operation */

    /* Long NOR code runs with IRQs masked on the normal System stack.  Unlike
     * FIX162B, only ordinary call-frame space is consumed; no arbitrary region
     * below SP is overwritten with executable code. */
    mov r3, #0x9F                /* System, IRQ masked */
    msr cpsr_c, r3
    stmdb sp!, {r0-r12, lr}
    bl flush_sram_hotkey_to_m36
    ldmia sp!, {r0-r12, lr}
    mov r3, #0x92                /* IRQ mode, IRQ masked */
    msr cpsr_c, r3
    ldr r0, =0x04000000
    b generic_irq_chain

generic_irq_rearm_chain:
    ldr r2, =0x03007FFB
    mov r3, #0
    strb r3, [r2]

generic_irq_chain:
    /* BIOS user-IRQ handlers are conventionally entered with r0=04000000.
     * Recreate that contract explicitly instead of depending on our incoming
     * scratch-register state before tail-chaining the game's handler. */
    ldr r0, =0x04000000
    ldr pc, [r0, #-12]

/* Return nonzero when live SRAM is already byte-identical to the newest
 * valid GBJ4 snapshot.  Legacy GBS2 is retained as a migration fallback.
 * Changed saves normally exit the compare on the first differing byte. */
generic_sram_quick_unchanged:
    stmdb sp!, {r4-r10, lr}
    bl load_cfg_save_block_r0
    mov r10, r0                    /* block0 offset */
    bl gbj4_find_latest_shape
    cmp r0, #0
    beq quick_try_legacy
    mov r1, r0                     /* latest slot absolute */
    mov r4, #0x0E000000
    add r5, r1, #64
    ldr r6, =0x00008000
    ldr r9, =0x811C9DC5
    ldr r10, =0x01000193
1:  ldrb r7, [r4], #1
    ldrb r8, [r5], #1
    cmp r7, r8
    bne 9f
    /* FIX166A speed-only optimization: hash the committed NOR byte while
     * doing the equality pass.  An unchanged save now needs one 32 KiB NOR
     * read instead of compare + second full hash pass.  Changed saves still
     * return on the first mismatch and the commit path is untouched. */
    eor r9, r9, r8
    mul r9, r10, r9
    subs r6, r6, #1
    bne 1b
    ldr r3, [r1, #12]
    cmp r9, r3
    bne 9f
    ldmia sp!, {r4-r10, lr}
    mov r0, #1
    bx lr

quick_try_legacy:
    ldr r1, =0x08000000
    add r1, r1, r10
    ldr r2, [r1, #0]
    ldr r3, =0x32534247
    cmp r2, r3
    bne 9f
    ldr r2, [r1, #4]
    cmp r2, #1
    bne 9f
    ldr r2, [r1, #8]
    ldr r3, =0x00008000
    cmp r2, r3
    bne 9f
    mov r4, #0x0E000000
    add r5, r1, #64
    ldr r6, =0x00008000
    ldr r9, =0x811C9DC5
    ldr r10, =0x01000193
3:  ldrb r7, [r4], #1
    ldrb r8, [r5], #1
    cmp r7, r8
    bne 9f
    eor r9, r9, r8
    mul r9, r10, r9
    subs r6, r6, #1
    bne 3b
    ldr r3, [r1, #12]
    cmp r9, r3
    bne 9f
    ldmia sp!, {r4-r10, lr}
    mov r0, #1
    bx lr
9:  ldmia sp!, {r4-r10, lr}
    mov r0, #0
    bx lr

/* The full-snapshot GBS2 worker is the exact hardware-proven FIX109 worker.
 * FIX163A calls it directly from ROM using M36 separate-bank RWW. */
flush_sram_hotkey_to_m36:
    stmdb sp!, {r4-r11, lr}
    bl generic_sram_quick_unchanged
    cmp r0, #0
    beq flush_resume_changed
    ldmia sp!, {r4-r11, lr}
    mov r0, #2
    bx lr

flush_resume_changed:
    /* Runtime RWW guard.  Save block must start a 1 MiB bank; the entire
     * frozen worker must live in one different 1 MiB ROM bank.  Refusal means
     * zero NOR writes and an immediate normal game resume. */
    bl load_cfg_save_block_r0
    mov r6, r0, lsl #12           /* nonzero iff low 20 offset bits are nonzero */
    cmp r6, #0
    bne flush_resume_fail_no_write

    adr r4, flash_worker_start
    adr r5, flash_worker_size_word
    ldr r5, [r5]
    add r5, r4, r5
    sub r5, r5, #1
    ldr r6, =0x08000000
    sub r7, r4, r6
    sub r8, r5, r6
    mov r7, r7, lsr #20           /* code start bank */
    mov r8, r8, lsr #20           /* code end bank */
    cmp r7, r8
    bne flush_resume_fail_no_write
    mov r9, r0, lsr #20           /* dedicated save bank */
    cmp r7, r9
    beq flush_resume_fail_no_write

    /* FIX163A resume discipline deliberately reuses the hardware policy that
     * was already proven end-to-end on Pokémon Pinball in FIX146B.  Save IME,
     * all four timer controls and DMA control words.  Stop timers while the
     * flash transaction is active.  Preserve enabled SPECIAL DMA1/2 channels
     * (DirectSound FIFO) exactly as-is so their hidden current-source latches
     * are never reset; disable only other DMA channels.  SOUNDCNT is observed
     * but never rewritten. */
    sub sp, sp, #40
    ldr r4, =0x04000208
    ldrh r5, [r4]
    str r5, [sp, #0]               /* IME */
    mov r10, #0
    strh r10, [r4]

    ldr r4, =0x04000000
    ldr r5, [r4, #0x80]
    str r5, [sp, #4]               /* SOUNDCNT_L/H read-only snapshot */
    add r6, r4, #0x100
    ldrh r5, [r6, #0x02]
    str r5, [sp, #8]               /* TM0CNT_H */
    ldrh r5, [r6, #0x06]
    str r5, [sp, #12]              /* TM1CNT_H */
    ldrh r5, [r6, #0x0A]
    str r5, [sp, #16]              /* TM2CNT_H */
    ldrh r5, [r6, #0x0E]
    str r5, [sp, #20]              /* TM3CNT_H */
    strh r10, [r6, #0x02]
    strh r10, [r6, #0x06]
    strh r10, [r6, #0x0A]
    strh r10, [r6, #0x0E]

    ldrh r5, [r4, #0xBA]
    str r5, [sp, #24]              /* DMA0CNT_H */
    ldrh r5, [r4, #0xC6]
    str r5, [sp, #28]              /* DMA1CNT_H */
    ldrh r5, [r4, #0xD2]
    str r5, [sp, #32]              /* DMA2CNT_H */
    ldrh r5, [r4, #0xDE]
    str r5, [sp, #36]              /* DMA3CNT_H */

    /* DMA1/2 enabled + timing=3 are DirectSound FIFO channels.  Their request
     * timers are stopped above, so leaving them enabled is quiescent and keeps
     * the hidden current source intact. */
    strh r10, [r4, #0xBA]
    ldr r5, [sp, #28]
    tst r5, #0x8000
    beq 3f
    and r6, r5, #0x3000
    cmp r6, #0x3000
    beq 4f
3:  strh r10, [r4, #0xC6]
4:
    ldr r5, [sp, #32]
    tst r5, #0x8000
    beq 5f
    and r6, r5, #0x3000
    cmp r6, #0x3000
    beq 6f
5:  strh r10, [r4, #0xD2]
6:  strh r10, [r4, #0xDE]

    /* Execute the exact frozen full-32K worker directly from the payload ROM
     * bank while the dedicated, different M36 bank is modified. */
    bl load_cfg_save_block_r0
    bl load_cfg_save_size_r2
    bl flash_worker_start
    mov r11, r0                     /* durable result across MMIO restore */

    /* Restore exactly what FIX146B proved safe: only DMA channels we disabled,
     * then all timer controls, then IME.  Active SPECIAL DMA1/2 were never
     * touched and therefore keep the hardware's internal source position. */
    ldr r4, =0x04000000
    ldr r5, [sp, #24]
    strh r5, [r4, #0xBA]

    ldr r5, [sp, #28]
    tst r5, #0x8000
    beq 7f
    and r6, r5, #0x3000
    cmp r6, #0x3000
    beq 8f
7:  strh r5, [r4, #0xC6]
8:
    ldr r5, [sp, #32]
    tst r5, #0x8000
    beq 9f
    and r6, r5, #0x3000
    cmp r6, #0x3000
    beq 10f
9:  strh r5, [r4, #0xD2]
10:
    ldr r5, [sp, #36]
    strh r5, [r4, #0xDE]

    add r6, r4, #0x100
    ldr r5, [sp, #8]
    strh r5, [r6, #0x02]
    ldr r5, [sp, #12]
    strh r5, [r6, #0x06]
    ldr r5, [sp, #16]
    strh r5, [r6, #0x0A]
    ldr r5, [sp, #20]
    strh r5, [r6, #0x0E]

    ldr r4, =0x04000208
    ldr r5, [sp, #0]
    strh r5, [r4]
    add sp, sp, #40
    mov r0, r11
    ldmia sp!, {r4-r11, pc}

flush_resume_fail_no_write:
    mov r0, #0
    ldmia sp!, {r4-r11, pc}

    .ltorg
.align 2
flash_worker_size_word:
    .word flash_worker_end - flash_worker_start
.align 2
flash_worker_start:
    /* r0 = save block ROM offset. Return r0=1 success, 0 failure.
     * Self-contained: no calls outside [flash_worker_start, flash_worker_end). */
    stmdb sp!, {r4-r12, lr}
    sub sp, sp, #96             /* 64-byte header + locals */
    mov r12, r0                 /* block offset */

    /* Save WAITCNT and force conservative access. */
    ldr r4, =0x04000204
    ldrh r5, [r4]
    str r5, [sp, #64]
    /* Match the hardware-proven M36 timing used by the NDS flasher.
     * Commands/status use 0x000F; E8 payload halfwords use 0x000B. */
    mov r6, #0x0F
    strh r6, [r4]

    /* FIX165A SRAM32 uses a dual-block, three-slots-per-block full-snapshot
     * journal.  The older EEPROM64K path is retained unchanged for source
     * continuity even though this generic SRAM payload never selects it. */
    ldr r6, =0x00008000
    cmp r2, r6
    beq sram32_journal_worker
    ldr r6, =0x00002000
    cmp r2, r6
    beq journal_worker
    b legacy_worker

sram32_journal_worker:
    /* Hash live SRAM before any NOR command. */
    mov r4, #0x0E000000
    ldr r6, =0x00008000
    ldr r7, =0x811C9DC5
    ldr r8, =0x01000193
100:
    ldrb r9, [r4], #1
    eor r7, r7, r9
    mul r7, r8, r7
    subs r6, r6, #1
    bne 100b
    str r7, [sp, #68]             /* current hash */

    mov r6, #0
    str r6, [sp, #72]             /* best sequence */
    str r6, [sp, #76]             /* best slot pointer */
    str r6, [sp, #80]             /* best block absolute */
    str r6, [sp, #84]             /* first free slot block0 */
    str r6, [sp, #88]             /* first free slot block1 */
    str r6, [sp, #92]             /* action: 1 append, 3 erase+append */

    ldr r11, =0x08000000
    add r11, r11, r12              /* block0 absolute */
    mov r10, #0                    /* block index */
101:
    mov r5, r11                    /* slot pointer */
    mov r9, #0                     /* slot index */
102:
    ldr r6, [r5, #0]
    ldr r7, =0x344A4247            /* GBJ4 */
    cmp r6, r7
    beq 104f
    mvn r7, #0
    cmp r6, r7
    bne 108f
    ldr r6, [r5, #32]
    cmp r6, r7
    bne 108f
    /* Virgin slot: reservation is always written before data, so FF/FF is a
     * safe append target. Record only the first free slot in each block. */
    cmp r10, #0
    bne 103f
    ldr r6, [sp, #84]
    cmp r6, #0
    streq r5, [sp, #84]
    b 108f
103:
    ldr r6, [sp, #88]
    cmp r6, #0
    streq r5, [sp, #88]
    b 108f
104:
    ldr r6, [r5, #4]
    cmp r6, #1
    bne 108f
    ldr r6, [r5, #8]
    ldr r7, =0x00008000
    cmp r6, r7
    bne 108f
    ldr r6, [r5, #32]
    ldr r7, =0x34565352            /* RSV4 */
    cmp r6, r7
    bne 108f
    ldr r6, [r5, #16]              /* seq */
    cmp r6, #0
    beq 108f
    mvn r7, #0
    cmp r6, r7
    beq 108f
    ldr r7, [r5, #20]
    mvn r8, r6
    cmp r7, r8
    bne 108f
    ldr r7, [r5, #24]
    ldr r8, =0x454E4F44            /* DONE */
    cmp r7, r8
    bne 108f
    ldr r7, [r5, #28]
    mvn r8, r8
    cmp r7, r8
    bne 108f
    ldr r7, [sp, #76]
    cmp r7, #0
    beq 105f
    ldr r7, [sp, #72]
    cmp r6, r7
    beq worker_fail_finish         /* duplicate committed sequence: fail closed */
    bls 108f
105:
    str r6, [sp, #72]
    str r5, [sp, #76]
    str r11, [sp, #80]
108:
    ldr r6, =0x00008100
    add r5, r5, r6
    add r9, r9, #1
    cmp r9, #3
    blo 102b
    add r10, r10, #1
    cmp r10, #2
    bhs 109f
    ldr r6, =0x00020000
    add r11, r11, r6
    b 101b
109:
    /* Legacy GBS2 migration: if there is no GBJ4 header yet, preserve the old
     * snapshot as sequence 1. Slot1/2 of the same block are outside GBS2 data
     * and may be appended without erasing the legacy commit. */
    ldr r6, [sp, #76]
    cmp r6, #0
    bne 110f
    ldr r11, =0x08000000
    add r11, r11, r12
    ldr r6, [r11, #0]
    ldr r7, =0x32534247
    cmp r6, r7
    bne 110f
    ldr r6, [r11, #4]
    cmp r6, #1
    bne 110f
    ldr r6, [r11, #8]
    ldr r7, =0x00008000
    cmp r6, r7
    bne 110f
    mov r6, #1
    str r6, [sp, #72]
    str r11, [sp, #80]
110:
    /* Pick a virgin slot without erase whenever possible. Prefer the block
     * containing the newest commit, then the other block. Only when all six
     * slots are consumed is the non-active block erased and reused. */
    ldr r7, =0x08000000
    add r7, r7, r12                /* block0 absolute */
    ldr r6, =0x00020000
    add r8, r7, r6                 /* block1 absolute */
    ldr r6, [sp, #80]              /* best block, or 0 */
    cmp r6, r7
    beq 111f
    cmp r6, r8
    beq 112f
    /* No committed snapshot. */
    ldr r5, [sp, #84]
    cmp r5, #0
    bne 116f
    ldr r5, [sp, #88]
    cmp r5, #0
    bne 117f
    b 114f                         /* erase block0 */
111: /* Newest commit is in block0. */
    ldr r5, [sp, #84]
    cmp r5, #0
    bne 116f
    ldr r5, [sp, #88]
    cmp r5, #0
    bne 117f
    b 115f                         /* erase block1 */
112: /* Newest commit is in block1. */
    ldr r5, [sp, #88]
    cmp r5, #0
    bne 117f
    ldr r5, [sp, #84]
    cmp r5, #0
    bne 116f
    b 114f                         /* erase block0 */
114:
    mov r11, r7
    mov r5, r11
    b 118f
115:
    mov r11, r8
    mov r5, r11
    b 118f
116:
    mov r11, r7                    /* append in block0 */
    mov r6, #1
    str r6, [sp, #92]
    b 119f
117:
    mov r11, r8                    /* append in block1 */
    mov r6, #1
    str r6, [sp, #92]
    b 119f
118:
    mov r6, #3
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
    b 120f
119:
    bl journal_unlock_block
    cmp r0, #0
    beq worker_fail_finish
120:
    /* Allocate next sequence, skipping reserved 0/FFFFFFFF. */
    ldr r6, [sp, #72]
    add r6, r6, #1
    cmp r6, #0
    moveq r6, #1
    mvn r7, #0
    cmp r6, r7
    moveq r6, #1
    str r6, [sp, #72]

    /* Reservation marker FIRST. */
    add r4, sp, #0
    mvn r6, #0
    mov r7, #8
121:
    str r6, [r4], #4
    subs r7, r7, #1
    bne 121b
    ldr r6, =0x34565352            /* RSV4 */
    str r6, [sp, #0]
    add r4, sp, #0
    add r5, r5, #32
    bl journal_program32
    cmp r0, #0
    beq worker_fail_finish
    sub r5, r5, #32                /* restore target slot base */

    /* Program only non-FF 32-byte chunks into the virgin slot.  This keeps the
     * full-snapshot semantics while avoiding pointless E8 cycles for erased
     * portions of sparse SRAM saves. */
    mov r4, #0x0E000000
    add r5, r5, #64
    ldr r10, =0x00008000
122:
    mov r6, #0
    mov r7, #0
123:
    ldrb r8, [r4, r7]
    cmp r8, #0xFF
    bne 124f
    add r7, r7, #1
    cmp r7, #32
    blo 123b
    b 125f
124:
    bl journal_program32
    cmp r0, #0
    beq worker_fail_finish
125:
    add r4, r4, #32
    add r5, r5, #32
    subs r10, r10, #32
    bne 122b

    /* Verify the full 32 KiB target image before the validity header. */
    sub r5, r5, #0x40              /* assembler may materialize literal */
    ldr r6, =0x00008000
    sub r5, r5, r6                 /* back to slot base */
    add r4, r5, #64
    ldr r6, =0x00008000
    ldr r7, =0x811C9DC5
    ldr r8, =0x01000193
126:
    ldrb r9, [r4], #1
    eor r7, r7, r9
    mul r7, r8, r7
    subs r6, r6, #1
    bne 126b
    ldr r6, [sp, #68]
    cmp r7, r6
    bne worker_fail_reset

    /* 32-byte validity header LAST. */
    add r4, sp, #0
    mvn r6, #0
    mov r7, #8
127:
    str r6, [r4], #4
    subs r7, r7, #1
    bne 127b
    ldr r6, =0x344A4247            /* GBJ4 */
    str r6, [sp, #0]
    mov r6, #1
    str r6, [sp, #4]
    ldr r6, =0x00008000
    str r6, [sp, #8]
    ldr r6, [sp, #68]
    str r6, [sp, #12]
    ldr r6, [sp, #72]
    str r6, [sp, #16]
    mvn r7, r6
    str r7, [sp, #20]
    ldr r6, =0x454E4F44            /* DONE */
    str r6, [sp, #24]
    mvn r6, r6
    str r6, [sp, #28]
    add r4, sp, #0
    bl journal_program32
    cmp r0, #0
    beq worker_fail_finish

    /* Header readback. */
    ldr r6, [r5, #0]
    ldr r7, =0x344A4247
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

    ldr r4, =0x04000204
    ldr r5, [sp, #64]
    strh r5, [r4]
    ldr r0, [sp, #92]
    add sp, sp, #96
    ldmia sp!, {r4-r12, lr}
    bx lr

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

    /* Absolute save-block base. */
    ldr r11, =0x08000000
    add r11, r11, r12
    mov r6, #0
    str r6, [sp, #72]           /* best sequence */
    str r6, [sp, #76]           /* best pointer */
    mvn r6, #0
    str r6, [sp, #80]           /* best slot */
    mov r6, #0
    str r6, [sp, #84]           /* first free pointer */
    mvn r6, #0
    str r6, [sp, #88]           /* first free slot */
    mov r10, #0                 /* slot index */

journal_scan_headers:
    cmp r10, #15
    beq journal_scan_done
    ldr r6, =0x00002100
    mul r5, r10, r6
    add r5, r11, r5
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
    mov r6, #0
    str r6, [sp, #84]
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
    /* Prefer a virgin slot. Only when all 15 slots are consumed do we erase
     * the 128 KiB block and wrap to slot 0. */
    ldr r5, [sp, #84]
    cmp r5, #0
    bne journal_have_target
    mov r5, r11
    str r5, [sp, #84]
    mov r6, #0
    str r6, [sp, #88]
    mov r6, #3                  /* erase + append */
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
    bl journal_unlock_block
    cmp r0, #0
    beq worker_fail_finish
journal_target_ready:
    ldr r5, [sp, #84]           /* target slot base */
    ldr r6, [sp, #72]
    add r6, r6, #1
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
    add sp, sp, #96
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
    add sp, sp, #96
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
    add sp, sp, #96
    ldmia sp!, {r4-r12, lr}
    mov r0, #0
    bx lr
    .ltorg
flash_worker_end:

    .ltorg
payload_end:
