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

.align 2
.global sramStackD137ProgramChunk
.global sramStackD137ProgramChunkEnd
.global sramStackD137EraseBlock
.global sramStackD137EraseBlockEnd
.global sramStackAmdProgramChunk
.global sramStackAmdProgramChunkEnd
.type sramStackD137ProgramChunk, %function
.type sramStackD137EraseBlock, %function
.type sramStackAmdProgramChunk, %function

/* M6MGD137 target-relative 70/ready -> 40/data program primitive. */
sramStackD137ProgramChunk:
    push {r4-r7, lr}
    adds r4, r0, #0
    adds r5, r1, #0
    adds r6, r2, #0
    cmp r5, #0
    beq .Ld137_prog_bad
    cmp r6, #0
    beq .Ld137_prog_bad
    adds r3, r4, #0
    orrs r3, r5
    orrs r3, r6
    movs r2, #1
    tst r3, r2
    bne .Ld137_prog_bad
.Ld137_prog_word:
    ldrh r7, [r5]
    ldrh r3, [r4]
    cmp r3, r7
    beq .Ld137_prog_next
    adds r2, r3, #0
    ands r2, r7
    cmp r2, r7
    bne .Ld137_prog_need_erase

    movs r3, #0x70
    strh r3, [r4]
    movs r2, #2
    lsls r2, r2, #16      /* 0x20000 ready polls */
.Ld137_pre_poll:
    ldrh r3, [r4]
    movs r0, #0x80
    tst r3, r0
    bne .Ld137_pre_ready
    subs r2, r2, #1
    bne .Ld137_pre_poll
    b .Ld137_prog_fail_reset
.Ld137_pre_ready:
    movs r0, #0x18
    tst r3, r0
    bne .Ld137_prog_fail_reset

    movs r3, #0x40
    strh r3, [r4]
    strh r7, [r4]
    movs r2, #2
    lsls r2, r2, #20      /* 0x200000 completion polls */
.Ld137_data_poll:
    ldrh r3, [r4]
    movs r0, #0x80
    tst r3, r0
    bne .Ld137_data_ready
    subs r2, r2, #1
    bne .Ld137_data_poll
    b .Ld137_prog_fail_reset
.Ld137_data_ready:
    movs r0, #0x18
    tst r3, r0
    bne .Ld137_prog_fail_reset
    movs r3, #0x50
    strh r3, [r4]
    movs r3, #0xFF
    strh r3, [r4]
    movs r3, #64
.Ld137_prog_settle:
    subs r3, r3, #1
    bne .Ld137_prog_settle
    ldrh r3, [r4]
    cmp r3, r7
    bne .Ld137_prog_fail
.Ld137_prog_next:
    adds r4, #2
    adds r5, #2
    subs r6, #2
    bne .Ld137_prog_word
    movs r0, #0
    pop {r4-r7, pc}
.Ld137_prog_fail_reset:
    movs r3, #0x50
    strh r3, [r4]
    movs r3, #0xFF
    strh r3, [r4]
.Ld137_prog_fail:
    movs r0, #0xE7
    lsls r0, r0, #8
    adds r0, #0x43        /* E743 */
    pop {r4-r7, pc}
.Ld137_prog_bad:
    movs r0, #0xE7
    lsls r0, r0, #8
    adds r0, #0x41        /* E741 */
    pop {r4-r7, pc}
.Ld137_prog_need_erase:
    movs r0, #0xE7
    lsls r0, r0, #8
    adds r0, #0x42        /* E742 */
    pop {r4-r7, pc}
sramStackD137ProgramChunkEnd:
.size sramStackD137ProgramChunk, .-sramStackD137ProgramChunk

/* M6MGD137 target-relative 60/D0/20/D0 sector erase primitive. */
sramStackD137EraseBlock:
    push {r4-r7, lr}
    adds r4, r0, #0
    movs r3, #0x60
    strh r3, [r4]
    movs r3, #0xD0
    strh r3, [r4]
    movs r3, #0x20
    strh r3, [r4]
    movs r3, #0xD0
    strh r3, [r4]
    movs r6, #12
    lsls r6, r6, #20      /* 12,582,912 bounded polls */
.Ld137_erase_poll:
    ldrh r5, [r4]
    movs r3, #0x80
    tst r5, r3
    bne .Ld137_erase_ready
    subs r6, r6, #1
    bne .Ld137_erase_poll
    movs r0, #0xE7
    lsls r0, r0, #8
    adds r0, #0x51        /* E751 */
    b .Ld137_erase_reset
.Ld137_erase_ready:
    movs r3, #0x38
    tst r5, r3
    beq .Ld137_erase_ok
    movs r0, #0xE7
    lsls r0, r0, #8
    adds r0, #0x52        /* E752 */
    b .Ld137_erase_reset
.Ld137_erase_ok:
    movs r0, #0
.Ld137_erase_reset:
    movs r3, #0x50
    strh r3, [r4]
    movs r3, #0xFF
    strh r3, [r4]
    pop {r4-r7, pc}
sramStackD137EraseBlockEnd:
.size sramStackD137EraseBlock, .-sramStackD137EraseBlock

/* MX26L6420 AMD unlock-cycle AA/55/A0 program primitive. No erase routine. */
sramStackAmdProgramChunk:
    push {r4-r7, lr}
    adds r4, r0, #0
    adds r5, r1, #0
    adds r6, r2, #0
    cmp r5, #0
    beq .Lamd_prog_bad
    cmp r6, #0
    beq .Lamd_prog_bad
    adds r3, r4, #0
    orrs r3, r5
    orrs r3, r6
    movs r2, #1
    tst r3, r2
    bne .Lamd_prog_bad
.Lamd_prog_word:
    ldrh r7, [r5]
    ldrh r3, [r4]
    cmp r3, r7
    beq .Lamd_prog_next
    adds r2, r3, #0
    ands r2, r7
    cmp r2, r7
    bne .Lamd_prog_need_erase

    /* r0 = 08000AAA, r1 = 08000554 */
    movs r2, #8
    lsls r2, r2, #24
    movs r0, #0xAA
    lsls r0, r0, #4
    adds r0, #0x0A
    adds r0, r0, r2
    movs r1, #0x55
    lsls r1, r1, #4
    adds r1, #4
    adds r1, r1, r2
    movs r3, #0xAA
    strh r3, [r0]
    movs r3, #0x55
    strh r3, [r1]
    movs r3, #0xA0
    strh r3, [r0]
    strh r7, [r4]

    movs r0, #2
    lsls r0, r0, #20      /* DQ6 bounded toggle polls */
.Lamd_poll:
    ldrh r1, [r4]
    ldrh r2, [r4]
    eors r1, r2
    movs r3, #0x40
    tst r1, r3
    beq .Lamd_poll_done
    movs r3, #0x20
    tst r2, r3
    beq .Lamd_poll_continue
    ldrh r1, [r4]
    ldrh r2, [r4]
    eors r1, r2
    movs r3, #0x40
    tst r1, r3
    beq .Lamd_poll_done
    b .Lamd_prog_fail_reset
.Lamd_poll_continue:
    subs r0, r0, #1
    bne .Lamd_poll
    b .Lamd_prog_fail_reset
.Lamd_poll_done:
    /* F0 reset through unlock1, then exact read-array verification. */
    movs r2, #8
    lsls r2, r2, #24
    movs r0, #0xAA
    lsls r0, r0, #4
    adds r0, #0x0A
    adds r0, r0, r2
    movs r3, #0xF0
    strh r3, [r0]
    movs r3, #64
.Lamd_settle:
    subs r3, r3, #1
    bne .Lamd_settle
    ldrh r3, [r4]
    cmp r3, r7
    bne .Lamd_prog_fail
.Lamd_prog_next:
    adds r4, #2
    adds r5, #2
    subs r6, #2
    bne .Lamd_prog_word
    movs r0, #0
    pop {r4-r7, pc}
.Lamd_prog_fail_reset:
    movs r2, #8
    lsls r2, r2, #24
    movs r0, #0xAA
    lsls r0, r0, #4
    adds r0, #0x0A
    adds r0, r0, r2
    movs r3, #0xF0
    strh r3, [r0]
.Lamd_prog_fail:
    movs r0, #0xE7
    lsls r0, r0, #8
    adds r0, #0xA3        /* E7A3 */
    pop {r4-r7, pc}
.Lamd_prog_bad:
    movs r0, #0xE7
    lsls r0, r0, #8
    adds r0, #0xA1        /* E7A1 */
    pop {r4-r7, pc}
.Lamd_prog_need_erase:
    movs r0, #0xE7
    lsls r0, r0, #8
    adds r0, #0xA2        /* E7A2 */
    pop {r4-r7, pc}
sramStackAmdProgramChunkEnd:
.size sramStackAmdProgramChunk, .-sramStackAmdProgramChunk
