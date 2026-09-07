/* SPDX-License-Identifier: GPL-3.0-or-later */
.syntax unified
.cpu arm7tdmi
.thumb
.section .text,"ax",%progbits

.macro FN name
.global \name
.global \name\()_end
.thumb_func
\name:
.endm
.macro END name
\name\()_end:
.endm

/* EEPROM -> 32 KiB cart SRAM conversion, adapted from SuperFW patches.S. */
FN sfw_eeprom_read_sram
    lsls r0, r0, #22
    lsrs r0, r0, #19
    ldr r2, 9f
    adds r2, r2, r0
    adds r2, #8
    movs r0, #8
1:  subs r2, #1
    ldrb r3, [r2]
    strb r3, [r1]
    adds r1, #1
    subs r0, #1
    bne 1b
    bx lr
    .align 2
9:  .word 0x0E000000
END sfw_eeprom_read_sram

FN sfw_eeprom_write_sram
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
    bx lr
    .align 2
9:  .word 0x0E000000
END sfw_eeprom_write_sram

/* Direct-NOR trampolines. Each ends in one dispatcher literal patched by GBABF. */
FN sfw_flash_read_direct
    push {r7, lr}
    lsls r0, r0, #27
    lsrs r0, r0, #15
    adds r1, r1, r0
    movs r0, r2
    movs r2, r3
    movs r7, #2
    ldr r3, 9f
    bl 1f
    pop {r7}
    pop {r1}
    bx r1
1:  bx r3
    .align 2
.global sfw_flash_read_direct_dispatch
9:  .word 0
.set sfw_flash_read_direct_dispatch, 9b
END sfw_flash_read_direct

FN sfw_flash_erase_chip_direct
    push {r7, lr}
    movs r7, #4
    ldr r3, 9f
    bl 1f
    pop {r7}
    pop {r1}
    bx r1
1:  bx r3
    .align 2
.global sfw_flash_erase_chip_direct_dispatch
9:  .word 0
.set sfw_flash_erase_chip_direct_dispatch, 9b
END sfw_flash_erase_chip_direct

FN sfw_flash_erase_sector_direct
    push {r7, lr}
    lsls r0, r0, #27
    lsrs r0, r0, #27
    movs r7, #5
    ldr r3, 9f
    bl 1f
    pop {r7}
    pop {r1}
    bx r1
1:  bx r3
    .align 2
.global sfw_flash_erase_sector_direct_dispatch
9:  .word 0
.set sfw_flash_erase_sector_direct_dispatch, 9b
END sfw_flash_erase_sector_direct

FN sfw_flash_write_sector_direct
    push {r7, lr}
    lsls r0, r0, #27
    lsrs r0, r0, #27
    movs r2, r0
    movs r0, r1
    movs r1, r2
    movs r7, #3
    ldr r3, 9f
    bl 1f
    pop {r7}
    pop {r1}
    bx r1
1:  bx r3
    .align 2
.global sfw_flash_write_sector_direct_dispatch
9:  .word 0
.set sfw_flash_write_sector_direct_dispatch, 9b
END sfw_flash_write_sector_direct

FN sfw_flash_write_byte_direct
    push {r7, lr}
    lsls r0, r0, #27
    lsrs r0, r0, #27
    movs r7, #6
    ldr r3, 9f
    bl 1f
    pop {r7}
    pop {r1}
    bx r1
1:  bx r3
    .align 2
.global sfw_flash_write_byte_direct_dispatch
9:  .word 0
.set sfw_flash_write_byte_direct_dispatch, 9b
END sfw_flash_write_byte_direct

/* Hard-coded IDs matching SuperFW's conversion stubs. */
FN sfw_flash_ident_512
    movs r0, #0x1c
    lsls r0, r0, #8
    adds r0, #0xc2
    bx lr
END sfw_flash_ident_512

FN sfw_flash_ident_1m
    movs r0, #0x09
    lsls r0, r0, #8
    adds r0, #0xc2
    bx lr
END sfw_flash_ident_1m

FN sfw_thumb_ret0
    movs r0, #0
    bx lr
END sfw_thumb_ret0
