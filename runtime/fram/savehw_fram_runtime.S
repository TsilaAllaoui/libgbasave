.syntax unified
.cpu arm7tdmi
.arm
.section .text,"ax",%progbits

/* Generic SRAM/FRAM flash-emulation dispatcher.
 * This payload is copied once into the ROM target and configured by the
 * SaveHwProfile.  The small Thumb SDK-handler trampolines above branch here.
 * No cart family is encoded in the logic; selector/window wiring is data.
 */
.arm
.align 2
.global sfw_fram_payload_start
.global sfw_fram_payload_end
.global sfw_fram_dispatch
.global sfw_fram_cfg_window
.global sfw_fram_cfg_selector
.global sfw_fram_cfg_selector_mask
.global sfw_fram_cfg_selector_shift
.global sfw_fram_cfg_selector_fixed
.global sfw_fram_cfg_selector_width
.global sfw_fram_cfg_sector_count
.global sfw_fram_cfg_window_bytes
.global sfw_fram_cfg_bank_count
sfw_fram_payload_start:
    .ascii "GBFSHFRAM1"
    .byte 0,0
    .align 2
/* Fail-closed placeholders. Every operational value is overlaid from the
 * active SaveHwProfile before this payload is ever flashed. Keeping these
 * zero also makes it impossible for a new cart family to accidentally inherit
 * cart-family wiring from the generic runtime. */
sfw_fram_cfg_window:          .word 0
sfw_fram_cfg_selector:        .word 0
sfw_fram_cfg_selector_mask:   .word 0
sfw_fram_cfg_selector_shift:  .word 0
sfw_fram_cfg_selector_fixed:  .word 0
sfw_fram_cfg_selector_width:  .word 0
sfw_fram_cfg_sector_count:    .word 0
sfw_fram_cfg_window_bytes:    .word 0
sfw_fram_cfg_bank_count:      .word 0

/* Dispatcher ABI matches the existing SuperFW direct trampolines:
 * r7=2 read:         r0=dst, r1=logical byte offset, r2=count
 * r7=3 write sector: r0=src, r1=4KiB sector
 * r7=4 erase chip
 * r7=5 erase sector: r0=4KiB sector
 * r7=6 write byte:   r0=sector, r1=byte offset, r2=value
 * r0=0 success, nonzero failure.
 */
sfw_fram_dispatch:
    stmdb sp!, {r4-r11, lr}
    cmp r7, #2
    beq fram_read
    cmp r7, #3
    beq fram_write_sector
    cmp r7, #4
    beq fram_erase_chip
    cmp r7, #5
    beq fram_erase_sector
    cmp r7, #6
    beq fram_write_byte
    b fram_fail

/* r0=logical bank. clobbers r1-r3,r12. */
fram_select_bank:
    adr r1, sfw_fram_cfg_bank_count
    ldr r1, [r1]
    cmp r0, r1
    bhs fram_select_fail
    adr r1, sfw_fram_cfg_selector_shift
    ldr r1, [r1]
    mov r0, r0, lsl r1
    adr r1, sfw_fram_cfg_selector_mask
    ldr r1, [r1]
    and r0, r0, r1
    adr r1, sfw_fram_cfg_selector_fixed
    ldr r1, [r1]
    orr r0, r0, r1
    adr r1, sfw_fram_cfg_selector
    ldr r1, [r1]
    adr r2, sfw_fram_cfg_selector_width
    ldr r2, [r2]
    cmp r2, #8
    strbeq r0, [r1]
    cmp r2, #16
    strheq r0, [r1]
    cmp r2, #8
    beq 1f
    cmp r2, #16
    bne fram_select_fail
1:  mov r0, #0
    bx lr
fram_select_fail:
    mov r0, #1
    bx lr

fram_read:
    /* Validate logical range against sector_count*4096. */
    adr r3, sfw_fram_cfg_sector_count
    ldr r3, [r3]
    mov r3, r3, lsl #12
    cmp r1, r3
    bhs fram_fail
    sub r4, r3, r1
    cmp r2, r4
    bhi fram_fail
    mov r8, r0              /* dst */
    mov r9, r1              /* logical offset */
    mov r10, r2             /* count */
1:  cmp r10, #0
    beq fram_ok
    adr r3, sfw_fram_cfg_window_bytes
    ldr r11, [r3]
    sub r3, r11, #1
    and r5, r9, r3          /* within bank */
    mov r6, r9              /* bank = offset / windowBytes; profile window is pow2 */
    mov r7, #0
2:  cmp r11, #1
    beq 3f
    mov r11, r11, lsr #1
    add r7, r7, #1
    b 2b
3:  mov r6, r6, lsr r7
    mov r0, r6
    bl fram_select_bank
    cmp r0, #0
    bne fram_fail
    adr r3, sfw_fram_cfg_window
    ldr r4, [r3]
    add r4, r4, r5
    adr r3, sfw_fram_cfg_window_bytes
    ldr r6, [r3]
    sub r6, r6, r5
    cmp r10, r6
    movlo r6, r10
4:  ldrb r3, [r4], #1
    strb r3, [r8], #1
    add r9, r9, #1
    subs r10, r10, #1
    subs r6, r6, #1
    bne 4b
    b 1b

fram_write_sector:
    adr r3, sfw_fram_cfg_sector_count
    ldr r3, [r3]
    cmp r1, r3
    bhs fram_fail
    mov r8, r0              /* src */
    mov r9, r1              /* sector */
    mov r0, r9, lsr #4      /* 16 sectors per 64KiB bank */
    bl fram_select_bank
    cmp r0, #0
    bne fram_fail
    and r9, r9, #15
    mov r9, r9, lsl #12
    adr r3, sfw_fram_cfg_window
    ldr r4, [r3]
    add r4, r4, r9
    mov r5, #0x1000
5:  ldrb r6, [r8], #1
    strb r6, [r4], #1
    subs r5, r5, #1
    bne 5b
    b fram_ok

fram_erase_chip:
    adr r3, sfw_fram_cfg_sector_count
    ldr r10, [r3]
    mov r9, #0
6:  cmp r9, r10
    bhs fram_ok
    mov r0, r9, lsr #4
    bl fram_select_bank
    cmp r0, #0
    bne fram_fail
    and r4, r9, #15
    mov r4, r4, lsl #12
    adr r3, sfw_fram_cfg_window
    ldr r5, [r3]
    add r5, r5, r4
    mov r6, #0x1000
    mov r7, #0xFF
7:  strb r7, [r5], #1
    subs r6, r6, #1
    bne 7b
    add r9, r9, #1
    b 6b

fram_erase_sector:
    adr r3, sfw_fram_cfg_sector_count
    ldr r3, [r3]
    cmp r0, r3
    bhs fram_fail
    mov r9, r0
    mov r0, r9, lsr #4
    bl fram_select_bank
    cmp r0, #0
    bne fram_fail
    and r4, r9, #15
    mov r4, r4, lsl #12
    adr r3, sfw_fram_cfg_window
    ldr r5, [r3]
    add r5, r5, r4
    mov r6, #0x1000
    mov r7, #0xFF
8:  strb r7, [r5], #1
    subs r6, r6, #1
    bne 8b
    b fram_ok

fram_write_byte:
    adr r3, sfw_fram_cfg_sector_count
    ldr r3, [r3]
    cmp r0, r3
    bhs fram_fail
    cmp r1, #0x1000
    bhs fram_fail
    mov r9, r0
    mov r10, r1
    mov r11, r2
    mov r0, r9, lsr #4
    bl fram_select_bank
    cmp r0, #0
    bne fram_fail
    and r9, r9, #15
    mov r9, r9, lsl #12
    add r9, r9, r10
    adr r3, sfw_fram_cfg_window
    ldr r4, [r3]
    strb r11, [r4, r9]
    b fram_ok

fram_ok:
    mov r0, #0
    b fram_return
fram_fail:
    mov r0, #1
fram_return:
    ldmia sp!, {r4-r11, lr}
    bx lr
sfw_fram_payload_end:
