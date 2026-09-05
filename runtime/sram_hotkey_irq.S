.syntax unified
.cpu arm7tdmi
.arm

.section .text.runtime.arm,"ax",%progbits
.align 2
.global gbashSramBootEntry
.global gbashSramHotkeyIrqEntry
.global gbashSramOwnedShadowInitIrqEntry
.global gbashSramOwnedShadowHotkeyIrqEntry
.global gbashSramOwnedShadowHotkeyLatchedIrqEntry
.global gbashSramDummyIrq
.global gbashSramChainSlotAddressWord
.global gbashSramFallbackDispatcherWord
.type gbashSramBootEntry, %function
.type gbashSramHotkeyIrqEntry, %function
.type gbashSramOwnedShadowInitIrqEntry, %function
.type gbashSramOwnedShadowHotkeyIrqEntry, %function
.type gbashSramOwnedShadowHotkeyLatchedIrqEntry, %function
.type gbashSramDummyIrq, %function
.type gbashSramChainSlotAddressWord, %object
.type gbashSramFallbackDispatcherWord, %object
.extern gbashSramBootInit
.extern gbashSramHotkeyCommit
.extern gbashOwnedSramHotkeyCommit
.extern gbashSramIrqChainTarget
.extern gbashRunM36RwwProgram
.extern gbashRunM36RwwErase
.extern sramStackProgramChunk
.extern sramStackProgramChunkEnd
.extern sramStackEraseBlock
.extern sramStackEraseBlockEnd
.extern sramStackD137ProgramChunk
.extern sramStackD137ProgramChunkEnd
.extern sramStackD137EraseBlock
.extern sramStackD137EraseBlockEnd
.extern sramStackAmdProgramChunk
.extern sramStackAmdProgramChunkEnd
.extern __ram_worker_start
.extern __ram_worker_end
.extern ramProgramBytes
.extern ramEraseBlock

.equ BIOS_USER_IRQ_VECTOR,  0x03007FFC
.equ SRAM_HOTKEY_LATCH,       0x0203BFE8
.equ KEYINPUT_ADDRESS,       0x04000130
.equ HOTKEY_RAW,             0x00F9   /* L+R+SELECT+B, exact active-low */

/* SRAM-only boot trampoline.
 *
 * The host replaces the ROM's initial ARM B with a branch here. BootInit does
 * one ROM-array -> EWRAM shadow restore, installs our IRQ vector, and returns
 * the original ROM entry target. No NOR command is issued during boot. */
gbashSramBootEntry:
    stmdb sp!, {r0-r3, lr}
    adr lr, .Lboot_return
    ldr r3, =gbashSramBootInit
    orr r3, r3, #1
    bx r3
.Lboot_return:
    mov r12, r0
    ldmia sp!, {r0-r3, lr}
    bx r12
.size gbashSramBootEntry, .-gbashSramBootEntry

/* Kept as an exported no-op for runtime-blob compatibility. The v10 SRAM
 * path no longer stores a chain target in BIOS-reserved IWRAM. */
gbashSramDummyIrq:
    bx lr
.size gbashSramDummyIrq, .-gbashSramDummyIrq

/* Transparent SRAM hotkey IRQ interposer.
 *
 * The Nintendo crt0 vector normally points directly at Nintendo's ARM IRQ
 * dispatcher.  That dispatcher saves the interrupted r0-r3/lr itself.  Any
 * code placed in front of it therefore MUST preserve the interrupted scratch
 * registers before touching KEYINPUT or runtime state.  A previous interposer
 * clobbered r0/r2/r3 before tail-chaining, which could corrupt arbitrary game
 * code on every VBlank/IRQ.
 *
 * All entries below use a 7-word IRQ-stack frame:
 *   [r0,r1,r2,r3,r12,lr,chainTarget]
 * The final LDM restores the exact interrupted state and loads PC atomically
 * from chainTarget, so no register needs to be clobbered after restoration.
 */

.macro GBASH_IRQ_FRAME_ENTER
    sub sp, sp, #28
    stmia sp, {r0-r3, r12, lr}
.endm

/* Finish an interposed IRQ by restoring the interrupted scratch state and
 * branching to the game's original Nintendo dispatcher/private chain target.
 * This macro assumes GBASH_IRQ_FRAME_ENTER has been executed. */
.Lirq_chain_common:
    /* Obtain the structurally validated Nintendo dispatcher from immutable
     * RuntimeConfig. Never use 03007FF4: Nintendo reserves that area for BIOS
     * activity and SWIs are allowed to overwrite it. The interrupted scratch
     * frame is already complete before this helper call. */
    adr lr, .Lirq_chain_target_return
    ldr r3, =gbashSramIrqChainTarget
    orr r3, r3, #1
    bx r3
.Lirq_chain_target_return:
    str r0, [sp, #24]
    ldmia sp!, {r0-r3, r12, lr, pc}

/* Game-owned SRAM IRQ state machine.
 *
 * This deliberately restores the hardware-proven v0.16 IRQ ABI: the small
 * interposer is allowed to use r0-r3 as scratch and tail-chains directly to
 * the game's Nintendo IRQ target.  The only unsafe part of v0.16 was storing
 * that dynamic target at BIOS-reserved 03007FF4.  v0.29 stores it instead in
 * an analyzer-proven EWRAM word whose address is patched into the two words
 * below.  No C helper and no synthetic full IRQ register frame sits in front
 * of the game's dispatcher on this path.
 */
.align 2
gbashSramChainSlotAddressWord:
    .word 0
gbashSramFallbackDispatcherWord:
    .word 0

/* BIOS_USER_IRQ_VECTOR itself remains INIT -> ARMED -> LATCHED. */
gbashSramOwnedShadowInitIrqEntry:
    b .Lowned_chain
.size gbashSramOwnedShadowInitIrqEntry, .-gbashSramOwnedShadowInitIrqEntry

gbashSramOwnedShadowHotkeyIrqEntry:
    ldr r2, =KEYINPUT_ADDRESS
    ldrh r3, [r2]
    mov r3, r3, lsl #22
    mov r3, r3, lsr #22
    cmp r3, #HOTKEY_RAW
    bne .Lowned_chain

    ldr r2, =BIOS_USER_IRQ_VECTOR
    ldr r3, =gbashSramOwnedShadowHotkeyLatchedIrqEntry
    str r3, [r2]

    mov r3, #0x9F
    msr cpsr_c, r3
    stmdb sp!, {lr}
    adr lr, .Lowned_commit_return
    ldr r3, =gbashOwnedSramHotkeyCommit
    orr r3, r3, #1
    bx r3
.Lowned_commit_return:
    ldmia sp!, {lr}
    mov r3, #0x92
    msr cpsr_c, r3
    b .Lowned_chain
.size gbashSramOwnedShadowHotkeyIrqEntry, .-gbashSramOwnedShadowHotkeyIrqEntry

gbashSramOwnedShadowHotkeyLatchedIrqEntry:
    ldr r2, =KEYINPUT_ADDRESS
    ldrh r3, [r2]
    mov r3, r3, lsl #22
    mov r3, r3, lsr #22
    cmp r3, #HOTKEY_RAW
    beq .Lowned_chain
    ldr r2, =BIOS_USER_IRQ_VECTOR
    ldr r3, =gbashSramOwnedShadowHotkeyIrqEntry
    str r3, [r2]

.Lowned_chain:
    /* Load the dynamic target from the analyzer-proven EWRAM chain slot.
     * EWRAM may be cleared by a title/soft reset; zero falls back to the
     * immutable structurally validated Nintendo dispatcher until the game
     * installs its next dynamic target. */
    ldr r1, =gbashSramChainSlotAddressWord
    ldr r1, [r1]
    ldr r3, [r1]
    cmp r3, #0
    bne .Lowned_have_target
    ldr r1, =gbashSramFallbackDispatcherWord
    ldr r3, [r1]
.Lowned_have_target:
    ldr r0, =0x04000000
    bx r3
.size gbashSramOwnedShadowHotkeyLatchedIrqEntry, .-gbashSramOwnedShadowHotkeyLatchedIrqEntry

/* Legacy/private-shadow SRAM route.  It now uses the same transparent IRQ
 * frame as the game-owned route.  The established hotkey transaction itself
 * is otherwise unchanged. */
gbashSramHotkeyIrqEntry:
    GBASH_IRQ_FRAME_ENTER
    ldr r2, =KEYINPUT_ADDRESS
    ldrh r3, [r2]
    mov r3, r3, lsl #22
    mov r3, r3, lsr #22
    cmp r3, #HOTKEY_RAW
    bne .Llegacy_rearm

    ldr r2, =SRAM_HOTKEY_LATCH
    ldrb r3, [r2]
    cmp r3, #0
    bne .Lirq_chain_common
    mov r3, #1
    strb r3, [r2]

    mov r3, #0x9F
    msr cpsr_c, r3
    /* Keep the established System-stack save around the legacy transaction.
     * The new IRQ-stack frame additionally guarantees transparent chaining. */
    stmdb sp!, {r0-r12, lr}
    adr lr, .Llegacy_commit_return
    ldr r3, =gbashSramHotkeyCommit
    orr r3, r3, #1
    bx r3
.Llegacy_commit_return:
    ldmia sp!, {r0-r12, lr}
    mov r3, #0x92
    msr cpsr_c, r3
    b .Lirq_chain_common

.Llegacy_rearm:
    ldr r2, =SRAM_HOTKEY_LATCH
    mov r3, #0
    strb r3, [r2]
    b .Lirq_chain_common
.size gbashSramHotkeyIrqEntry, .-gbashSramHotkeyIrqEntry

/* M36 save transactions execute the self-contained E8 worker directly from
 * the runtime ROM bank. Host placement guarantees every mutable M36 save
 * target is in a different 1 MiB RWW bank. The worker returns the target bank
 * to read-array mode before control returns here. This deliberately avoids the
 * old below-System-SP full-worker relocation that was proven to persist data
 * but freeze on same-game resume. */

/* Full-size SRAM stack-execution wrappers.
 *
 * These wrappers solve the v0.16 stack-audit issue without reserving any
 * permanent EWRAM/IWRAM address.  Only one NOR primitive is copied at a time:
 * program (still 0x86 bytes, rounded to a 0x88-byte wrapper allocation) or erase (still 0x56 bytes, rounded to 0x58 bytes). The code
 * buffer is the wrapper's own descending-stack allocation, and the copied
 * worker pushes below that allocation while it executes.  C++ never owns a
 * 0x100-byte executable local anymore.
 */
.thumb
.align 2
.global gbashRunSramStackProgram
.global gbashRunSramStackErase
.type gbashRunSramStackProgram, %function
.type gbashRunSramStackErase, %function

/* u32 gbashRunSramStackProgram(u32 target, const u8 *source, u32 bytes, u32 programSelector) */
gbashRunSramStackProgram:
    /* Preserve every callee-saved register before backend dispatch. */
    push {r4-r7, lr}
    movs r4, #0xE8
    cmp r3, r4
    beq .Lsel_m36_program
    movs r4, #0x40
    cmp r3, r4
    beq .Lsel_m6_program
    movs r4, #0xA0
    cmp r3, r4
    beq .Lsel_amd_program
    movs r4, #1
    lsls r4, r4, #8
    adds r4, #0x40        /* internal D137-relative selector 0x0140 */
    cmp r3, r4
    beq .Lsel_d137_program
    movs r0, #0xE7
    lsls r0, r0, #8
    adds r0, #0x20
    pop {r4-r7, pc}
.Lsel_m36_program:
    movs r3, #0xE8
    bl gbashRunM36RwwProgram
    pop {r4-r7, pc}
.Lsel_m6_program:
    b .Lrun_m6_program_worker
.Lsel_d137_program:
    b .Lrun_d137_program_worker
.Lsel_amd_program:
    b .Lrun_amd_program_worker

.Lrun_m6_program_worker:
    /* Byte-exact hardware-proven v0.16 M6/M6M primitive remains untouched. */
    adds r5, r0, #0
    adds r6, r1, #0
    adds r7, r2, #0
    sub sp, #0x88
    add r4, sp, #0
    ldr r0, =sramStackProgramChunk
    movs r3, #1
    bics r0, r3
    ldr r1, =sramStackProgramChunkEnd
    movs r3, #1
    bics r1, r3
    adds r2, r4, #0
.Lcopy_program_worker:
    cmp r0, r1
    bhs .Lprogram_worker_copied
    ldrh r3, [r0]
    strh r3, [r2]
    adds r0, #2
    adds r2, #2
    b .Lcopy_program_worker
.Lprogram_worker_copied:
    adds r0, r5, #0
    adds r1, r6, #0
    adds r2, r7, #0
    ldr r3, =.Lprogram_worker_return + 1
    mov lr, r3
    adds r3, r4, #1
    bx r3
.Lprogram_worker_return:
    add sp, #0x88
    pop {r4-r7, pc}

.Lrun_d137_program_worker:
    adds r5, r0, #0
    adds r6, r1, #0
    adds r7, r2, #0
    sub sp, #0x100
    add r4, sp, #0
    ldr r0, =sramStackD137ProgramChunk
    movs r3, #1
    bics r0, r3
    ldr r1, =sramStackD137ProgramChunkEnd
    movs r3, #1
    bics r1, r3
    adds r2, r4, #0
.Lcopy_d137_program_worker:
    cmp r0, r1
    bhs .Ld137_program_worker_copied
    ldrh r3, [r0]
    strh r3, [r2]
    adds r0, #2
    adds r2, #2
    b .Lcopy_d137_program_worker
.Ld137_program_worker_copied:
    adds r0, r5, #0
    adds r1, r6, #0
    adds r2, r7, #0
    ldr r3, =.Ld137_program_worker_return + 1
    mov lr, r3
    adds r3, r4, #1
    bx r3
.Ld137_program_worker_return:
    add sp, #0x100
    pop {r4-r7, pc}

.Lrun_amd_program_worker:
    adds r5, r0, #0
    adds r6, r1, #0
    adds r7, r2, #0
    sub sp, #0x100
    add r4, sp, #0
    ldr r0, =sramStackAmdProgramChunk
    movs r3, #1
    bics r0, r3
    ldr r1, =sramStackAmdProgramChunkEnd
    movs r3, #1
    bics r1, r3
    adds r2, r4, #0
.Lcopy_amd_program_worker:
    cmp r0, r1
    bhs .Lamd_program_worker_copied
    ldrh r3, [r0]
    strh r3, [r2]
    adds r0, #2
    adds r2, #2
    b .Lcopy_amd_program_worker
.Lamd_program_worker_copied:
    adds r0, r5, #0
    adds r1, r6, #0
    adds r2, r7, #0
    ldr r3, =.Lamd_program_worker_return + 1
    mov lr, r3
    adds r3, r4, #1
    bx r3
.Lamd_program_worker_return:
    add sp, #0x100
    pop {r4-r7, pc}
.size gbashRunSramStackProgram, .-gbashRunSramStackProgram

/* u32 gbashRunSramStackErase(u32 blockAddress, u32 programSelector) */
gbashRunSramStackErase:
    push {r4-r5, lr}
    movs r4, #0xE8
    cmp r1, r4
    beq .Lsel_m36_erase
    movs r4, #0x40
    cmp r1, r4
    beq .Lsel_m6_erase
    movs r4, #0xA0
    cmp r1, r4
    beq .Lsel_no_erase
    movs r4, #1
    lsls r4, r4, #8
    adds r4, #0x40
    cmp r1, r4
    beq .Lsel_d137_erase
    b .Lsel_no_erase
.Lsel_m36_erase:
    movs r1, #0xE8
    bl gbashRunM36RwwErase
    pop {r4-r5, pc}
.Lsel_m6_erase:
    b .Lrun_m6_erase_worker
.Lsel_d137_erase:
    b .Lrun_d137_erase_worker
.Lsel_no_erase:
    movs r0, #0xE7
    lsls r0, r0, #8
    adds r0, #0x21
    pop {r4-r5, pc}

.Lrun_m6_erase_worker:
    adds r5, r0, #0
    sub sp, #0x58
    add r4, sp, #0
    ldr r0, =sramStackEraseBlock
    movs r3, #1
    bics r0, r3
    ldr r1, =sramStackEraseBlockEnd
    movs r3, #1
    bics r1, r3
    adds r2, r4, #0
.Lcopy_erase_worker:
    cmp r0, r1
    bhs .Lerase_worker_copied
    ldrh r3, [r0]
    strh r3, [r2]
    adds r0, #2
    adds r2, #2
    b .Lcopy_erase_worker
.Lerase_worker_copied:
    adds r0, r5, #0
    ldr r3, =.Lerase_worker_return + 1
    mov lr, r3
    adds r3, r4, #1
    bx r3
.Lerase_worker_return:
    add sp, #0x58
    pop {r4-r5, pc}

.Lrun_d137_erase_worker:
    adds r5, r0, #0
    sub sp, #0x100
    add r4, sp, #0
    ldr r0, =sramStackD137EraseBlock
    movs r3, #1
    bics r0, r3
    ldr r1, =sramStackD137EraseBlockEnd
    movs r3, #1
    bics r1, r3
    adds r2, r4, #0
.Lcopy_d137_erase_worker:
    cmp r0, r1
    bhs .Ld137_erase_worker_copied
    ldrh r3, [r0]
    strh r3, [r2]
    adds r0, #2
    adds r2, #2
    b .Lcopy_d137_erase_worker
.Ld137_erase_worker_copied:
    adds r0, r5, #0
    ldr r3, =.Ld137_erase_worker_return + 1
    mov lr, r3
    adds r3, r4, #1
    bx r3
.Ld137_erase_worker_return:
    add sp, #0x100
    pop {r4-r5, pc}
.size gbashRunSramStackErase, .-gbashRunSramStackErase
