.syntax unified
.cpu arm7tdmi
.arm
.section .text,"ax",%progbits
.global m36c_payload_start
.global m36c_dispatch
.global m36c_get_config
.global m36c_run_worker
.global m36c_config_block0
.global m36c_config_block1
.global m36c_config_block2
.global m36c_config_block3
.global m36c_config_block4
.global m36c_config_block5
.global m36c_config_sector_count
.global m36c_config_layout_magic
.global m36c_worker_start
.global m36c_worker_end
.extern m36c_dispatch_c

m36c_payload_start:
    .ascii "GBF167RW"
    .byte 0
    .align 2
m36c_config_block0: .word 0x00940000
m36c_config_block1: .word 0x00B40000
m36c_config_block2: .word 0x00B80000
m36c_config_block3: .word 0x00C20000
m36c_config_block4: .word 0x008A0000
m36c_config_block5: .word 0x00AC0000
m36c_config_sector_count: .word 32
m36c_config_layout_magic: .word 0x314D4336 /* 6CM1 */

m36c_get_config:
    adr r0, m36c_config_block0
    bx lr

/* SuperFW direct stubs enter in ARM state with op in r7 and original args
 * in r0-r2.  Convert to m36c_dispatch_c(op,a0,a1,a2). */
m36c_dispatch:
    stmdb sp!, {r4-r11, lr}
    mov r3, r2
    mov r2, r1
    mov r1, r0
    mov r0, r7
    bl m36c_dispatch_c
    ldmia sp!, {r4-r11, lr}
    bx lr

/* r0 worker op: 2=program 4K no-erase, 3=program 32B no-erase,
 * 4=force erase complete 128K physical block. r1 ROM offset. */
m36c_run_worker:
    stmdb sp!, {r4-r11, lr}
    sub sp, sp, #48
    str r0, [sp, #40]
    str r1, [sp, #44]

    /* FIX167A: execute the self-contained NOR worker directly from this ROM
     * payload bank using the M36 separate-bank RWW property proven by FIX163A.
     * Never relocate worker code below the game stack and never borrow game
     * EWRAM. Fail before any MMIO/NOR mutation if target and worker share a
     * 1 MiB bank or if the worker straddles a bank. */
    adr r4, m36c_worker_start
    adr r5, m36c_worker_size_word
    ldr r6, [r5]
    add r5, r4, r6
    sub r5, r5, #1
    ldr r6, =0x08000000
    sub r7, r4, r6
    sub r8, r5, r6
    mov r7, r7, lsr #20
    mov r8, r8, lsr #20
    cmp r7, r8
    bne m36c_rww_refuse
    ldr r9, [sp, #44]
    mov r9, r9, lsr #20
    cmp r7, r9
    beq m36c_rww_refuse

    /* DEV10 audio-safe RWW discipline. IME still stays OFF for the whole NOR
     * transaction, so no game IRQ handler can run from a conflicting ROM bank.
     * DMA0/3 are stopped as before. Active SPECIAL DMA1/2 are left untouched.
     * The only relaxation versus FIX167A is that a timer actually feeding an
     * active DirectSound FIFO is allowed to keep running; this preserves FIFO
     * request cadence/source position instead of dropping a music channel. */
    ldr r4, =0x04000208
    ldrh r5, [r4]
    str r5, [sp, #0]
    mov r10, #0
    strh r10, [r4]

    ldr r4, =0x04000000
    ldr r5, [r4, #0x80]
    mov r9, r5, lsr #16          /* SOUNDCNT_H */
    add r6, r4, #0x100
    ldrh r5, [r6, #0x02]
    str r5, [sp, #8]
    ldrh r5, [r6, #0x06]
    str r5, [sp, #12]
    ldrh r5, [r6, #0x0A]
    str r5, [sp, #16]
    ldrh r5, [r6, #0x0E]
    str r5, [sp, #20]

    ldrh r5, [r4, #0xBA]
    str r5, [sp, #24]
    ldrh r5, [r4, #0xC6]
    str r5, [sp, #28]
    ldrh r5, [r4, #0xD2]
    str r5, [sp, #32]
    ldrh r5, [r4, #0xDE]
    str r5, [sp, #36]

    /* DEV10 safety refinement. Keep-mask bits are:
     *   bit0 Timer0, bit1 Timer1, bit8 DMA1, bit9 DMA2.
     * A SPECIAL DMA1/2 channel may remain active only when it targets FIFO A/B
     * AND its programmed source is outside Game Pak space (<0x08000000).
     * This preserves the common RAM-fed DirectSound FIFO without allowing a
     * ROM-fed DMA engine to read a bank that may be busy with M36 NOR work.
     * If the source is in Game Pak/save space, fall back to the historical
     * stop/restore behavior for that channel and do not keep its timer alive. */
    mov r8, #0
    ldr r5, [sp, #28]
    tst r5, #0x8000
    beq .Lds_dma2
    and r7, r5, #0x3000
    cmp r7, #0x3000
    bne .Lds_dma2
    ldr r7, [r4, #0xBC]          /* DMA1SAD programmed source */
    ldr r6, =0x08000000
    cmp r7, r6
    bhs .Lds_dma2                /* ROM/Game Pak source: stop safely */
    ldr r7, [r4, #0xC0]          /* DMA1DAD */
    ldr r5, =0x040000A0
    cmp r7, r5
    beq .Lds_fifo_a1
    add r5, r5, #4
    cmp r7, r5
    beq .Lds_fifo_b1
    b .Lds_dma2
.Lds_fifo_a1:
    orr r8, r8, #0x100           /* keep DMA1 */
    tst r9, #0x0400
    orreq r8, r8, #1
    orrne r8, r8, #2
    b .Lds_dma2
.Lds_fifo_b1:
    orr r8, r8, #0x100           /* keep DMA1 */
    tst r9, #0x4000
    orreq r8, r8, #1
    orrne r8, r8, #2

.Lds_dma2:
    ldr r5, [sp, #32]
    tst r5, #0x8000
    beq .Lds_timer_stop
    and r7, r5, #0x3000
    cmp r7, #0x3000
    bne .Lds_timer_stop
    ldr r7, [r4, #0xC8]          /* DMA2SAD programmed source */
    ldr r6, =0x08000000
    cmp r7, r6
    bhs .Lds_timer_stop           /* ROM/Game Pak source: stop safely */
    ldr r7, [r4, #0xCC]          /* DMA2DAD */
    ldr r5, =0x040000A0
    cmp r7, r5
    beq .Lds_fifo_a2
    add r5, r5, #4
    cmp r7, r5
    beq .Lds_fifo_b2
    b .Lds_timer_stop
.Lds_fifo_a2:
    orr r8, r8, #0x200           /* keep DMA2 */
    tst r9, #0x0400
    orreq r8, r8, #1
    orrne r8, r8, #2
    b .Lds_timer_stop
.Lds_fifo_b2:
    orr r8, r8, #0x200           /* keep DMA2 */
    tst r9, #0x4000
    orreq r8, r8, #1
    orrne r8, r8, #2

.Lds_timer_stop:
    str r8, [sp, #4]             /* local keep mask */
    add r6, r4, #0x100           /* timer register base; safety checks reuse r6 */
    tst r8, #1
    strheq r10, [r6, #0x02]
    tst r8, #2
    strheq r10, [r6, #0x06]
    strh r10, [r6, #0x0A]
    strh r10, [r6, #0x0E]

    strh r10, [r4, #0xBA]
    tst r8, #0x100
    strheq r10, [r4, #0xC6]
    tst r8, #0x200
    strheq r10, [r4, #0xD2]
    strh r10, [r4, #0xDE]

    ldr r0, [sp, #40]
    ldr r1, [sp, #44]
    bl m36c_worker_start
    mov r11, r0

    /* Restore only DMA channels that were stopped. RAM-fed FIFO DMA channels
     * selected by the keep mask were never rewritten, preserving their hidden
     * current-source position. */
    ldr r4, =0x04000000
    ldr r8, [sp, #4]
    ldr r5, [sp, #24]
    strh r5, [r4, #0xBA]
    tst r8, #0x100
    ldrheq r5, [sp, #28]
    strheq r5, [r4, #0xC6]
    tst r8, #0x200
    ldrheq r5, [sp, #32]
    strheq r5, [r4, #0xD2]
    ldr r5, [sp, #36]
    strh r5, [r4, #0xDE]

    /* Match the hardware-proven FIX163A/FIX165A restore ordering exactly:
     * restore only DMA channels we stopped, then timer controls, then IME. */
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
    mov r0, r11
    add sp, sp, #48
    ldmia sp!, {r4-r11, pc}

m36c_rww_refuse:
    mov r0, #0xEA
    add sp, sp, #48
    ldmia sp!, {r4-r11, pc}

.align 2
m36c_worker_size_word: .word m36c_worker_end - m36c_worker_start
.align 2
m36c_worker_start:
    stmdb sp!, {r4-r12, lr}
    sub sp, sp, #16
    str r0, [sp, #0]
    str r1, [sp, #4]
    ldr r4, =0x04000204
    ldrh r5, [r4]
    str r5, [sp, #8]
    mov r6, #0x0F
    strh r6, [r4]
    ldr r11, =0x08000000
    add r11, r11, r1
    /* Physical block base for unlock/erase. */
    mov r10, r11, lsr #17
    mov r10, r10, lsl #17
    ldr r0, [sp, #0]
    cmp r0, #4
    beq cw_force_erase
    cmp r0, #2
    moveq r9, #0x1000
    cmp r0, #3
    moveq r9, #32
    cmp r0, #2
    beq cw_precheck
    cmp r0, #3
    beq cw_precheck
    mov r0, #0xE1
    b cw_fail_reset
cw_precheck:
    mov r4, #0x0E000000
    mov r5, r11
    mov r7, r9
    mov r8, #0
cw_precheck_loop:
    ldrb r2, [r4], #1
    ldrb r3, [r5], #1
    cmp r2, r3
    movne r8, #1
    and r6, r3, r2
    cmp r6, r2
    bne cw_need_erase_fail
    subs r7, r7, #1
    bne cw_precheck_loop
    cmp r8, #0
    bne cw_program_unlock
    mov r0, #0
    b cw_success
cw_need_erase_fail:
    mov r0, #0xE2
    b cw_fail_reset
cw_program_unlock:
    mov r6, #0x50
    strh r6, [r10]
    mov r6, #0x60
    strh r6, [r10]
    mov r6, #0xD0
    strh r6, [r10]
    ldr r7, =2000000
cw_unlock_wait:
    ldrh r6, [r10]
    tst r6, #0x80
    bne cw_unlock_ready
    subs r7, r7, #1
    bne cw_unlock_wait
    mov r0, #0xE3
    b cw_fail_reset
cw_unlock_ready:
    tst r6, #0x3A
    bne cw_unlock_status_fail
    mov r6, #0xFF
    strh r6, [r10]
    mov r4, #0x0E000000
    mov r5, r11
    mov r7, r9
cw_program_loop:
    cmp r7, #0
    beq cw_verify
    /* Skip a 32-byte chunk that already matches. */
    mov r8, #0
cw_chunk_cmp:
    ldrb r2, [r4, r8]
    ldrb r3, [r5, r8]
    cmp r2, r3
    bne cw_chunk_do
    add r8, r8, #1
    cmp r8, #32
    blo cw_chunk_cmp
    add r4, r4, #32
    add r5, r5, #32
    subs r7, r7, #32
    b cw_program_loop
cw_chunk_do:
    mov r6, #0x50
    strh r6, [r5]
    mov r6, #0xE8
    strh r6, [r5]
    ldr r8, =1000000
cw_e8_wait:
    ldrh r6, [r5]
    tst r6, #0x80
    bne cw_e8_ready
    subs r8, r8, #1
    bne cw_e8_wait
    mov r0, #0xE5
    b cw_fail_reset_at_r5
cw_e8_ready:
    tst r6, #0x3A
    bne cw_e8_status_fail
    mov r6, #15
    strh r6, [r5]
    ldr r6, =0x04000204
    mov r8, #0x0B
    strh r8, [r6]
    mov r8, #0
cw_payload_loop:
    ldrb r2, [r4, r8]
    add r3, r8, #1
    ldrb r3, [r4, r3]
    orr r2, r2, r3, lsl #8
    strh r2, [r5, r8]
    add r8, r8, #2
    cmp r8, #32
    blo cw_payload_loop
    mov r8, #64
cw_settle:
    subs r8, r8, #1
    bne cw_settle
    ldr r6, =0x04000204
    mov r8, #0x0F
    strh r8, [r6]
    mov r6, #0xD0
    strh r6, [r5]
    ldr r8, =1000000
cw_d0_wait:
    ldrh r6, [r5]
    tst r6, #0x80
    bne cw_d0_ready
    subs r8, r8, #1
    bne cw_d0_wait
    mov r0, #0xE7
    b cw_fail_reset_at_r5
cw_d0_ready:
    tst r6, #0x3A
    bne cw_d0_status_fail
    mov r6, #0xFF
    strh r6, [r5]
    add r4, r4, #32
    add r5, r5, #32
    subs r7, r7, #32
    b cw_program_loop
cw_verify:
    mov r4, #0x0E000000
    mov r5, r11
    mov r7, r9
cw_verify_loop:
    ldrb r2, [r4], #1
    ldrb r3, [r5], #1
    cmp r2, r3
    bne cw_verify_fail
    subs r7, r7, #1
    bne cw_verify_loop
    mov r0, #0
    b cw_success

cw_force_erase:
    mov r6, #0x50
    strh r6, [r10]
    mov r6, #0x60
    strh r6, [r10]
    mov r6, #0xD0
    strh r6, [r10]
    ldr r7, =2000000
cw_eunlock_wait:
    ldrh r6, [r10]
    tst r6, #0x80
    bne cw_eunlock_ready
    subs r7, r7, #1
    bne cw_eunlock_wait
    mov r0, #0xD1
    b cw_fail_reset
cw_eunlock_ready:
    tst r6, #0x3A
    bne cw_unlock_status_fail
    mov r6, #0xFF
    strh r6, [r10]
    mov r6, #0x50
    strh r6, [r10]
    mov r6, #0x20
    strh r6, [r10]
    mov r6, #0xD0
    strh r6, [r10]
    ldr r7, =12000000
cw_erase_wait:
    ldrh r6, [r10]
    tst r6, #0x80
    bne cw_erase_ready
    subs r7, r7, #1
    bne cw_erase_wait
    mov r0, #0xD3
    b cw_fail_reset
cw_erase_ready:
    tst r6, #0x3A
    bne cw_erase_status_fail
    mov r6, #0xFF
    strh r6, [r10]
    /* Full 128 KiB erased-state verification. */
    mov r5, r10
    mov r7, #0x20000
cw_erase_verify:
    ldrb r6, [r5], #1
    cmp r6, #0xFF
    bne cw_erase_verify_fail
    subs r7, r7, #1
    bne cw_erase_verify
    mov r0, #0
    b cw_success

cw_unlock_status_fail: mov r0, #0xE4; b cw_fail_clear
cw_e8_status_fail: mov r0, #0xE6; b cw_fail_clear_at_r5
cw_d0_status_fail: mov r0, #0xE8; b cw_fail_clear_at_r5
cw_verify_fail: mov r0, #0xE9; b cw_fail_reset
cw_erase_status_fail: mov r0, #0xD4; b cw_fail_clear
cw_erase_verify_fail: mov r0, #0xD5; b cw_fail_reset
cw_fail_clear_at_r5:
    mov r6, #0x50
    strh r6, [r5]
    mov r6, #0xFF
    strh r6, [r5]
    b cw_finish
cw_fail_reset_at_r5:
    mov r6, #0xFF
    strh r6, [r5]
    b cw_finish
cw_fail_clear:
    mov r6, #0x50
    strh r6, [r10]
cw_fail_reset:
    mov r6, #0xFF
    strh r6, [r10]
    b cw_finish
cw_success:
    mov r6, #0xFF
    strh r6, [r10]
cw_finish:
    ldr r4, =0x04000204
    ldr r5, [sp, #8]
    strh r5, [r4]
    add sp, sp, #16
    ldmia sp!, {r4-r12, pc}
m36c_worker_end:
.align 2
