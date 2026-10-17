.syntax unified
.arm
.text
.global fail_test, timeout_test, timeout_rw
fail_test:
 mov r1,#3
 b auto_fail
timeout_test:
 mov r1,#4
 b auto_fail
timeout_rw:
 mvn r0,#0
 mov r1,#4
 b auto_fail

// A single non-nested recovery point for safe, pure CPU groups.
// Restore callee-saved registers, original SP, LR and control bits.
.global suite_call, suite_recover
suite_call:
 push {r4-r12,lr}
 ldr r1,=saved_context
 str sp,[r1]
 mrs r2,cpsr
 str r2,[r1,#4]
 mov lr,pc
 bx r0
 mov r0,#0
 pop {r4-r12,lr}
 bx lr
suite_recover:
 ldr r1,=0x04000208
 mov r2,#0
 str r2,[r1]
 ldr r2,=0x00052078
 mcr p15,0,r2,c1,c0,0
 ldr r2,=0x0080000a
 mcr p15,0,r2,c9,c1,0
 mov r2,#0x0c
 mcr p15,0,r2,c9,c1,1
 ldr r1,=saved_context
 ldr r2,[r1,#4]
 msr cpsr_c,r2
 ldr sp,[r1]
 mov r0,#1
 pop {r4-r12,lr}
 bx lr
.align 2
saved_context: .word 0,0
