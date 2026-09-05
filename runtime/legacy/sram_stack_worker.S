.syntax unified
.cpu arm7tdmi
.thumb

.section .sram_stack_worker,"ax",%progbits
.align 2
.global sramStackProgramChunk
.global sramStackEraseBlock
.global sramStackProgramChunkEnd
.global sramStackEraseBlockEnd
.type sramStackProgramChunk, %function
.type sramStackEraseBlock, %function

/* u32 sramStackProgramChunk(u32 target, const u8 *source, u32 bytes)
 *
 * Position-independent, no literal pool, no external calls.  The caller only
 * uses aligned 4 KiB chunks from internal RAM.  Target-local 50/FF/40/DATA is
 * the Phase 0.13 hardware-proven M6 program form. */
sramStackProgramChunk:
    push {r4-r7, lr}
    adds r4, r0, #0
    adds r5, r1, #0
    adds r6, r2, #0
    cmp r5, #0
    beq .Lprog_bad
    cmp r6, #0
    beq .Lprog_bad
    adds r3, r4, #0
    orrs r3, r5
    orrs r3, r6
    movs r2, #1
    tst r3, r2
    bne .Lprog_bad

.Lprog_word:
    ldrh r7, [r5]
    ldrh r3, [r4]
    cmp r3, r7
    beq .Lprog_next
    adds r2, r3, #0
    ands r2, r7
    cmp r2, r7
    bne .Lprog_need_erase

    movs r2, #3
.Lprog_retry:
    movs r3, #0x50
    strh r3, [r4]
    movs r3, #0xFF
    strh r3, [r4]
    movs r3, #0x40
    strh r3, [r4]
    strh r7, [r4]

    movs r3, #2
    lsls r3, r3, #8       /* 0x200 bounded status polls */
.Lprog_poll:
    ldrh r0, [r4]
    cmp r0, #0x80
    beq .Lprog_poll_done
    subs r3, r3, #1
    bne .Lprog_poll
.Lprog_poll_done:
    movs r3, #0x50
    strh r3, [r4]
    movs r3, #0xFF
    strh r3, [r4]
    movs r3, #64
.Lprog_settle:
    subs r3, r3, #1
    bne .Lprog_settle
    ldrh r3, [r4]
    cmp r3, r7
    beq .Lprog_next
    subs r2, r2, #1
    bne .Lprog_retry
    movs r0, #0xE7
    lsls r0, r0, #8
    adds r0, #3           /* E703 */
    pop {r4-r7, pc}

.Lprog_next:
    adds r4, #2
    adds r5, #2
    subs r6, #2
    bne .Lprog_word
    movs r0, #0
    pop {r4-r7, pc}

.Lprog_bad:
    movs r0, #0xE7
    lsls r0, r0, #8
    adds r0, #1           /* E701 */
    pop {r4-r7, pc}
.Lprog_need_erase:
    movs r0, #0xE7
    lsls r0, r0, #8
    adds r0, #2           /* E702 */
    pop {r4-r7, pc}
sramStackProgramChunkEnd:
.size sramStackProgramChunk, .-sramStackProgramChunk

/* u32 sramStackEraseBlock(u32 blockAddress)
 * Position-independent target-local Intel erase sequence. */
sramStackEraseBlock:
    push {r4-r7, lr}
    adds r4, r0, #0
    movs r3, #0x50
    strh r3, [r4]
    movs r3, #0xFF
    strh r3, [r4]
    movs r3, #0x60
    strh r3, [r4]
    movs r3, #0xD0
    strh r3, [r4]
    movs r3, #0x20
    strh r3, [r4]
    movs r3, #0xD0
    strh r3, [r4]
    movs r3, #0x70
    strh r3, [r4]

    movs r6, #100
.Lerase_poll_outer:
    movs r7, #1
    lsls r7, r7, #18      /* delay 0x40000 */
.Lerase_delay:
    subs r7, r7, #1
    bne .Lerase_delay
    ldrh r5, [r4]
    cmp r5, #0x80
    beq .Lerase_ok
    movs r3, #0x80
    tst r5, r3
    bne .Lerase_status_error
    subs r6, r6, #1
    bne .Lerase_poll_outer

    movs r0, #0xE7
    lsls r0, r0, #8
    adds r0, #0x11        /* E711 */
    b .Lerase_reset
.Lerase_status_error:
    movs r0, #0xE7
    lsls r0, r0, #8
    adds r0, #0x13        /* E713 */
    b .Lerase_reset
.Lerase_ok:
    movs r0, #0
.Lerase_reset:
    movs r3, #0x50
    strh r3, [r4]
    movs r3, #0xFF
    strh r3, [r4]
    pop {r4-r7, pc}
sramStackEraseBlockEnd:
.size sramStackEraseBlock, .-sramStackEraseBlock
