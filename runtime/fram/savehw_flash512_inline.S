/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * FLASH512 -> one 64 KiB SRAM/FRAM window.
 *
 * FIX201L deliberately follows SuperFW's patch_flash_*_sram64k instruction
 * sequences exactly.  The only project-specific piece is that the SRAM base
 * literal is emitted as zero and filled from the active SaveHwProfile by the
 * ARM9 virtual-ROM overlay composer.  For ChisFlash this resolves to the same
 * 0x0E000000 literal used by upstream SuperFW.
 *
 * Upstream reference:
 *   davidgfnet/superfw, src/patches.S, FLASH -> SRAM (64KB) conversion.
 */
.syntax unified
.cpu arm7tdmi
.thumb
.section .text,"ax",%progbits

.macro FN name
.balign 4
.global \name
.global \name\()_end
.global \name\()_window
.thumb_func
\name:
.endm
.macro END name
.balign 4
\name\()_end:
.endm

/* Upstream patch_flash_read_sram64k.
 * R0=sector, R1=byte offset, R2=destination, R3=count. Returns 0. */
FN savehw_f512_read
    lsls r0, r0, #28
    lsrs r0, r0, #16
    adds r0, r0, r1
    ldr r1, 9f
    adds r0, r0, r1
1:
    ldrb r1, [r0]
    strb r1, [r2]
    adds r0, #1
    adds r2, #1
    subs r3, #1
    bne 1b
    movs r0, #0
    bx lr
.balign 4
9:  .word 0
.set savehw_f512_read_window, 9b
END savehw_f512_read

/* Upstream patch_flash_erase_device_sram64k, intentionally byte-for-byte in
 * semantics including the historical 64<<10 loop count.  On a 64 KiB SRAM
 * window the address region aliases/mirrors just as upstream expects. */
FN savehw_f512_erase_chip
    movs r0, #64
    lsls r0, r0, #10
    ldr r2, 9f
    movs r1, #0xFF
1:
    strb r1, [r2, #0]
    strb r1, [r2, #1]
    strb r1, [r2, #2]
    strb r1, [r2, #3]
    adds r2, #4
    subs r0, #1
    bne 1b
    bx lr
.balign 4
9:  .word 0
.set savehw_f512_erase_chip_window, 9b
END savehw_f512_erase_chip

/* Upstream patch_flash_erase_sector_sram64k. R0=4 KiB sector. */
FN savehw_f512_erase_sector
    lsls r0, r0, #28
    lsrs r0, r0, #16
    ldr r2, 9f
    adds r2, r2, r0
    movs r1, #0xFF
    ldr r0, 8f
1:
    strb r1, [r2, #0]
    strb r1, [r2, #1]
    strb r1, [r2, #2]
    strb r1, [r2, #3]
    adds r2, #4
    subs r0, #1
    bne 1b
    bx lr
.balign 4
8:  .word 1024
9:  .word 0
.set savehw_f512_erase_sector_window, 9b
END savehw_f512_erase_sector

/* Upstream patch_flash_write_sector_sram64k. R0=sector, R1=source. */
FN savehw_f512_write_sector
    lsls r0, r0, #28
    lsrs r0, r0, #16
    ldr r2, 9f
    adds r2, r2, r0
    movs r0, #4
    lsls r0, r0, #10
1:
    ldrb r3, [r1, #0]
    strb r3, [r2, #0]
    ldrb r3, [r1, #1]
    strb r3, [r2, #1]
    ldrb r3, [r1, #2]
    strb r3, [r2, #2]
    ldrb r3, [r1, #3]
    strb r3, [r2, #3]
    adds r1, #4
    adds r2, #4
    subs r0, #4
    bne 1b
    bx lr
.balign 4
9:  .word 0
.set savehw_f512_write_sector_window, 9b
END savehw_f512_write_sector




/* Upstream patch_flash_write_byte_sram64k. */
FN savehw_f512_write_byte
    lsls r0, r0, #28
    lsrs r0, r0, #16
    adds r0, r0, r1
    ldr r1, 9f
    adds r0, r0, r1
    strb r2, [r0]
    movs r0, #0
    bx lr
.balign 4
9:  .word 0
.set savehw_f512_write_byte_window, 9b
END savehw_f512_write_byte
