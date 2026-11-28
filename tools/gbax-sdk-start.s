/* SPDX-License-Identifier: MPL-2.0 */
.syntax unified
.cpu arm7tdmi
.section .text.start,"ax"
.arm
.global _start
_start:
    b boot
    .org 0xa0
    .ascii "GBAX RAM64  GBAX"
    .ascii "00"
    .byte 0x96
    .org 0xc0
boot:
    ldr sp,=0x03007e00
    bl sdk_test
    ldr r1,=0x02000000
    str r0,[r1]
1:  b 1b
    .ltorg
