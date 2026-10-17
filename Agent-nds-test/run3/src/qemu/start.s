.syntax unified
.arm
.text
.global _start
_start:
 ldr sp,=0x01000000
 bl oracle_main
1: b 1b
.global semi
semi:
 svc 0x123456
 bx lr
