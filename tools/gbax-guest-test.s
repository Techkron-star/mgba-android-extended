/* SPDX-License-Identifier: MPL-2.0
 * Original GBAX diagnostic ROM. No commercial game assets.
 * HLE/skip BIOS; green = PASS, red = FAIL, status word at 0x02000000.
 */
.syntax unified
.cpu arm7tdmi
.section .text
.arm
.global _start
_start:
    b start
    .org 0xa0
    .ascii "GBAX RAM64  GBAX"
    .ascii "00"
    .byte 0x96
    .org 0xc0
start:
    ldr r6, =0x02000000
    mov r1, #0
    str r1, [r6]
    ldr r0, =0x40000000
    ldr r2, =1024
    ldr r3, =0x0000fffc
    ldr r4, =0x5a5a0000
    ldr r5, =0xa5a50000
    /* Write both ends of each page, then read in a separate pass.
       Distinct values detect aliasing across the entire 64 MiB aperture. */
write_pages:
    ldr r7, [r0]
    cmp r7, #0
    bne fail
    orr r7, r4, r1
    str r7, [r0]
    orr r7, r5, r1
    str r7, [r0, r3]
    add r0, r0, #0x10000
    add r1, r1, #1
    cmp r1, r2
    bne write_pages
    ldr r0, =0x40000000
    mov r1, #0
read_pages:
    ldr r7, [r0]
    orr r8, r4, r1
    cmp r7, r8
    bne fail
    ldr r7, [r0, r3]
    orr r8, r5, r1
    cmp r7, r8
    bne fail
    add r0, r0, #0x10000
    add r1, r1, #1
    cmp r1, r2
    bne read_pages
    /* ARM scalar byte/halfword and unaligned word rotation. */
    ldr r0, =0x40000100
    ldr r1, =0x12345678
    str r1, [r0]
    ldr r2, [r0, #1]
    ldr r3, =0x78123456
    cmp r2, r3
    bne fail
    mov r1, #0xab
    strb r1, [r0, #1]
    ldrb r2, [r0, #1]
    cmp r1, r2
    bne fail
    ldr r1, =0xcdef
    strh r1, [r0, #2]
    ldrh r2, [r0, #2]
    cmp r1, r2
    bne fail
    /* Multiple transfers cross a page boundary. */
    ldr r0, =0x4000fffc
    ldr r1, =0x11223344
    ldr r2, =0x55667788
    stmia r0!, {r1,r2}
    ldr r3, =0x40010004
    cmp r0, r3
    bne fail
    ldmdb r0!, {r4,r5}
    cmp r4, r1
    bne fail
    cmp r5, r2
    bne fail
    ldr r3, =0x4000fffc
    cmp r0, r3
    bne fail
    adr r3, thumb_entry
    orr r3, r3, #1
    bx r3
fail:
    ldr r1, =0xbad0bad0
    ldr r2, =0x001f
    b finish
pass:
    ldr r1, =0x600d600d
    ldr r2, =0x03e0
finish:
    ldr r0, =0x02000000
    str r1, [r0]
    ldr r0, =0x05000000
    strh r2, [r0]
    ldr r0, =0x04000000
    mov r1, #0
    strh r1, [r0]
halt:
    b halt
    .align 2
.thumb
thumb_entry:
    ldr r0, =0x43fffff0
    ldr r1, =0x89abcdef
    str r1, [r0]
    ldr r2, [r0]
    cmp r1, r2
    bne thumb_fail
    movs r1, #0x42
    strb r1, [r0, #1]
    ldrb r2, [r0, #1]
    cmp r1, r2
    bne thumb_fail
    ldr r1, =0x9876
    strh r1, [r0, #2]
    ldrh r2, [r0, #2]
    cmp r1, r2
    bne thumb_fail
    ldr r1, =0xdeadbeef
    ldr r2, =0x10203040
    stmia r0!, {r1,r2}
    subs r0, #8
    ldmia r0!, {r3,r4}
    cmp r1, r3
    bne thumb_fail
    cmp r2, r4
    bne thumb_fail
    ldr r3, =pass
    bx r3
thumb_fail:
    ldr r3, =fail
    bx r3
    .ltorg
