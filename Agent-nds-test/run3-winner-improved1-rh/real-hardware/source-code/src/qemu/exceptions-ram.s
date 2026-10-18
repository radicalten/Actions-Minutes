.syntax unified
.arch armv5te
.text
.arm
.align 2
.global cpu_arm9_ldrd_offset_data
.type cpu_arm9_ldrd_offset_data, %function
cpu_arm9_ldrd_offset_data:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
add r6,r5,#24
mov r7,#8
str r0,[r5,#32]
str r1,[r5,#36]
ldrd r0,r1,[r6,#8]
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.arm
.align 2
.global cpu_arm9_ldrd_offset_writeback
.type cpu_arm9_ldrd_offset_writeback, %function
cpu_arm9_ldrd_offset_writeback:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
add r6,r5,#24
mov r7,#8
str r0,[r5,#32]
str r1,[r5,#36]
ldrd r0,r1,[r6,#8]
sub r0,r6,r5
sub r0,r0,#24
ldr r1,[r5,#32]
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.arm
.align 2
.global cpu_arm9_ldrd_pre_plus_data
.type cpu_arm9_ldrd_pre_plus_data, %function
cpu_arm9_ldrd_pre_plus_data:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
add r6,r5,#24
mov r7,#8
str r0,[r5,#32]
str r1,[r5,#36]
ldrd r0,r1,[r6,#8]!
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.arm
.align 2
.global cpu_arm9_ldrd_pre_plus_writeback
.type cpu_arm9_ldrd_pre_plus_writeback, %function
cpu_arm9_ldrd_pre_plus_writeback:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
add r6,r5,#24
mov r7,#8
str r0,[r5,#32]
str r1,[r5,#36]
ldrd r0,r1,[r6,#8]!
sub r0,r6,r5
sub r0,r0,#24
ldr r1,[r5,#32]
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.arm
.align 2
.global cpu_arm9_ldrd_pre_minus_data
.type cpu_arm9_ldrd_pre_minus_data, %function
cpu_arm9_ldrd_pre_minus_data:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
add r6,r5,#24
mov r7,#8
str r0,[r5,#16]
str r1,[r5,#20]
ldrd r0,r1,[r6,#-8]!
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.arm
.align 2
.global cpu_arm9_ldrd_pre_minus_writeback
.type cpu_arm9_ldrd_pre_minus_writeback, %function
cpu_arm9_ldrd_pre_minus_writeback:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
add r6,r5,#24
mov r7,#8
str r0,[r5,#16]
str r1,[r5,#20]
ldrd r0,r1,[r6,#-8]!
sub r0,r6,r5
sub r0,r0,#24
ldr r1,[r5,#16]
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.arm
.align 2
.global cpu_arm9_ldrd_post_plus_data
.type cpu_arm9_ldrd_post_plus_data, %function
cpu_arm9_ldrd_post_plus_data:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
add r6,r5,#24
mov r7,#8
str r0,[r5,#24]
str r1,[r5,#28]
ldrd r0,r1,[r6],#8
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.arm
.align 2
.global cpu_arm9_ldrd_post_plus_writeback
.type cpu_arm9_ldrd_post_plus_writeback, %function
cpu_arm9_ldrd_post_plus_writeback:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
add r6,r5,#24
mov r7,#8
str r0,[r5,#24]
str r1,[r5,#28]
ldrd r0,r1,[r6],#8
sub r0,r6,r5
sub r0,r0,#24
ldr r1,[r5,#24]
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.arm
.align 2
.global cpu_arm9_ldrd_post_minus_data
.type cpu_arm9_ldrd_post_minus_data, %function
cpu_arm9_ldrd_post_minus_data:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
add r6,r5,#24
mov r7,#8
str r0,[r5,#24]
str r1,[r5,#28]
ldrd r0,r1,[r6],#-8
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.arm
.align 2
.global cpu_arm9_ldrd_post_minus_writeback
.type cpu_arm9_ldrd_post_minus_writeback, %function
cpu_arm9_ldrd_post_minus_writeback:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
add r6,r5,#24
mov r7,#8
str r0,[r5,#24]
str r1,[r5,#28]
ldrd r0,r1,[r6],#-8
sub r0,r6,r5
sub r0,r0,#24
ldr r1,[r5,#24]
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.arm
.align 2
.global cpu_arm9_ldrd_register_data
.type cpu_arm9_ldrd_register_data, %function
cpu_arm9_ldrd_register_data:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
add r6,r5,#24
mov r7,#8
str r0,[r5,#32]
str r1,[r5,#36]
ldrd r0,r1,[r6,r7]
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.arm
.align 2
.global cpu_arm9_ldrd_register_writeback
.type cpu_arm9_ldrd_register_writeback, %function
cpu_arm9_ldrd_register_writeback:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
add r6,r5,#24
mov r7,#8
str r0,[r5,#32]
str r1,[r5,#36]
ldrd r0,r1,[r6,r7]
sub r0,r6,r5
sub r0,r0,#24
ldr r1,[r5,#32]
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.arm
.align 2
.global cpu_arm9_strd_offset_data
.type cpu_arm9_strd_offset_data, %function
cpu_arm9_strd_offset_data:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
add r6,r5,#24
mov r7,#8
mvn r8,r0
str r8,[r5,#32]
mvn r8,r1
str r8,[r5,#36]
strd r0,r1,[r6,#8]
ldr r0,[r5,#32]
ldr r1,[r5,#36]
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.arm
.align 2
.global cpu_arm9_strd_offset_writeback
.type cpu_arm9_strd_offset_writeback, %function
cpu_arm9_strd_offset_writeback:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
add r6,r5,#24
mov r7,#8
mvn r8,r0
str r8,[r5,#32]
mvn r8,r1
str r8,[r5,#36]
strd r0,r1,[r6,#8]
sub r0,r6,r5
sub r0,r0,#24
ldr r1,[r5,#32]
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.arm
.align 2
.global cpu_arm9_strd_pre_plus_data
.type cpu_arm9_strd_pre_plus_data, %function
cpu_arm9_strd_pre_plus_data:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
add r6,r5,#24
mov r7,#8
mvn r8,r0
str r8,[r5,#32]
mvn r8,r1
str r8,[r5,#36]
strd r0,r1,[r6,#8]!
ldr r0,[r5,#32]
ldr r1,[r5,#36]
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.arm
.align 2
.global cpu_arm9_strd_pre_plus_writeback
.type cpu_arm9_strd_pre_plus_writeback, %function
cpu_arm9_strd_pre_plus_writeback:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
add r6,r5,#24
mov r7,#8
mvn r8,r0
str r8,[r5,#32]
mvn r8,r1
str r8,[r5,#36]
strd r0,r1,[r6,#8]!
sub r0,r6,r5
sub r0,r0,#24
ldr r1,[r5,#32]
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.arm
.align 2
.global cpu_arm9_strd_pre_minus_data
.type cpu_arm9_strd_pre_minus_data, %function
cpu_arm9_strd_pre_minus_data:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
add r6,r5,#24
mov r7,#8
mvn r8,r0
str r8,[r5,#16]
mvn r8,r1
str r8,[r5,#20]
strd r0,r1,[r6,#-8]!
ldr r0,[r5,#16]
ldr r1,[r5,#20]
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.arm
.align 2
.global cpu_arm9_strd_pre_minus_writeback
.type cpu_arm9_strd_pre_minus_writeback, %function
cpu_arm9_strd_pre_minus_writeback:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
add r6,r5,#24
mov r7,#8
mvn r8,r0
str r8,[r5,#16]
mvn r8,r1
str r8,[r5,#20]
strd r0,r1,[r6,#-8]!
sub r0,r6,r5
sub r0,r0,#24
ldr r1,[r5,#16]
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.arm
.align 2
.global cpu_arm9_strd_post_plus_data
.type cpu_arm9_strd_post_plus_data, %function
cpu_arm9_strd_post_plus_data:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
add r6,r5,#24
mov r7,#8
mvn r8,r0
str r8,[r5,#24]
mvn r8,r1
str r8,[r5,#28]
strd r0,r1,[r6],#8
ldr r0,[r5,#24]
ldr r1,[r5,#28]
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.arm
.align 2
.global cpu_arm9_strd_post_plus_writeback
.type cpu_arm9_strd_post_plus_writeback, %function
cpu_arm9_strd_post_plus_writeback:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
add r6,r5,#24
mov r7,#8
mvn r8,r0
str r8,[r5,#24]
mvn r8,r1
str r8,[r5,#28]
strd r0,r1,[r6],#8
sub r0,r6,r5
sub r0,r0,#24
ldr r1,[r5,#24]
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.arm
.align 2
.global cpu_arm9_strd_post_minus_data
.type cpu_arm9_strd_post_minus_data, %function
cpu_arm9_strd_post_minus_data:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
add r6,r5,#24
mov r7,#8
mvn r8,r0
str r8,[r5,#24]
mvn r8,r1
str r8,[r5,#28]
strd r0,r1,[r6],#-8
ldr r0,[r5,#24]
ldr r1,[r5,#28]
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.arm
.align 2
.global cpu_arm9_strd_post_minus_writeback
.type cpu_arm9_strd_post_minus_writeback, %function
cpu_arm9_strd_post_minus_writeback:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
add r6,r5,#24
mov r7,#8
mvn r8,r0
str r8,[r5,#24]
mvn r8,r1
str r8,[r5,#28]
strd r0,r1,[r6],#-8
sub r0,r6,r5
sub r0,r0,#24
ldr r1,[r5,#24]
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.arm
.align 2
.global cpu_arm9_strd_register_data
.type cpu_arm9_strd_register_data, %function
cpu_arm9_strd_register_data:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
add r6,r5,#24
mov r7,#8
mvn r8,r0
str r8,[r5,#32]
mvn r8,r1
str r8,[r5,#36]
strd r0,r1,[r6,r7]
ldr r0,[r5,#32]
ldr r1,[r5,#36]
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.arm
.align 2
.global cpu_arm9_strd_register_writeback
.type cpu_arm9_strd_register_writeback, %function
cpu_arm9_strd_register_writeback:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
add r6,r5,#24
mov r7,#8
mvn r8,r0
str r8,[r5,#32]
mvn r8,r1
str r8,[r5,#36]
strd r0,r1,[r6,r7]
sub r0,r6,r5
sub r0,r0,#24
ldr r1,[r5,#32]
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.arm
.balign 4
.global cpu_arm9_qadd_sticky_q
.type cpu_arm9_qadd_sticky_q, %function
cpu_arm9_qadd_sticky_q:
push {r4-r8,lr}
mov r4,r3
msr cpsr_f,r2
qadd r0,r0,r1
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_qsub_sticky_q
.type cpu_arm9_qsub_sticky_q, %function
cpu_arm9_qsub_sticky_q:
push {r4-r8,lr}
mov r4,r3
msr cpsr_f,r2
qsub r0,r0,r1
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_qdadd_sticky_q
.type cpu_arm9_qdadd_sticky_q, %function
cpu_arm9_qdadd_sticky_q:
push {r4-r8,lr}
mov r4,r3
msr cpsr_f,r2
qdadd r0,r0,r1
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_qdsub_sticky_q
.type cpu_arm9_qdsub_sticky_q, %function
cpu_arm9_qdsub_sticky_q:
push {r4-r8,lr}
mov r4,r3
msr cpsr_f,r2
qdsub r0,r0,r1
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_smulbb_flags
.type cpu_arm9_smulbb_flags, %function
cpu_arm9_smulbb_flags:
push {r4-r8,lr}
mov r4,r3
msr cpsr_f,r2
smulbb r0,r0,r1
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_smulbt_flags
.type cpu_arm9_smulbt_flags, %function
cpu_arm9_smulbt_flags:
push {r4-r8,lr}
mov r4,r3
msr cpsr_f,r2
smulbt r0,r0,r1
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_smultb_flags
.type cpu_arm9_smultb_flags, %function
cpu_arm9_smultb_flags:
push {r4-r8,lr}
mov r4,r3
msr cpsr_f,r2
smultb r0,r0,r1
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_smultt_flags
.type cpu_arm9_smultt_flags, %function
cpu_arm9_smultt_flags:
push {r4-r8,lr}
mov r4,r3
msr cpsr_f,r2
smultt r0,r0,r1
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_smlabb_q0
.type cpu_arm9_smlabb_q0, %function
cpu_arm9_smlabb_q0:
push {r4-r8,lr}
mov r4,r3
mov r5,r2
ldr r2,=0x50000000
msr cpsr_f,r2
smlabb r0,r0,r1,r5
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_smlabb_q1
.type cpu_arm9_smlabb_q1, %function
cpu_arm9_smlabb_q1:
push {r4-r8,lr}
mov r4,r3
mov r5,r2
ldr r2,=0xa8000000
msr cpsr_f,r2
smlabb r0,r0,r1,r5
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_smlabt_q0
.type cpu_arm9_smlabt_q0, %function
cpu_arm9_smlabt_q0:
push {r4-r8,lr}
mov r4,r3
mov r5,r2
ldr r2,=0x50000000
msr cpsr_f,r2
smlabt r0,r0,r1,r5
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_smlabt_q1
.type cpu_arm9_smlabt_q1, %function
cpu_arm9_smlabt_q1:
push {r4-r8,lr}
mov r4,r3
mov r5,r2
ldr r2,=0xa8000000
msr cpsr_f,r2
smlabt r0,r0,r1,r5
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_smlatb_q0
.type cpu_arm9_smlatb_q0, %function
cpu_arm9_smlatb_q0:
push {r4-r8,lr}
mov r4,r3
mov r5,r2
ldr r2,=0x50000000
msr cpsr_f,r2
smlatb r0,r0,r1,r5
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_smlatb_q1
.type cpu_arm9_smlatb_q1, %function
cpu_arm9_smlatb_q1:
push {r4-r8,lr}
mov r4,r3
mov r5,r2
ldr r2,=0xa8000000
msr cpsr_f,r2
smlatb r0,r0,r1,r5
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_smlatt_q0
.type cpu_arm9_smlatt_q0, %function
cpu_arm9_smlatt_q0:
push {r4-r8,lr}
mov r4,r3
mov r5,r2
ldr r2,=0x50000000
msr cpsr_f,r2
smlatt r0,r0,r1,r5
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_smlatt_q1
.type cpu_arm9_smlatt_q1, %function
cpu_arm9_smlatt_q1:
push {r4-r8,lr}
mov r4,r3
mov r5,r2
ldr r2,=0xa8000000
msr cpsr_f,r2
smlatt r0,r0,r1,r5
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_smlawb_q0
.type cpu_arm9_smlawb_q0, %function
cpu_arm9_smlawb_q0:
push {r4-r8,lr}
mov r4,r3
mov r5,r2
ldr r2,=0x50000000
msr cpsr_f,r2
smlawb r0,r0,r1,r5
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_smlawb_q1
.type cpu_arm9_smlawb_q1, %function
cpu_arm9_smlawb_q1:
push {r4-r8,lr}
mov r4,r3
mov r5,r2
ldr r2,=0xa8000000
msr cpsr_f,r2
smlawb r0,r0,r1,r5
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_smlawt_q0
.type cpu_arm9_smlawt_q0, %function
cpu_arm9_smlawt_q0:
push {r4-r8,lr}
mov r4,r3
mov r5,r2
ldr r2,=0x50000000
msr cpsr_f,r2
smlawt r0,r0,r1,r5
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_smlawt_q1
.type cpu_arm9_smlawt_q1, %function
cpu_arm9_smlawt_q1:
push {r4-r8,lr}
mov r4,r3
mov r5,r2
ldr r2,=0xa8000000
msr cpsr_f,r2
smlawt r0,r0,r1,r5
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_smulwb_flags
.type cpu_arm9_smulwb_flags, %function
cpu_arm9_smulwb_flags:
push {r4-r8,lr}
mov r4,r3
msr cpsr_f,r2
smulwb r0,r0,r1
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_smulwt_flags
.type cpu_arm9_smulwt_flags, %function
cpu_arm9_smulwt_flags:
push {r4-r8,lr}
mov r4,r3
msr cpsr_f,r2
smulwt r0,r0,r1
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_smlalbb_acc0_data
.type cpu_arm9_smlalbb_acc0_data, %function
cpu_arm9_smlalbb_acc0_data:
push {r4-r8,lr}
mov r4,r3
msr cpsr_f,r2
ldr r5,=0x0
ldr r6,=0x0
smlalbb r5,r6,r0,r1
mov r0,r5
mov r1,r6
str r0,[r4]
str r1,[r4,#4]
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_smlalbb_acc0_flags
.type cpu_arm9_smlalbb_acc0_flags, %function
cpu_arm9_smlalbb_acc0_flags:
push {r4-r8,lr}
mov r4,r3
msr cpsr_f,r2
ldr r5,=0x0
ldr r6,=0x0
smlalbb r5,r6,r0,r1
mov r0,r5
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_smlalbb_acc1_data
.type cpu_arm9_smlalbb_acc1_data, %function
cpu_arm9_smlalbb_acc1_data:
push {r4-r8,lr}
mov r4,r3
msr cpsr_f,r2
ldr r5,=0xffffffff
ldr r6,=0x7fffffff
smlalbb r5,r6,r0,r1
mov r0,r5
mov r1,r6
str r0,[r4]
str r1,[r4,#4]
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_smlalbb_acc1_flags
.type cpu_arm9_smlalbb_acc1_flags, %function
cpu_arm9_smlalbb_acc1_flags:
push {r4-r8,lr}
mov r4,r3
msr cpsr_f,r2
ldr r5,=0xffffffff
ldr r6,=0x7fffffff
smlalbb r5,r6,r0,r1
mov r0,r5
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_smlalbb_acc2_data
.type cpu_arm9_smlalbb_acc2_data, %function
cpu_arm9_smlalbb_acc2_data:
push {r4-r8,lr}
mov r4,r3
msr cpsr_f,r2
ldr r5,=0x0
ldr r6,=0x80000000
smlalbb r5,r6,r0,r1
mov r0,r5
mov r1,r6
str r0,[r4]
str r1,[r4,#4]
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_smlalbb_acc2_flags
.type cpu_arm9_smlalbb_acc2_flags, %function
cpu_arm9_smlalbb_acc2_flags:
push {r4-r8,lr}
mov r4,r3
msr cpsr_f,r2
ldr r5,=0x0
ldr r6,=0x80000000
smlalbb r5,r6,r0,r1
mov r0,r5
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_smlalbb_acc3_data
.type cpu_arm9_smlalbb_acc3_data, %function
cpu_arm9_smlalbb_acc3_data:
push {r4-r8,lr}
mov r4,r3
msr cpsr_f,r2
ldr r5,=0xffffffff
ldr r6,=0xffffffff
smlalbb r5,r6,r0,r1
mov r0,r5
mov r1,r6
str r0,[r4]
str r1,[r4,#4]
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_smlalbb_acc3_flags
.type cpu_arm9_smlalbb_acc3_flags, %function
cpu_arm9_smlalbb_acc3_flags:
push {r4-r8,lr}
mov r4,r3
msr cpsr_f,r2
ldr r5,=0xffffffff
ldr r6,=0xffffffff
smlalbb r5,r6,r0,r1
mov r0,r5
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_smlalbt_acc0_data
.type cpu_arm9_smlalbt_acc0_data, %function
cpu_arm9_smlalbt_acc0_data:
push {r4-r8,lr}
mov r4,r3
msr cpsr_f,r2
ldr r5,=0x0
ldr r6,=0x0
smlalbt r5,r6,r0,r1
mov r0,r5
mov r1,r6
str r0,[r4]
str r1,[r4,#4]
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_smlalbt_acc0_flags
.type cpu_arm9_smlalbt_acc0_flags, %function
cpu_arm9_smlalbt_acc0_flags:
push {r4-r8,lr}
mov r4,r3
msr cpsr_f,r2
ldr r5,=0x0
ldr r6,=0x0
smlalbt r5,r6,r0,r1
mov r0,r5
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_smlalbt_acc1_data
.type cpu_arm9_smlalbt_acc1_data, %function
cpu_arm9_smlalbt_acc1_data:
push {r4-r8,lr}
mov r4,r3
msr cpsr_f,r2
ldr r5,=0xffffffff
ldr r6,=0x7fffffff
smlalbt r5,r6,r0,r1
mov r0,r5
mov r1,r6
str r0,[r4]
str r1,[r4,#4]
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_smlalbt_acc1_flags
.type cpu_arm9_smlalbt_acc1_flags, %function
cpu_arm9_smlalbt_acc1_flags:
push {r4-r8,lr}
mov r4,r3
msr cpsr_f,r2
ldr r5,=0xffffffff
ldr r6,=0x7fffffff
smlalbt r5,r6,r0,r1
mov r0,r5
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_smlalbt_acc2_data
.type cpu_arm9_smlalbt_acc2_data, %function
cpu_arm9_smlalbt_acc2_data:
push {r4-r8,lr}
mov r4,r3
msr cpsr_f,r2
ldr r5,=0x0
ldr r6,=0x80000000
smlalbt r5,r6,r0,r1
mov r0,r5
mov r1,r6
str r0,[r4]
str r1,[r4,#4]
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_smlalbt_acc2_flags
.type cpu_arm9_smlalbt_acc2_flags, %function
cpu_arm9_smlalbt_acc2_flags:
push {r4-r8,lr}
mov r4,r3
msr cpsr_f,r2
ldr r5,=0x0
ldr r6,=0x80000000
smlalbt r5,r6,r0,r1
mov r0,r5
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_smlalbt_acc3_data
.type cpu_arm9_smlalbt_acc3_data, %function
cpu_arm9_smlalbt_acc3_data:
push {r4-r8,lr}
mov r4,r3
msr cpsr_f,r2
ldr r5,=0xffffffff
ldr r6,=0xffffffff
smlalbt r5,r6,r0,r1
mov r0,r5
mov r1,r6
str r0,[r4]
str r1,[r4,#4]
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_smlalbt_acc3_flags
.type cpu_arm9_smlalbt_acc3_flags, %function
cpu_arm9_smlalbt_acc3_flags:
push {r4-r8,lr}
mov r4,r3
msr cpsr_f,r2
ldr r5,=0xffffffff
ldr r6,=0xffffffff
smlalbt r5,r6,r0,r1
mov r0,r5
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_smlaltb_acc0_data
.type cpu_arm9_smlaltb_acc0_data, %function
cpu_arm9_smlaltb_acc0_data:
push {r4-r8,lr}
mov r4,r3
msr cpsr_f,r2
ldr r5,=0x0
ldr r6,=0x0
smlaltb r5,r6,r0,r1
mov r0,r5
mov r1,r6
str r0,[r4]
str r1,[r4,#4]
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_smlaltb_acc0_flags
.type cpu_arm9_smlaltb_acc0_flags, %function
cpu_arm9_smlaltb_acc0_flags:
push {r4-r8,lr}
mov r4,r3
msr cpsr_f,r2
ldr r5,=0x0
ldr r6,=0x0
smlaltb r5,r6,r0,r1
mov r0,r5
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_smlaltb_acc1_data
.type cpu_arm9_smlaltb_acc1_data, %function
cpu_arm9_smlaltb_acc1_data:
push {r4-r8,lr}
mov r4,r3
msr cpsr_f,r2
ldr r5,=0xffffffff
ldr r6,=0x7fffffff
smlaltb r5,r6,r0,r1
mov r0,r5
mov r1,r6
str r0,[r4]
str r1,[r4,#4]
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_smlaltb_acc1_flags
.type cpu_arm9_smlaltb_acc1_flags, %function
cpu_arm9_smlaltb_acc1_flags:
push {r4-r8,lr}
mov r4,r3
msr cpsr_f,r2
ldr r5,=0xffffffff
ldr r6,=0x7fffffff
smlaltb r5,r6,r0,r1
mov r0,r5
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_smlaltb_acc2_data
.type cpu_arm9_smlaltb_acc2_data, %function
cpu_arm9_smlaltb_acc2_data:
push {r4-r8,lr}
mov r4,r3
msr cpsr_f,r2
ldr r5,=0x0
ldr r6,=0x80000000
smlaltb r5,r6,r0,r1
mov r0,r5
mov r1,r6
str r0,[r4]
str r1,[r4,#4]
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_smlaltb_acc2_flags
.type cpu_arm9_smlaltb_acc2_flags, %function
cpu_arm9_smlaltb_acc2_flags:
push {r4-r8,lr}
mov r4,r3
msr cpsr_f,r2
ldr r5,=0x0
ldr r6,=0x80000000
smlaltb r5,r6,r0,r1
mov r0,r5
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_smlaltb_acc3_data
.type cpu_arm9_smlaltb_acc3_data, %function
cpu_arm9_smlaltb_acc3_data:
push {r4-r8,lr}
mov r4,r3
msr cpsr_f,r2
ldr r5,=0xffffffff
ldr r6,=0xffffffff
smlaltb r5,r6,r0,r1
mov r0,r5
mov r1,r6
str r0,[r4]
str r1,[r4,#4]
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_smlaltb_acc3_flags
.type cpu_arm9_smlaltb_acc3_flags, %function
cpu_arm9_smlaltb_acc3_flags:
push {r4-r8,lr}
mov r4,r3
msr cpsr_f,r2
ldr r5,=0xffffffff
ldr r6,=0xffffffff
smlaltb r5,r6,r0,r1
mov r0,r5
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_smlaltt_acc0_data
.type cpu_arm9_smlaltt_acc0_data, %function
cpu_arm9_smlaltt_acc0_data:
push {r4-r8,lr}
mov r4,r3
msr cpsr_f,r2
ldr r5,=0x0
ldr r6,=0x0
smlaltt r5,r6,r0,r1
mov r0,r5
mov r1,r6
str r0,[r4]
str r1,[r4,#4]
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_smlaltt_acc0_flags
.type cpu_arm9_smlaltt_acc0_flags, %function
cpu_arm9_smlaltt_acc0_flags:
push {r4-r8,lr}
mov r4,r3
msr cpsr_f,r2
ldr r5,=0x0
ldr r6,=0x0
smlaltt r5,r6,r0,r1
mov r0,r5
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_smlaltt_acc1_data
.type cpu_arm9_smlaltt_acc1_data, %function
cpu_arm9_smlaltt_acc1_data:
push {r4-r8,lr}
mov r4,r3
msr cpsr_f,r2
ldr r5,=0xffffffff
ldr r6,=0x7fffffff
smlaltt r5,r6,r0,r1
mov r0,r5
mov r1,r6
str r0,[r4]
str r1,[r4,#4]
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_smlaltt_acc1_flags
.type cpu_arm9_smlaltt_acc1_flags, %function
cpu_arm9_smlaltt_acc1_flags:
push {r4-r8,lr}
mov r4,r3
msr cpsr_f,r2
ldr r5,=0xffffffff
ldr r6,=0x7fffffff
smlaltt r5,r6,r0,r1
mov r0,r5
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_smlaltt_acc2_data
.type cpu_arm9_smlaltt_acc2_data, %function
cpu_arm9_smlaltt_acc2_data:
push {r4-r8,lr}
mov r4,r3
msr cpsr_f,r2
ldr r5,=0x0
ldr r6,=0x80000000
smlaltt r5,r6,r0,r1
mov r0,r5
mov r1,r6
str r0,[r4]
str r1,[r4,#4]
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_smlaltt_acc2_flags
.type cpu_arm9_smlaltt_acc2_flags, %function
cpu_arm9_smlaltt_acc2_flags:
push {r4-r8,lr}
mov r4,r3
msr cpsr_f,r2
ldr r5,=0x0
ldr r6,=0x80000000
smlaltt r5,r6,r0,r1
mov r0,r5
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_smlaltt_acc3_data
.type cpu_arm9_smlaltt_acc3_data, %function
cpu_arm9_smlaltt_acc3_data:
push {r4-r8,lr}
mov r4,r3
msr cpsr_f,r2
ldr r5,=0xffffffff
ldr r6,=0xffffffff
smlaltt r5,r6,r0,r1
mov r0,r5
mov r1,r6
str r0,[r4]
str r1,[r4,#4]
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_smlaltt_acc3_flags
.type cpu_arm9_smlaltt_acc3_flags, %function
cpu_arm9_smlaltt_acc3_flags:
push {r4-r8,lr}
mov r4,r3
msr cpsr_f,r2
ldr r5,=0xffffffff
ldr r6,=0xffffffff
smlaltt r5,r6,r0,r1
mov r0,r5
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_bank_sp_sys_fiq
.type cpu_arm9_bank_sp_sys_fiq, %function
cpu_arm9_bank_sp_sys_fiq:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#223
mov r2,sp
mov sp,r0
msr cpsr_c,#209
mov r5,sp
mov sp,r1
msr cpsr_c,#223
mov r0,sp
mov sp,r2
msr cpsr_c,#209
mov r1,sp
mov sp,r5
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_bank_lr_sys_fiq
.type cpu_arm9_bank_lr_sys_fiq, %function
cpu_arm9_bank_lr_sys_fiq:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#223
mov r2,lr
mov lr,r0
msr cpsr_c,#209
mov r5,lr
mov lr,r1
msr cpsr_c,#223
mov r0,lr
mov lr,r2
msr cpsr_c,#209
mov r1,lr
mov lr,r5
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_bank_sp_sys_irq
.type cpu_arm9_bank_sp_sys_irq, %function
cpu_arm9_bank_sp_sys_irq:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#223
mov r2,sp
mov sp,r0
msr cpsr_c,#210
mov r5,sp
mov sp,r1
msr cpsr_c,#223
mov r0,sp
mov sp,r2
msr cpsr_c,#210
mov r1,sp
mov sp,r5
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_bank_lr_sys_irq
.type cpu_arm9_bank_lr_sys_irq, %function
cpu_arm9_bank_lr_sys_irq:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#223
mov r2,lr
mov lr,r0
msr cpsr_c,#210
mov r5,lr
mov lr,r1
msr cpsr_c,#223
mov r0,lr
mov lr,r2
msr cpsr_c,#210
mov r1,lr
mov lr,r5
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_bank_sp_sys_svc
.type cpu_arm9_bank_sp_sys_svc, %function
cpu_arm9_bank_sp_sys_svc:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#223
mov r2,sp
mov sp,r0
msr cpsr_c,#211
mov r5,sp
mov sp,r1
msr cpsr_c,#223
mov r0,sp
mov sp,r2
msr cpsr_c,#211
mov r1,sp
mov sp,r5
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_bank_lr_sys_svc
.type cpu_arm9_bank_lr_sys_svc, %function
cpu_arm9_bank_lr_sys_svc:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#223
mov r2,lr
mov lr,r0
msr cpsr_c,#211
mov r5,lr
mov lr,r1
msr cpsr_c,#223
mov r0,lr
mov lr,r2
msr cpsr_c,#211
mov r1,lr
mov lr,r5
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_bank_sp_sys_abt
.type cpu_arm9_bank_sp_sys_abt, %function
cpu_arm9_bank_sp_sys_abt:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#223
mov r2,sp
mov sp,r0
msr cpsr_c,#215
mov r5,sp
mov sp,r1
msr cpsr_c,#223
mov r0,sp
mov sp,r2
msr cpsr_c,#215
mov r1,sp
mov sp,r5
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_bank_lr_sys_abt
.type cpu_arm9_bank_lr_sys_abt, %function
cpu_arm9_bank_lr_sys_abt:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#223
mov r2,lr
mov lr,r0
msr cpsr_c,#215
mov r5,lr
mov lr,r1
msr cpsr_c,#223
mov r0,lr
mov lr,r2
msr cpsr_c,#215
mov r1,lr
mov lr,r5
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_bank_sp_sys_und
.type cpu_arm9_bank_sp_sys_und, %function
cpu_arm9_bank_sp_sys_und:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#223
mov r2,sp
mov sp,r0
msr cpsr_c,#219
mov r5,sp
mov sp,r1
msr cpsr_c,#223
mov r0,sp
mov sp,r2
msr cpsr_c,#219
mov r1,sp
mov sp,r5
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_bank_lr_sys_und
.type cpu_arm9_bank_lr_sys_und, %function
cpu_arm9_bank_lr_sys_und:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#223
mov r2,lr
mov lr,r0
msr cpsr_c,#219
mov r5,lr
mov lr,r1
msr cpsr_c,#223
mov r0,lr
mov lr,r2
msr cpsr_c,#219
mov r1,lr
mov lr,r5
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_bank_sp_fiq_sys
.type cpu_arm9_bank_sp_fiq_sys, %function
cpu_arm9_bank_sp_fiq_sys:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#209
mov r2,sp
mov sp,r0
msr cpsr_c,#223
mov r5,sp
mov sp,r1
msr cpsr_c,#209
mov r0,sp
mov sp,r2
msr cpsr_c,#223
mov r1,sp
mov sp,r5
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_bank_lr_fiq_sys
.type cpu_arm9_bank_lr_fiq_sys, %function
cpu_arm9_bank_lr_fiq_sys:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#209
mov r2,lr
mov lr,r0
msr cpsr_c,#223
mov r5,lr
mov lr,r1
msr cpsr_c,#209
mov r0,lr
mov lr,r2
msr cpsr_c,#223
mov r1,lr
mov lr,r5
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_bank_sp_fiq_irq
.type cpu_arm9_bank_sp_fiq_irq, %function
cpu_arm9_bank_sp_fiq_irq:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#209
mov r2,sp
mov sp,r0
msr cpsr_c,#210
mov r5,sp
mov sp,r1
msr cpsr_c,#209
mov r0,sp
mov sp,r2
msr cpsr_c,#210
mov r1,sp
mov sp,r5
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_bank_lr_fiq_irq
.type cpu_arm9_bank_lr_fiq_irq, %function
cpu_arm9_bank_lr_fiq_irq:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#209
mov r2,lr
mov lr,r0
msr cpsr_c,#210
mov r5,lr
mov lr,r1
msr cpsr_c,#209
mov r0,lr
mov lr,r2
msr cpsr_c,#210
mov r1,lr
mov lr,r5
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_bank_sp_fiq_svc
.type cpu_arm9_bank_sp_fiq_svc, %function
cpu_arm9_bank_sp_fiq_svc:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#209
mov r2,sp
mov sp,r0
msr cpsr_c,#211
mov r5,sp
mov sp,r1
msr cpsr_c,#209
mov r0,sp
mov sp,r2
msr cpsr_c,#211
mov r1,sp
mov sp,r5
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_bank_lr_fiq_svc
.type cpu_arm9_bank_lr_fiq_svc, %function
cpu_arm9_bank_lr_fiq_svc:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#209
mov r2,lr
mov lr,r0
msr cpsr_c,#211
mov r5,lr
mov lr,r1
msr cpsr_c,#209
mov r0,lr
mov lr,r2
msr cpsr_c,#211
mov r1,lr
mov lr,r5
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_bank_sp_fiq_abt
.type cpu_arm9_bank_sp_fiq_abt, %function
cpu_arm9_bank_sp_fiq_abt:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#209
mov r2,sp
mov sp,r0
msr cpsr_c,#215
mov r5,sp
mov sp,r1
msr cpsr_c,#209
mov r0,sp
mov sp,r2
msr cpsr_c,#215
mov r1,sp
mov sp,r5
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_bank_lr_fiq_abt
.type cpu_arm9_bank_lr_fiq_abt, %function
cpu_arm9_bank_lr_fiq_abt:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#209
mov r2,lr
mov lr,r0
msr cpsr_c,#215
mov r5,lr
mov lr,r1
msr cpsr_c,#209
mov r0,lr
mov lr,r2
msr cpsr_c,#215
mov r1,lr
mov lr,r5
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_bank_sp_fiq_und
.type cpu_arm9_bank_sp_fiq_und, %function
cpu_arm9_bank_sp_fiq_und:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#209
mov r2,sp
mov sp,r0
msr cpsr_c,#219
mov r5,sp
mov sp,r1
msr cpsr_c,#209
mov r0,sp
mov sp,r2
msr cpsr_c,#219
mov r1,sp
mov sp,r5
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_bank_lr_fiq_und
.type cpu_arm9_bank_lr_fiq_und, %function
cpu_arm9_bank_lr_fiq_und:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#209
mov r2,lr
mov lr,r0
msr cpsr_c,#219
mov r5,lr
mov lr,r1
msr cpsr_c,#209
mov r0,lr
mov lr,r2
msr cpsr_c,#219
mov r1,lr
mov lr,r5
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_bank_sp_irq_sys
.type cpu_arm9_bank_sp_irq_sys, %function
cpu_arm9_bank_sp_irq_sys:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#210
mov r2,sp
mov sp,r0
msr cpsr_c,#223
mov r5,sp
mov sp,r1
msr cpsr_c,#210
mov r0,sp
mov sp,r2
msr cpsr_c,#223
mov r1,sp
mov sp,r5
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_bank_lr_irq_sys
.type cpu_arm9_bank_lr_irq_sys, %function
cpu_arm9_bank_lr_irq_sys:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#210
mov r2,lr
mov lr,r0
msr cpsr_c,#223
mov r5,lr
mov lr,r1
msr cpsr_c,#210
mov r0,lr
mov lr,r2
msr cpsr_c,#223
mov r1,lr
mov lr,r5
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_bank_sp_irq_fiq
.type cpu_arm9_bank_sp_irq_fiq, %function
cpu_arm9_bank_sp_irq_fiq:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#210
mov r2,sp
mov sp,r0
msr cpsr_c,#209
mov r5,sp
mov sp,r1
msr cpsr_c,#210
mov r0,sp
mov sp,r2
msr cpsr_c,#209
mov r1,sp
mov sp,r5
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_bank_lr_irq_fiq
.type cpu_arm9_bank_lr_irq_fiq, %function
cpu_arm9_bank_lr_irq_fiq:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#210
mov r2,lr
mov lr,r0
msr cpsr_c,#209
mov r5,lr
mov lr,r1
msr cpsr_c,#210
mov r0,lr
mov lr,r2
msr cpsr_c,#209
mov r1,lr
mov lr,r5
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_bank_sp_irq_svc
.type cpu_arm9_bank_sp_irq_svc, %function
cpu_arm9_bank_sp_irq_svc:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#210
mov r2,sp
mov sp,r0
msr cpsr_c,#211
mov r5,sp
mov sp,r1
msr cpsr_c,#210
mov r0,sp
mov sp,r2
msr cpsr_c,#211
mov r1,sp
mov sp,r5
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_bank_lr_irq_svc
.type cpu_arm9_bank_lr_irq_svc, %function
cpu_arm9_bank_lr_irq_svc:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#210
mov r2,lr
mov lr,r0
msr cpsr_c,#211
mov r5,lr
mov lr,r1
msr cpsr_c,#210
mov r0,lr
mov lr,r2
msr cpsr_c,#211
mov r1,lr
mov lr,r5
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_bank_sp_irq_abt
.type cpu_arm9_bank_sp_irq_abt, %function
cpu_arm9_bank_sp_irq_abt:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#210
mov r2,sp
mov sp,r0
msr cpsr_c,#215
mov r5,sp
mov sp,r1
msr cpsr_c,#210
mov r0,sp
mov sp,r2
msr cpsr_c,#215
mov r1,sp
mov sp,r5
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_bank_lr_irq_abt
.type cpu_arm9_bank_lr_irq_abt, %function
cpu_arm9_bank_lr_irq_abt:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#210
mov r2,lr
mov lr,r0
msr cpsr_c,#215
mov r5,lr
mov lr,r1
msr cpsr_c,#210
mov r0,lr
mov lr,r2
msr cpsr_c,#215
mov r1,lr
mov lr,r5
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_bank_sp_irq_und
.type cpu_arm9_bank_sp_irq_und, %function
cpu_arm9_bank_sp_irq_und:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#210
mov r2,sp
mov sp,r0
msr cpsr_c,#219
mov r5,sp
mov sp,r1
msr cpsr_c,#210
mov r0,sp
mov sp,r2
msr cpsr_c,#219
mov r1,sp
mov sp,r5
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_bank_lr_irq_und
.type cpu_arm9_bank_lr_irq_und, %function
cpu_arm9_bank_lr_irq_und:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#210
mov r2,lr
mov lr,r0
msr cpsr_c,#219
mov r5,lr
mov lr,r1
msr cpsr_c,#210
mov r0,lr
mov lr,r2
msr cpsr_c,#219
mov r1,lr
mov lr,r5
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_bank_sp_svc_sys
.type cpu_arm9_bank_sp_svc_sys, %function
cpu_arm9_bank_sp_svc_sys:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#211
mov r2,sp
mov sp,r0
msr cpsr_c,#223
mov r5,sp
mov sp,r1
msr cpsr_c,#211
mov r0,sp
mov sp,r2
msr cpsr_c,#223
mov r1,sp
mov sp,r5
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_bank_lr_svc_sys
.type cpu_arm9_bank_lr_svc_sys, %function
cpu_arm9_bank_lr_svc_sys:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#211
mov r2,lr
mov lr,r0
msr cpsr_c,#223
mov r5,lr
mov lr,r1
msr cpsr_c,#211
mov r0,lr
mov lr,r2
msr cpsr_c,#223
mov r1,lr
mov lr,r5
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_bank_sp_svc_fiq
.type cpu_arm9_bank_sp_svc_fiq, %function
cpu_arm9_bank_sp_svc_fiq:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#211
mov r2,sp
mov sp,r0
msr cpsr_c,#209
mov r5,sp
mov sp,r1
msr cpsr_c,#211
mov r0,sp
mov sp,r2
msr cpsr_c,#209
mov r1,sp
mov sp,r5
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_bank_lr_svc_fiq
.type cpu_arm9_bank_lr_svc_fiq, %function
cpu_arm9_bank_lr_svc_fiq:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#211
mov r2,lr
mov lr,r0
msr cpsr_c,#209
mov r5,lr
mov lr,r1
msr cpsr_c,#211
mov r0,lr
mov lr,r2
msr cpsr_c,#209
mov r1,lr
mov lr,r5
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_bank_sp_svc_irq
.type cpu_arm9_bank_sp_svc_irq, %function
cpu_arm9_bank_sp_svc_irq:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#211
mov r2,sp
mov sp,r0
msr cpsr_c,#210
mov r5,sp
mov sp,r1
msr cpsr_c,#211
mov r0,sp
mov sp,r2
msr cpsr_c,#210
mov r1,sp
mov sp,r5
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_bank_lr_svc_irq
.type cpu_arm9_bank_lr_svc_irq, %function
cpu_arm9_bank_lr_svc_irq:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#211
mov r2,lr
mov lr,r0
msr cpsr_c,#210
mov r5,lr
mov lr,r1
msr cpsr_c,#211
mov r0,lr
mov lr,r2
msr cpsr_c,#210
mov r1,lr
mov lr,r5
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_bank_sp_svc_abt
.type cpu_arm9_bank_sp_svc_abt, %function
cpu_arm9_bank_sp_svc_abt:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#211
mov r2,sp
mov sp,r0
msr cpsr_c,#215
mov r5,sp
mov sp,r1
msr cpsr_c,#211
mov r0,sp
mov sp,r2
msr cpsr_c,#215
mov r1,sp
mov sp,r5
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_bank_lr_svc_abt
.type cpu_arm9_bank_lr_svc_abt, %function
cpu_arm9_bank_lr_svc_abt:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#211
mov r2,lr
mov lr,r0
msr cpsr_c,#215
mov r5,lr
mov lr,r1
msr cpsr_c,#211
mov r0,lr
mov lr,r2
msr cpsr_c,#215
mov r1,lr
mov lr,r5
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_bank_sp_svc_und
.type cpu_arm9_bank_sp_svc_und, %function
cpu_arm9_bank_sp_svc_und:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#211
mov r2,sp
mov sp,r0
msr cpsr_c,#219
mov r5,sp
mov sp,r1
msr cpsr_c,#211
mov r0,sp
mov sp,r2
msr cpsr_c,#219
mov r1,sp
mov sp,r5
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_bank_lr_svc_und
.type cpu_arm9_bank_lr_svc_und, %function
cpu_arm9_bank_lr_svc_und:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#211
mov r2,lr
mov lr,r0
msr cpsr_c,#219
mov r5,lr
mov lr,r1
msr cpsr_c,#211
mov r0,lr
mov lr,r2
msr cpsr_c,#219
mov r1,lr
mov lr,r5
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_bank_sp_abt_sys
.type cpu_arm9_bank_sp_abt_sys, %function
cpu_arm9_bank_sp_abt_sys:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#215
mov r2,sp
mov sp,r0
msr cpsr_c,#223
mov r5,sp
mov sp,r1
msr cpsr_c,#215
mov r0,sp
mov sp,r2
msr cpsr_c,#223
mov r1,sp
mov sp,r5
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_bank_lr_abt_sys
.type cpu_arm9_bank_lr_abt_sys, %function
cpu_arm9_bank_lr_abt_sys:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#215
mov r2,lr
mov lr,r0
msr cpsr_c,#223
mov r5,lr
mov lr,r1
msr cpsr_c,#215
mov r0,lr
mov lr,r2
msr cpsr_c,#223
mov r1,lr
mov lr,r5
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_bank_sp_abt_fiq
.type cpu_arm9_bank_sp_abt_fiq, %function
cpu_arm9_bank_sp_abt_fiq:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#215
mov r2,sp
mov sp,r0
msr cpsr_c,#209
mov r5,sp
mov sp,r1
msr cpsr_c,#215
mov r0,sp
mov sp,r2
msr cpsr_c,#209
mov r1,sp
mov sp,r5
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_bank_lr_abt_fiq
.type cpu_arm9_bank_lr_abt_fiq, %function
cpu_arm9_bank_lr_abt_fiq:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#215
mov r2,lr
mov lr,r0
msr cpsr_c,#209
mov r5,lr
mov lr,r1
msr cpsr_c,#215
mov r0,lr
mov lr,r2
msr cpsr_c,#209
mov r1,lr
mov lr,r5
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_bank_sp_abt_irq
.type cpu_arm9_bank_sp_abt_irq, %function
cpu_arm9_bank_sp_abt_irq:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#215
mov r2,sp
mov sp,r0
msr cpsr_c,#210
mov r5,sp
mov sp,r1
msr cpsr_c,#215
mov r0,sp
mov sp,r2
msr cpsr_c,#210
mov r1,sp
mov sp,r5
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_bank_lr_abt_irq
.type cpu_arm9_bank_lr_abt_irq, %function
cpu_arm9_bank_lr_abt_irq:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#215
mov r2,lr
mov lr,r0
msr cpsr_c,#210
mov r5,lr
mov lr,r1
msr cpsr_c,#215
mov r0,lr
mov lr,r2
msr cpsr_c,#210
mov r1,lr
mov lr,r5
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_bank_sp_abt_svc
.type cpu_arm9_bank_sp_abt_svc, %function
cpu_arm9_bank_sp_abt_svc:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#215
mov r2,sp
mov sp,r0
msr cpsr_c,#211
mov r5,sp
mov sp,r1
msr cpsr_c,#215
mov r0,sp
mov sp,r2
msr cpsr_c,#211
mov r1,sp
mov sp,r5
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_bank_lr_abt_svc
.type cpu_arm9_bank_lr_abt_svc, %function
cpu_arm9_bank_lr_abt_svc:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#215
mov r2,lr
mov lr,r0
msr cpsr_c,#211
mov r5,lr
mov lr,r1
msr cpsr_c,#215
mov r0,lr
mov lr,r2
msr cpsr_c,#211
mov r1,lr
mov lr,r5
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_bank_sp_abt_und
.type cpu_arm9_bank_sp_abt_und, %function
cpu_arm9_bank_sp_abt_und:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#215
mov r2,sp
mov sp,r0
msr cpsr_c,#219
mov r5,sp
mov sp,r1
msr cpsr_c,#215
mov r0,sp
mov sp,r2
msr cpsr_c,#219
mov r1,sp
mov sp,r5
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_bank_lr_abt_und
.type cpu_arm9_bank_lr_abt_und, %function
cpu_arm9_bank_lr_abt_und:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#215
mov r2,lr
mov lr,r0
msr cpsr_c,#219
mov r5,lr
mov lr,r1
msr cpsr_c,#215
mov r0,lr
mov lr,r2
msr cpsr_c,#219
mov r1,lr
mov lr,r5
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_bank_sp_und_sys
.type cpu_arm9_bank_sp_und_sys, %function
cpu_arm9_bank_sp_und_sys:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#219
mov r2,sp
mov sp,r0
msr cpsr_c,#223
mov r5,sp
mov sp,r1
msr cpsr_c,#219
mov r0,sp
mov sp,r2
msr cpsr_c,#223
mov r1,sp
mov sp,r5
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_bank_lr_und_sys
.type cpu_arm9_bank_lr_und_sys, %function
cpu_arm9_bank_lr_und_sys:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#219
mov r2,lr
mov lr,r0
msr cpsr_c,#223
mov r5,lr
mov lr,r1
msr cpsr_c,#219
mov r0,lr
mov lr,r2
msr cpsr_c,#223
mov r1,lr
mov lr,r5
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_bank_sp_und_fiq
.type cpu_arm9_bank_sp_und_fiq, %function
cpu_arm9_bank_sp_und_fiq:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#219
mov r2,sp
mov sp,r0
msr cpsr_c,#209
mov r5,sp
mov sp,r1
msr cpsr_c,#219
mov r0,sp
mov sp,r2
msr cpsr_c,#209
mov r1,sp
mov sp,r5
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_bank_lr_und_fiq
.type cpu_arm9_bank_lr_und_fiq, %function
cpu_arm9_bank_lr_und_fiq:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#219
mov r2,lr
mov lr,r0
msr cpsr_c,#209
mov r5,lr
mov lr,r1
msr cpsr_c,#219
mov r0,lr
mov lr,r2
msr cpsr_c,#209
mov r1,lr
mov lr,r5
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_bank_sp_und_irq
.type cpu_arm9_bank_sp_und_irq, %function
cpu_arm9_bank_sp_und_irq:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#219
mov r2,sp
mov sp,r0
msr cpsr_c,#210
mov r5,sp
mov sp,r1
msr cpsr_c,#219
mov r0,sp
mov sp,r2
msr cpsr_c,#210
mov r1,sp
mov sp,r5
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_bank_lr_und_irq
.type cpu_arm9_bank_lr_und_irq, %function
cpu_arm9_bank_lr_und_irq:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#219
mov r2,lr
mov lr,r0
msr cpsr_c,#210
mov r5,lr
mov lr,r1
msr cpsr_c,#219
mov r0,lr
mov lr,r2
msr cpsr_c,#210
mov r1,lr
mov lr,r5
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_bank_sp_und_svc
.type cpu_arm9_bank_sp_und_svc, %function
cpu_arm9_bank_sp_und_svc:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#219
mov r2,sp
mov sp,r0
msr cpsr_c,#211
mov r5,sp
mov sp,r1
msr cpsr_c,#219
mov r0,sp
mov sp,r2
msr cpsr_c,#211
mov r1,sp
mov sp,r5
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_bank_lr_und_svc
.type cpu_arm9_bank_lr_und_svc, %function
cpu_arm9_bank_lr_und_svc:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#219
mov r2,lr
mov lr,r0
msr cpsr_c,#211
mov r5,lr
mov lr,r1
msr cpsr_c,#219
mov r0,lr
mov lr,r2
msr cpsr_c,#211
mov r1,lr
mov lr,r5
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_bank_sp_und_abt
.type cpu_arm9_bank_sp_und_abt, %function
cpu_arm9_bank_sp_und_abt:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#219
mov r2,sp
mov sp,r0
msr cpsr_c,#215
mov r5,sp
mov sp,r1
msr cpsr_c,#219
mov r0,sp
mov sp,r2
msr cpsr_c,#215
mov r1,sp
mov sp,r5
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_bank_lr_und_abt
.type cpu_arm9_bank_lr_und_abt, %function
cpu_arm9_bank_lr_und_abt:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#219
mov r2,lr
mov lr,r0
msr cpsr_c,#215
mov r5,lr
mov lr,r1
msr cpsr_c,#219
mov r0,lr
mov lr,r2
msr cpsr_c,#215
mov r1,lr
mov lr,r5
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_bank_r8_sys_fiq
.type cpu_arm9_bank_r8_sys_fiq, %function
cpu_arm9_bank_r8_sys_fiq:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#223
mov r2,r8
mov r8,r0
msr cpsr_c,#209
mov r5,r8
mov r8,r1
msr cpsr_c,#223
mov r0,r8
mov r8,r2
msr cpsr_c,#209
mov r1,r8
mov r8,r5
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_bank_r9_sys_fiq
.type cpu_arm9_bank_r9_sys_fiq, %function
cpu_arm9_bank_r9_sys_fiq:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#223
mov r2,r9
mov r9,r0
msr cpsr_c,#209
mov r5,r9
mov r9,r1
msr cpsr_c,#223
mov r0,r9
mov r9,r2
msr cpsr_c,#209
mov r1,r9
mov r9,r5
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_bank_r10_sys_fiq
.type cpu_arm9_bank_r10_sys_fiq, %function
cpu_arm9_bank_r10_sys_fiq:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#223
mov r2,r10
mov r10,r0
msr cpsr_c,#209
mov r5,r10
mov r10,r1
msr cpsr_c,#223
mov r0,r10
mov r10,r2
msr cpsr_c,#209
mov r1,r10
mov r10,r5
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_bank_r11_sys_fiq
.type cpu_arm9_bank_r11_sys_fiq, %function
cpu_arm9_bank_r11_sys_fiq:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#223
mov r2,r11
mov r11,r0
msr cpsr_c,#209
mov r5,r11
mov r11,r1
msr cpsr_c,#223
mov r0,r11
mov r11,r2
msr cpsr_c,#209
mov r1,r11
mov r11,r5
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_bank_r12_sys_fiq
.type cpu_arm9_bank_r12_sys_fiq, %function
cpu_arm9_bank_r12_sys_fiq:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#223
mov r2,r12
mov r12,r0
msr cpsr_c,#209
mov r5,r12
mov r12,r1
msr cpsr_c,#223
mov r0,r12
mov r12,r2
msr cpsr_c,#209
mov r1,r12
mov r12,r5
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_spsr_banks_fiq_irq
.type cpu_arm9_spsr_banks_fiq_irq, %function
cpu_arm9_spsr_banks_fiq_irq:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#209
mrs r2,spsr
msr spsr_fsxc,r0
msr cpsr_c,#210
mrs r5,spsr
msr spsr_fsxc,r1
msr cpsr_c,#209
mrs r0,spsr
msr spsr_fsxc,r2
msr cpsr_c,#210
mrs r1,spsr
msr spsr_fsxc,r5
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_spsr_banks_fiq_svc
.type cpu_arm9_spsr_banks_fiq_svc, %function
cpu_arm9_spsr_banks_fiq_svc:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#209
mrs r2,spsr
msr spsr_fsxc,r0
msr cpsr_c,#211
mrs r5,spsr
msr spsr_fsxc,r1
msr cpsr_c,#209
mrs r0,spsr
msr spsr_fsxc,r2
msr cpsr_c,#211
mrs r1,spsr
msr spsr_fsxc,r5
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_spsr_banks_fiq_abt
.type cpu_arm9_spsr_banks_fiq_abt, %function
cpu_arm9_spsr_banks_fiq_abt:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#209
mrs r2,spsr
msr spsr_fsxc,r0
msr cpsr_c,#215
mrs r5,spsr
msr spsr_fsxc,r1
msr cpsr_c,#209
mrs r0,spsr
msr spsr_fsxc,r2
msr cpsr_c,#215
mrs r1,spsr
msr spsr_fsxc,r5
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_spsr_banks_fiq_und
.type cpu_arm9_spsr_banks_fiq_und, %function
cpu_arm9_spsr_banks_fiq_und:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#209
mrs r2,spsr
msr spsr_fsxc,r0
msr cpsr_c,#219
mrs r5,spsr
msr spsr_fsxc,r1
msr cpsr_c,#209
mrs r0,spsr
msr spsr_fsxc,r2
msr cpsr_c,#219
mrs r1,spsr
msr spsr_fsxc,r5
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_spsr_banks_irq_fiq
.type cpu_arm9_spsr_banks_irq_fiq, %function
cpu_arm9_spsr_banks_irq_fiq:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#210
mrs r2,spsr
msr spsr_fsxc,r0
msr cpsr_c,#209
mrs r5,spsr
msr spsr_fsxc,r1
msr cpsr_c,#210
mrs r0,spsr
msr spsr_fsxc,r2
msr cpsr_c,#209
mrs r1,spsr
msr spsr_fsxc,r5
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_spsr_banks_irq_svc
.type cpu_arm9_spsr_banks_irq_svc, %function
cpu_arm9_spsr_banks_irq_svc:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#210
mrs r2,spsr
msr spsr_fsxc,r0
msr cpsr_c,#211
mrs r5,spsr
msr spsr_fsxc,r1
msr cpsr_c,#210
mrs r0,spsr
msr spsr_fsxc,r2
msr cpsr_c,#211
mrs r1,spsr
msr spsr_fsxc,r5
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_spsr_banks_irq_abt
.type cpu_arm9_spsr_banks_irq_abt, %function
cpu_arm9_spsr_banks_irq_abt:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#210
mrs r2,spsr
msr spsr_fsxc,r0
msr cpsr_c,#215
mrs r5,spsr
msr spsr_fsxc,r1
msr cpsr_c,#210
mrs r0,spsr
msr spsr_fsxc,r2
msr cpsr_c,#215
mrs r1,spsr
msr spsr_fsxc,r5
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_spsr_banks_irq_und
.type cpu_arm9_spsr_banks_irq_und, %function
cpu_arm9_spsr_banks_irq_und:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#210
mrs r2,spsr
msr spsr_fsxc,r0
msr cpsr_c,#219
mrs r5,spsr
msr spsr_fsxc,r1
msr cpsr_c,#210
mrs r0,spsr
msr spsr_fsxc,r2
msr cpsr_c,#219
mrs r1,spsr
msr spsr_fsxc,r5
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_spsr_banks_svc_fiq
.type cpu_arm9_spsr_banks_svc_fiq, %function
cpu_arm9_spsr_banks_svc_fiq:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#211
mrs r2,spsr
msr spsr_fsxc,r0
msr cpsr_c,#209
mrs r5,spsr
msr spsr_fsxc,r1
msr cpsr_c,#211
mrs r0,spsr
msr spsr_fsxc,r2
msr cpsr_c,#209
mrs r1,spsr
msr spsr_fsxc,r5
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_spsr_banks_svc_irq
.type cpu_arm9_spsr_banks_svc_irq, %function
cpu_arm9_spsr_banks_svc_irq:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#211
mrs r2,spsr
msr spsr_fsxc,r0
msr cpsr_c,#210
mrs r5,spsr
msr spsr_fsxc,r1
msr cpsr_c,#211
mrs r0,spsr
msr spsr_fsxc,r2
msr cpsr_c,#210
mrs r1,spsr
msr spsr_fsxc,r5
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_spsr_banks_svc_abt
.type cpu_arm9_spsr_banks_svc_abt, %function
cpu_arm9_spsr_banks_svc_abt:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#211
mrs r2,spsr
msr spsr_fsxc,r0
msr cpsr_c,#215
mrs r5,spsr
msr spsr_fsxc,r1
msr cpsr_c,#211
mrs r0,spsr
msr spsr_fsxc,r2
msr cpsr_c,#215
mrs r1,spsr
msr spsr_fsxc,r5
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_spsr_banks_svc_und
.type cpu_arm9_spsr_banks_svc_und, %function
cpu_arm9_spsr_banks_svc_und:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#211
mrs r2,spsr
msr spsr_fsxc,r0
msr cpsr_c,#219
mrs r5,spsr
msr spsr_fsxc,r1
msr cpsr_c,#211
mrs r0,spsr
msr spsr_fsxc,r2
msr cpsr_c,#219
mrs r1,spsr
msr spsr_fsxc,r5
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_spsr_banks_abt_fiq
.type cpu_arm9_spsr_banks_abt_fiq, %function
cpu_arm9_spsr_banks_abt_fiq:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#215
mrs r2,spsr
msr spsr_fsxc,r0
msr cpsr_c,#209
mrs r5,spsr
msr spsr_fsxc,r1
msr cpsr_c,#215
mrs r0,spsr
msr spsr_fsxc,r2
msr cpsr_c,#209
mrs r1,spsr
msr spsr_fsxc,r5
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_spsr_banks_abt_irq
.type cpu_arm9_spsr_banks_abt_irq, %function
cpu_arm9_spsr_banks_abt_irq:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#215
mrs r2,spsr
msr spsr_fsxc,r0
msr cpsr_c,#210
mrs r5,spsr
msr spsr_fsxc,r1
msr cpsr_c,#215
mrs r0,spsr
msr spsr_fsxc,r2
msr cpsr_c,#210
mrs r1,spsr
msr spsr_fsxc,r5
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_spsr_banks_abt_svc
.type cpu_arm9_spsr_banks_abt_svc, %function
cpu_arm9_spsr_banks_abt_svc:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#215
mrs r2,spsr
msr spsr_fsxc,r0
msr cpsr_c,#211
mrs r5,spsr
msr spsr_fsxc,r1
msr cpsr_c,#215
mrs r0,spsr
msr spsr_fsxc,r2
msr cpsr_c,#211
mrs r1,spsr
msr spsr_fsxc,r5
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_spsr_banks_abt_und
.type cpu_arm9_spsr_banks_abt_und, %function
cpu_arm9_spsr_banks_abt_und:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#215
mrs r2,spsr
msr spsr_fsxc,r0
msr cpsr_c,#219
mrs r5,spsr
msr spsr_fsxc,r1
msr cpsr_c,#215
mrs r0,spsr
msr spsr_fsxc,r2
msr cpsr_c,#219
mrs r1,spsr
msr spsr_fsxc,r5
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_spsr_banks_und_fiq
.type cpu_arm9_spsr_banks_und_fiq, %function
cpu_arm9_spsr_banks_und_fiq:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#219
mrs r2,spsr
msr spsr_fsxc,r0
msr cpsr_c,#209
mrs r5,spsr
msr spsr_fsxc,r1
msr cpsr_c,#219
mrs r0,spsr
msr spsr_fsxc,r2
msr cpsr_c,#209
mrs r1,spsr
msr spsr_fsxc,r5
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_spsr_banks_und_irq
.type cpu_arm9_spsr_banks_und_irq, %function
cpu_arm9_spsr_banks_und_irq:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#219
mrs r2,spsr
msr spsr_fsxc,r0
msr cpsr_c,#210
mrs r5,spsr
msr spsr_fsxc,r1
msr cpsr_c,#219
mrs r0,spsr
msr spsr_fsxc,r2
msr cpsr_c,#210
mrs r1,spsr
msr spsr_fsxc,r5
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_spsr_banks_und_svc
.type cpu_arm9_spsr_banks_und_svc, %function
cpu_arm9_spsr_banks_und_svc:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#219
mrs r2,spsr
msr spsr_fsxc,r0
msr cpsr_c,#211
mrs r5,spsr
msr spsr_fsxc,r1
msr cpsr_c,#219
mrs r0,spsr
msr spsr_fsxc,r2
msr cpsr_c,#211
mrs r1,spsr
msr spsr_fsxc,r5
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_spsr_banks_und_abt
.type cpu_arm9_spsr_banks_und_abt, %function
cpu_arm9_spsr_banks_und_abt:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#219
mrs r2,spsr
msr spsr_fsxc,r0
msr cpsr_c,#215
mrs r5,spsr
msr spsr_fsxc,r1
msr cpsr_c,#219
mrs r0,spsr
msr spsr_fsxc,r2
msr cpsr_c,#215
mrs r1,spsr
msr spsr_fsxc,r5
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_spsr_field_f_fiq
.type cpu_arm9_spsr_field_f_fiq, %function
cpu_arm9_spsr_field_f_fiq:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#209
mrs r2,spsr
msr spsr_fsxc,r0
msr spsr_f,r1
mrs r0,spsr
msr spsr_fsxc,r2
mrs r1,spsr
eor r1,r1,r2
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_spsr_field_c_fiq
.type cpu_arm9_spsr_field_c_fiq, %function
cpu_arm9_spsr_field_c_fiq:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#209
mrs r2,spsr
msr spsr_fsxc,r0
msr spsr_c,r1
mrs r0,spsr
msr spsr_fsxc,r2
mrs r1,spsr
eor r1,r1,r2
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_spsr_field_f_irq
.type cpu_arm9_spsr_field_f_irq, %function
cpu_arm9_spsr_field_f_irq:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#210
mrs r2,spsr
msr spsr_fsxc,r0
msr spsr_f,r1
mrs r0,spsr
msr spsr_fsxc,r2
mrs r1,spsr
eor r1,r1,r2
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_spsr_field_c_irq
.type cpu_arm9_spsr_field_c_irq, %function
cpu_arm9_spsr_field_c_irq:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#210
mrs r2,spsr
msr spsr_fsxc,r0
msr spsr_c,r1
mrs r0,spsr
msr spsr_fsxc,r2
mrs r1,spsr
eor r1,r1,r2
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_spsr_field_f_svc
.type cpu_arm9_spsr_field_f_svc, %function
cpu_arm9_spsr_field_f_svc:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#211
mrs r2,spsr
msr spsr_fsxc,r0
msr spsr_f,r1
mrs r0,spsr
msr spsr_fsxc,r2
mrs r1,spsr
eor r1,r1,r2
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_spsr_field_c_svc
.type cpu_arm9_spsr_field_c_svc, %function
cpu_arm9_spsr_field_c_svc:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#211
mrs r2,spsr
msr spsr_fsxc,r0
msr spsr_c,r1
mrs r0,spsr
msr spsr_fsxc,r2
mrs r1,spsr
eor r1,r1,r2
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_spsr_field_f_abt
.type cpu_arm9_spsr_field_f_abt, %function
cpu_arm9_spsr_field_f_abt:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#215
mrs r2,spsr
msr spsr_fsxc,r0
msr spsr_f,r1
mrs r0,spsr
msr spsr_fsxc,r2
mrs r1,spsr
eor r1,r1,r2
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_spsr_field_c_abt
.type cpu_arm9_spsr_field_c_abt, %function
cpu_arm9_spsr_field_c_abt:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#215
mrs r2,spsr
msr spsr_fsxc,r0
msr spsr_c,r1
mrs r0,spsr
msr spsr_fsxc,r2
mrs r1,spsr
eor r1,r1,r2
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_spsr_field_f_und
.type cpu_arm9_spsr_field_f_und, %function
cpu_arm9_spsr_field_f_und:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#219
mrs r2,spsr
msr spsr_fsxc,r0
msr spsr_f,r1
mrs r0,spsr
msr spsr_fsxc,r2
mrs r1,spsr
eor r1,r1,r2
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_spsr_field_c_und
.type cpu_arm9_spsr_field_c_und, %function
cpu_arm9_spsr_field_c_und:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#219
mrs r2,spsr
msr spsr_fsxc,r0
msr spsr_c,r1
mrs r0,spsr
msr spsr_fsxc,r2
mrs r1,spsr
eor r1,r1,r2
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_cpsr_mode_sys
.type cpu_arm9_cpsr_mode_sys, %function
cpu_arm9_cpsr_mode_sys:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_f,r0
msr cpsr_c,#223
mrs r0,cpsr
ldr r2,=0xf00000ff
and r0,r0,r2
mov r1,#0
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_cpsr_mode_fiq
.type cpu_arm9_cpsr_mode_fiq, %function
cpu_arm9_cpsr_mode_fiq:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_f,r0
msr cpsr_c,#209
mrs r0,cpsr
ldr r2,=0xf00000ff
and r0,r0,r2
mov r1,#0
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_cpsr_mode_irq
.type cpu_arm9_cpsr_mode_irq, %function
cpu_arm9_cpsr_mode_irq:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_f,r0
msr cpsr_c,#210
mrs r0,cpsr
ldr r2,=0xf00000ff
and r0,r0,r2
mov r1,#0
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_cpsr_mode_svc
.type cpu_arm9_cpsr_mode_svc, %function
cpu_arm9_cpsr_mode_svc:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_f,r0
msr cpsr_c,#211
mrs r0,cpsr
ldr r2,=0xf00000ff
and r0,r0,r2
mov r1,#0
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_cpsr_mode_abt
.type cpu_arm9_cpsr_mode_abt, %function
cpu_arm9_cpsr_mode_abt:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_f,r0
msr cpsr_c,#215
mrs r0,cpsr
ldr r2,=0xf00000ff
and r0,r0,r2
mov r1,#0
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_cpsr_mode_und
.type cpu_arm9_cpsr_mode_und, %function
cpu_arm9_cpsr_mode_und:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_f,r0
msr cpsr_c,#219
mrs r0,cpsr
ldr r2,=0xf00000ff
and r0,r0,r2
mov r1,#0
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_return_movs_fiq_arm
.type cpu_arm9_return_movs_fiq_arm, %function
cpu_arm9_return_movs_fiq_arm:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#209
mrs r2,spsr
str r2,[r7]
str lr,[r7,#4]
orr r0,r0,#223
msr spsr_fsxc,r0
adr r1,1f
str r1,[r7,#8]
adr r2,2f
mov lr,r1
movs pc,lr
.arm
.balign 4
1:
mov r0,#4
2:
mrs r1,cpsr
msr cpsr_c,#209
ldr r2,[r7]
msr spsr_fsxc,r2
ldr lr,[r7,#4]
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_return_movs_fiq_thumb
.type cpu_arm9_return_movs_fiq_thumb, %function
cpu_arm9_return_movs_fiq_thumb:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#209
mrs r2,spsr
str r2,[r7]
str lr,[r7,#4]
orr r0,r0,#255
msr spsr_fsxc,r0
adr r1,1f
str r1,[r7,#8]
adr r2,2f
mov lr,r1
movs pc,lr
.thumb
.balign 4
1:
mov r0,pc
bx r2
.arm
.balign 4
2:
ldr r2,[r7,#8]
sub r0,r0,r2
mrs r1,cpsr
msr cpsr_c,#209
ldr r2,[r7]
msr spsr_fsxc,r2
ldr lr,[r7,#4]
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_return_subs_fiq_arm
.type cpu_arm9_return_subs_fiq_arm, %function
cpu_arm9_return_subs_fiq_arm:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#209
mrs r2,spsr
str r2,[r7]
str lr,[r7,#4]
orr r0,r0,#223
msr spsr_fsxc,r0
adr r1,1f
str r1,[r7,#8]
adr r2,2f
add lr,r1,#4
subs pc,lr,#4
.arm
.balign 4
1:
mov r0,#4
2:
mrs r1,cpsr
msr cpsr_c,#209
ldr r2,[r7]
msr spsr_fsxc,r2
ldr lr,[r7,#4]
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_return_subs_fiq_thumb
.type cpu_arm9_return_subs_fiq_thumb, %function
cpu_arm9_return_subs_fiq_thumb:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#209
mrs r2,spsr
str r2,[r7]
str lr,[r7,#4]
orr r0,r0,#255
msr spsr_fsxc,r0
adr r1,1f
str r1,[r7,#8]
adr r2,2f
add lr,r1,#4
subs pc,lr,#4
.thumb
.balign 4
1:
mov r0,pc
bx r2
.arm
.balign 4
2:
ldr r2,[r7,#8]
sub r0,r0,r2
mrs r1,cpsr
msr cpsr_c,#209
ldr r2,[r7]
msr spsr_fsxc,r2
ldr lr,[r7,#4]
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_return_ldm_fiq_arm
.type cpu_arm9_return_ldm_fiq_arm, %function
cpu_arm9_return_ldm_fiq_arm:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#209
mrs r2,spsr
str r2,[r7]
str lr,[r7,#4]
orr r0,r0,#223
msr spsr_fsxc,r0
adr r1,1f
str r1,[r7,#8]
adr r2,2f
str r1,[r7,#12]
add r5,r7,#12
ldmia r5,{pc}^
.arm
.balign 4
1:
mov r0,#4
2:
mrs r1,cpsr
msr cpsr_c,#209
ldr r2,[r7]
msr spsr_fsxc,r2
ldr lr,[r7,#4]
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_return_ldm_fiq_thumb
.type cpu_arm9_return_ldm_fiq_thumb, %function
cpu_arm9_return_ldm_fiq_thumb:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#209
mrs r2,spsr
str r2,[r7]
str lr,[r7,#4]
orr r0,r0,#255
msr spsr_fsxc,r0
adr r1,1f
str r1,[r7,#8]
adr r2,2f
str r1,[r7,#12]
add r5,r7,#12
ldmia r5,{pc}^
.thumb
.balign 4
1:
mov r0,pc
bx r2
.arm
.balign 4
2:
ldr r2,[r7,#8]
sub r0,r0,r2
mrs r1,cpsr
msr cpsr_c,#209
ldr r2,[r7]
msr spsr_fsxc,r2
ldr lr,[r7,#4]
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_return_movs_irq_arm
.type cpu_arm9_return_movs_irq_arm, %function
cpu_arm9_return_movs_irq_arm:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#210
mrs r2,spsr
str r2,[r7]
str lr,[r7,#4]
orr r0,r0,#223
msr spsr_fsxc,r0
adr r1,1f
str r1,[r7,#8]
adr r2,2f
mov lr,r1
movs pc,lr
.arm
.balign 4
1:
mov r0,#4
2:
mrs r1,cpsr
msr cpsr_c,#210
ldr r2,[r7]
msr spsr_fsxc,r2
ldr lr,[r7,#4]
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_return_movs_irq_thumb
.type cpu_arm9_return_movs_irq_thumb, %function
cpu_arm9_return_movs_irq_thumb:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#210
mrs r2,spsr
str r2,[r7]
str lr,[r7,#4]
orr r0,r0,#255
msr spsr_fsxc,r0
adr r1,1f
str r1,[r7,#8]
adr r2,2f
mov lr,r1
movs pc,lr
.thumb
.balign 4
1:
mov r0,pc
bx r2
.arm
.balign 4
2:
ldr r2,[r7,#8]
sub r0,r0,r2
mrs r1,cpsr
msr cpsr_c,#210
ldr r2,[r7]
msr spsr_fsxc,r2
ldr lr,[r7,#4]
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_return_subs_irq_arm
.type cpu_arm9_return_subs_irq_arm, %function
cpu_arm9_return_subs_irq_arm:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#210
mrs r2,spsr
str r2,[r7]
str lr,[r7,#4]
orr r0,r0,#223
msr spsr_fsxc,r0
adr r1,1f
str r1,[r7,#8]
adr r2,2f
add lr,r1,#4
subs pc,lr,#4
.arm
.balign 4
1:
mov r0,#4
2:
mrs r1,cpsr
msr cpsr_c,#210
ldr r2,[r7]
msr spsr_fsxc,r2
ldr lr,[r7,#4]
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_return_subs_irq_thumb
.type cpu_arm9_return_subs_irq_thumb, %function
cpu_arm9_return_subs_irq_thumb:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#210
mrs r2,spsr
str r2,[r7]
str lr,[r7,#4]
orr r0,r0,#255
msr spsr_fsxc,r0
adr r1,1f
str r1,[r7,#8]
adr r2,2f
add lr,r1,#4
subs pc,lr,#4
.thumb
.balign 4
1:
mov r0,pc
bx r2
.arm
.balign 4
2:
ldr r2,[r7,#8]
sub r0,r0,r2
mrs r1,cpsr
msr cpsr_c,#210
ldr r2,[r7]
msr spsr_fsxc,r2
ldr lr,[r7,#4]
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_return_ldm_irq_arm
.type cpu_arm9_return_ldm_irq_arm, %function
cpu_arm9_return_ldm_irq_arm:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#210
mrs r2,spsr
str r2,[r7]
str lr,[r7,#4]
orr r0,r0,#223
msr spsr_fsxc,r0
adr r1,1f
str r1,[r7,#8]
adr r2,2f
str r1,[r7,#12]
add r5,r7,#12
ldmia r5,{pc}^
.arm
.balign 4
1:
mov r0,#4
2:
mrs r1,cpsr
msr cpsr_c,#210
ldr r2,[r7]
msr spsr_fsxc,r2
ldr lr,[r7,#4]
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_return_ldm_irq_thumb
.type cpu_arm9_return_ldm_irq_thumb, %function
cpu_arm9_return_ldm_irq_thumb:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#210
mrs r2,spsr
str r2,[r7]
str lr,[r7,#4]
orr r0,r0,#255
msr spsr_fsxc,r0
adr r1,1f
str r1,[r7,#8]
adr r2,2f
str r1,[r7,#12]
add r5,r7,#12
ldmia r5,{pc}^
.thumb
.balign 4
1:
mov r0,pc
bx r2
.arm
.balign 4
2:
ldr r2,[r7,#8]
sub r0,r0,r2
mrs r1,cpsr
msr cpsr_c,#210
ldr r2,[r7]
msr spsr_fsxc,r2
ldr lr,[r7,#4]
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_return_movs_svc_arm
.type cpu_arm9_return_movs_svc_arm, %function
cpu_arm9_return_movs_svc_arm:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#211
mrs r2,spsr
str r2,[r7]
str lr,[r7,#4]
orr r0,r0,#223
msr spsr_fsxc,r0
adr r1,1f
str r1,[r7,#8]
adr r2,2f
mov lr,r1
movs pc,lr
.arm
.balign 4
1:
mov r0,#4
2:
mrs r1,cpsr
msr cpsr_c,#211
ldr r2,[r7]
msr spsr_fsxc,r2
ldr lr,[r7,#4]
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_return_movs_svc_thumb
.type cpu_arm9_return_movs_svc_thumb, %function
cpu_arm9_return_movs_svc_thumb:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#211
mrs r2,spsr
str r2,[r7]
str lr,[r7,#4]
orr r0,r0,#255
msr spsr_fsxc,r0
adr r1,1f
str r1,[r7,#8]
adr r2,2f
mov lr,r1
movs pc,lr
.thumb
.balign 4
1:
mov r0,pc
bx r2
.arm
.balign 4
2:
ldr r2,[r7,#8]
sub r0,r0,r2
mrs r1,cpsr
msr cpsr_c,#211
ldr r2,[r7]
msr spsr_fsxc,r2
ldr lr,[r7,#4]
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_return_subs_svc_arm
.type cpu_arm9_return_subs_svc_arm, %function
cpu_arm9_return_subs_svc_arm:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#211
mrs r2,spsr
str r2,[r7]
str lr,[r7,#4]
orr r0,r0,#223
msr spsr_fsxc,r0
adr r1,1f
str r1,[r7,#8]
adr r2,2f
add lr,r1,#4
subs pc,lr,#4
.arm
.balign 4
1:
mov r0,#4
2:
mrs r1,cpsr
msr cpsr_c,#211
ldr r2,[r7]
msr spsr_fsxc,r2
ldr lr,[r7,#4]
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_return_subs_svc_thumb
.type cpu_arm9_return_subs_svc_thumb, %function
cpu_arm9_return_subs_svc_thumb:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#211
mrs r2,spsr
str r2,[r7]
str lr,[r7,#4]
orr r0,r0,#255
msr spsr_fsxc,r0
adr r1,1f
str r1,[r7,#8]
adr r2,2f
add lr,r1,#4
subs pc,lr,#4
.thumb
.balign 4
1:
mov r0,pc
bx r2
.arm
.balign 4
2:
ldr r2,[r7,#8]
sub r0,r0,r2
mrs r1,cpsr
msr cpsr_c,#211
ldr r2,[r7]
msr spsr_fsxc,r2
ldr lr,[r7,#4]
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_return_ldm_svc_arm
.type cpu_arm9_return_ldm_svc_arm, %function
cpu_arm9_return_ldm_svc_arm:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#211
mrs r2,spsr
str r2,[r7]
str lr,[r7,#4]
orr r0,r0,#223
msr spsr_fsxc,r0
adr r1,1f
str r1,[r7,#8]
adr r2,2f
str r1,[r7,#12]
add r5,r7,#12
ldmia r5,{pc}^
.arm
.balign 4
1:
mov r0,#4
2:
mrs r1,cpsr
msr cpsr_c,#211
ldr r2,[r7]
msr spsr_fsxc,r2
ldr lr,[r7,#4]
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_return_ldm_svc_thumb
.type cpu_arm9_return_ldm_svc_thumb, %function
cpu_arm9_return_ldm_svc_thumb:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#211
mrs r2,spsr
str r2,[r7]
str lr,[r7,#4]
orr r0,r0,#255
msr spsr_fsxc,r0
adr r1,1f
str r1,[r7,#8]
adr r2,2f
str r1,[r7,#12]
add r5,r7,#12
ldmia r5,{pc}^
.thumb
.balign 4
1:
mov r0,pc
bx r2
.arm
.balign 4
2:
ldr r2,[r7,#8]
sub r0,r0,r2
mrs r1,cpsr
msr cpsr_c,#211
ldr r2,[r7]
msr spsr_fsxc,r2
ldr lr,[r7,#4]
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_return_movs_abt_arm
.type cpu_arm9_return_movs_abt_arm, %function
cpu_arm9_return_movs_abt_arm:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#215
mrs r2,spsr
str r2,[r7]
str lr,[r7,#4]
orr r0,r0,#223
msr spsr_fsxc,r0
adr r1,1f
str r1,[r7,#8]
adr r2,2f
mov lr,r1
movs pc,lr
.arm
.balign 4
1:
mov r0,#4
2:
mrs r1,cpsr
msr cpsr_c,#215
ldr r2,[r7]
msr spsr_fsxc,r2
ldr lr,[r7,#4]
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_return_movs_abt_thumb
.type cpu_arm9_return_movs_abt_thumb, %function
cpu_arm9_return_movs_abt_thumb:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#215
mrs r2,spsr
str r2,[r7]
str lr,[r7,#4]
orr r0,r0,#255
msr spsr_fsxc,r0
adr r1,1f
str r1,[r7,#8]
adr r2,2f
mov lr,r1
movs pc,lr
.thumb
.balign 4
1:
mov r0,pc
bx r2
.arm
.balign 4
2:
ldr r2,[r7,#8]
sub r0,r0,r2
mrs r1,cpsr
msr cpsr_c,#215
ldr r2,[r7]
msr spsr_fsxc,r2
ldr lr,[r7,#4]
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_return_subs_abt_arm
.type cpu_arm9_return_subs_abt_arm, %function
cpu_arm9_return_subs_abt_arm:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#215
mrs r2,spsr
str r2,[r7]
str lr,[r7,#4]
orr r0,r0,#223
msr spsr_fsxc,r0
adr r1,1f
str r1,[r7,#8]
adr r2,2f
add lr,r1,#4
subs pc,lr,#4
.arm
.balign 4
1:
mov r0,#4
2:
mrs r1,cpsr
msr cpsr_c,#215
ldr r2,[r7]
msr spsr_fsxc,r2
ldr lr,[r7,#4]
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_return_subs_abt_thumb
.type cpu_arm9_return_subs_abt_thumb, %function
cpu_arm9_return_subs_abt_thumb:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#215
mrs r2,spsr
str r2,[r7]
str lr,[r7,#4]
orr r0,r0,#255
msr spsr_fsxc,r0
adr r1,1f
str r1,[r7,#8]
adr r2,2f
add lr,r1,#4
subs pc,lr,#4
.thumb
.balign 4
1:
mov r0,pc
bx r2
.arm
.balign 4
2:
ldr r2,[r7,#8]
sub r0,r0,r2
mrs r1,cpsr
msr cpsr_c,#215
ldr r2,[r7]
msr spsr_fsxc,r2
ldr lr,[r7,#4]
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_return_ldm_abt_arm
.type cpu_arm9_return_ldm_abt_arm, %function
cpu_arm9_return_ldm_abt_arm:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#215
mrs r2,spsr
str r2,[r7]
str lr,[r7,#4]
orr r0,r0,#223
msr spsr_fsxc,r0
adr r1,1f
str r1,[r7,#8]
adr r2,2f
str r1,[r7,#12]
add r5,r7,#12
ldmia r5,{pc}^
.arm
.balign 4
1:
mov r0,#4
2:
mrs r1,cpsr
msr cpsr_c,#215
ldr r2,[r7]
msr spsr_fsxc,r2
ldr lr,[r7,#4]
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_return_ldm_abt_thumb
.type cpu_arm9_return_ldm_abt_thumb, %function
cpu_arm9_return_ldm_abt_thumb:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#215
mrs r2,spsr
str r2,[r7]
str lr,[r7,#4]
orr r0,r0,#255
msr spsr_fsxc,r0
adr r1,1f
str r1,[r7,#8]
adr r2,2f
str r1,[r7,#12]
add r5,r7,#12
ldmia r5,{pc}^
.thumb
.balign 4
1:
mov r0,pc
bx r2
.arm
.balign 4
2:
ldr r2,[r7,#8]
sub r0,r0,r2
mrs r1,cpsr
msr cpsr_c,#215
ldr r2,[r7]
msr spsr_fsxc,r2
ldr lr,[r7,#4]
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_return_movs_und_arm
.type cpu_arm9_return_movs_und_arm, %function
cpu_arm9_return_movs_und_arm:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#219
mrs r2,spsr
str r2,[r7]
str lr,[r7,#4]
orr r0,r0,#223
msr spsr_fsxc,r0
adr r1,1f
str r1,[r7,#8]
adr r2,2f
mov lr,r1
movs pc,lr
.arm
.balign 4
1:
mov r0,#4
2:
mrs r1,cpsr
msr cpsr_c,#219
ldr r2,[r7]
msr spsr_fsxc,r2
ldr lr,[r7,#4]
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_return_movs_und_thumb
.type cpu_arm9_return_movs_und_thumb, %function
cpu_arm9_return_movs_und_thumb:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#219
mrs r2,spsr
str r2,[r7]
str lr,[r7,#4]
orr r0,r0,#255
msr spsr_fsxc,r0
adr r1,1f
str r1,[r7,#8]
adr r2,2f
mov lr,r1
movs pc,lr
.thumb
.balign 4
1:
mov r0,pc
bx r2
.arm
.balign 4
2:
ldr r2,[r7,#8]
sub r0,r0,r2
mrs r1,cpsr
msr cpsr_c,#219
ldr r2,[r7]
msr spsr_fsxc,r2
ldr lr,[r7,#4]
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_return_subs_und_arm
.type cpu_arm9_return_subs_und_arm, %function
cpu_arm9_return_subs_und_arm:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#219
mrs r2,spsr
str r2,[r7]
str lr,[r7,#4]
orr r0,r0,#223
msr spsr_fsxc,r0
adr r1,1f
str r1,[r7,#8]
adr r2,2f
add lr,r1,#4
subs pc,lr,#4
.arm
.balign 4
1:
mov r0,#4
2:
mrs r1,cpsr
msr cpsr_c,#219
ldr r2,[r7]
msr spsr_fsxc,r2
ldr lr,[r7,#4]
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_return_subs_und_thumb
.type cpu_arm9_return_subs_und_thumb, %function
cpu_arm9_return_subs_und_thumb:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#219
mrs r2,spsr
str r2,[r7]
str lr,[r7,#4]
orr r0,r0,#255
msr spsr_fsxc,r0
adr r1,1f
str r1,[r7,#8]
adr r2,2f
add lr,r1,#4
subs pc,lr,#4
.thumb
.balign 4
1:
mov r0,pc
bx r2
.arm
.balign 4
2:
ldr r2,[r7,#8]
sub r0,r0,r2
mrs r1,cpsr
msr cpsr_c,#219
ldr r2,[r7]
msr spsr_fsxc,r2
ldr lr,[r7,#4]
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_return_ldm_und_arm
.type cpu_arm9_return_ldm_und_arm, %function
cpu_arm9_return_ldm_und_arm:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#219
mrs r2,spsr
str r2,[r7]
str lr,[r7,#4]
orr r0,r0,#223
msr spsr_fsxc,r0
adr r1,1f
str r1,[r7,#8]
adr r2,2f
str r1,[r7,#12]
add r5,r7,#12
ldmia r5,{pc}^
.arm
.balign 4
1:
mov r0,#4
2:
mrs r1,cpsr
msr cpsr_c,#219
ldr r2,[r7]
msr spsr_fsxc,r2
ldr lr,[r7,#4]
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_return_ldm_und_thumb
.type cpu_arm9_return_ldm_und_thumb, %function
cpu_arm9_return_ldm_und_thumb:
push {r4-r11,lr}
sub sp,sp,#20
mov r7,sp
mov r4,r3
mrs r6,cpsr
msr cpsr_c,#219
mrs r2,spsr
str r2,[r7]
str lr,[r7,#4]
orr r0,r0,#255
msr spsr_fsxc,r0
adr r1,1f
str r1,[r7,#8]
adr r2,2f
str r1,[r7,#12]
add r5,r7,#12
ldmia r5,{pc}^
.thumb
.balign 4
1:
mov r0,pc
bx r2
.arm
.balign 4
2:
ldr r2,[r7,#8]
sub r0,r0,r2
mrs r1,cpsr
msr cpsr_c,#219
ldr r2,[r7]
msr spsr_fsxc,r2
ldr lr,[r7,#4]
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#20
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_irq_entry_arm
.type cpu_arm9_irq_entry_arm, %function
cpu_arm9_irq_entry_arm:
push {r4-r11,lr}
sub sp,sp,#36
mov r7,sp
mov r4,r3
mrs r6,cpsr
str r0,[r7,#16]
orr r1,r6,#0xc0
msr cpsr_c,r1
ldr r5,=0x04000200
ldr r2,[r5,#8]
str r2,[r7]
mov r1,#0
str r1,[r5,#8]
ldr r2,[r5,#16]
str r2,[r7,#4]
mrc p15,0,r3,c9,c1,0
mov r3,r3,lsr #12
mov r3,r3,lsl #12
add r3,r3,#0x3f00
add r3,r3,#0xfc
ldr r2,[r3]
str r2,[r7,#8]
ldr r2,=cpu_arm9_timer_irq_handler
str r2,[r3]
ldr r3,=0x0400010c
ldr r2,[r3]
str r2,[r7,#12]
str r1,[r3]
ldr r8,=cpu_arm9_irq_capture
str r1,[r8]
ldr r2,=0xbaadc0de
str r2,[r8,#4]
str r2,[r8,#8]
mov r2,#64
str r2,[r5,#16]
str r2,[r5,#20]
ldr r0,[r7,#16]
mov r10,r0
orr r0,r0,#95
msr cpsr_fsxc,r0
mov r1,#1
str r1,[r5,#8]
ldr r2,=0x00c0ff80
ldr r8,=2f
b 9f
.ltorg
9:
str r2,[r3]
.rept 2048
nop
.endr
2:
mrs r0,cpsr
orr r0,r0,#0xc0
msr cpsr_c,r0
mov r1,#0
str r1,[r5,#8]
ldr r3,=0x0400010c
str r1,[r3]
mov r1,#64
str r1,[r5,#20]
ldr r2,[r7,#12]
str r2,[r3]
mrc p15,0,r3,c9,c1,0
mov r3,r3,lsr #12
mov r3,r3,lsl #12
add r3,r3,#0x3f00
add r3,r3,#0xfc
ldr r2,[r7,#8]
str r2,[r3]
ldr r2,[r7,#4]
str r2,[r5,#16]
ldr r2,[r7]
str r2,[r5,#8]
ldr r3,=cpu_arm9_irq_capture
ldr r0,[r3,#8]
ldr r2,=0xf00000ff
and r0,r0,r2
ldr r1,[r3,#4]
and r1,r1,#0xff
ldr r2,[r3]
eor r2,r2,#1
orr r1,r1,r2,lsl #16
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#36
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_irq_entry_thumb
.type cpu_arm9_irq_entry_thumb, %function
cpu_arm9_irq_entry_thumb:
push {r4-r11,lr}
sub sp,sp,#36
mov r7,sp
mov r4,r3
mrs r6,cpsr
str r0,[r7,#16]
orr r1,r6,#0xc0
msr cpsr_c,r1
ldr r5,=0x04000200
ldr r2,[r5,#8]
str r2,[r7]
mov r1,#0
str r1,[r5,#8]
ldr r2,[r5,#16]
str r2,[r7,#4]
mrc p15,0,r3,c9,c1,0
mov r3,r3,lsr #12
mov r3,r3,lsl #12
add r3,r3,#0x3f00
add r3,r3,#0xfc
ldr r2,[r3]
str r2,[r7,#8]
ldr r2,=cpu_arm9_timer_irq_handler
str r2,[r3]
ldr r3,=0x0400010c
ldr r2,[r3]
str r2,[r7,#12]
str r1,[r3]
ldr r8,=cpu_arm9_irq_capture
str r1,[r8]
ldr r2,=0xbaadc0de
str r2,[r8,#4]
str r2,[r8,#8]
mov r2,#64
str r2,[r5,#16]
str r2,[r5,#20]
ldr r0,[r7,#16]
mov r10,r0
orr r0,r0,#95
msr cpsr_fsxc,r0
mov r1,#1
str r1,[r5,#8]
ldr r2,=0x00c0ff80
ldr r8,=2f
b 9f
.ltorg
9:
adr r0,1f
orr r0,r0,#1
bx r0
.thumb
.balign 4
1:
str r2,[r3]
.rept 2048
nop
.endr
bx r8
.arm
.balign 4
2:
mrs r0,cpsr
orr r0,r0,#0xc0
msr cpsr_c,r0
mov r1,#0
str r1,[r5,#8]
ldr r3,=0x0400010c
str r1,[r3]
mov r1,#64
str r1,[r5,#20]
ldr r2,[r7,#12]
str r2,[r3]
mrc p15,0,r3,c9,c1,0
mov r3,r3,lsr #12
mov r3,r3,lsl #12
add r3,r3,#0x3f00
add r3,r3,#0xfc
ldr r2,[r7,#8]
str r2,[r3]
ldr r2,[r7,#4]
str r2,[r5,#16]
ldr r2,[r7]
str r2,[r5,#8]
ldr r3,=cpu_arm9_irq_capture
ldr r0,[r3,#8]
ldr r2,=0xf00000ff
and r0,r0,r2
ldr r1,[r3,#4]
and r1,r1,#0xff
ldr r2,[r3]
eor r2,r2,#1
orr r1,r1,r2,lsl #16
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#36
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_irq_entry_user
.type cpu_arm9_irq_entry_user, %function
cpu_arm9_irq_entry_user:
push {r4-r11,lr}
sub sp,sp,#36
mov r7,sp
mov r4,r3
mrs r6,cpsr
str r0,[r7,#16]
orr r1,r6,#0xc0
msr cpsr_c,r1
ldr r5,=0x04000200
ldr r2,[r5,#8]
str r2,[r7]
mov r1,#0
str r1,[r5,#8]
ldr r2,[r5,#16]
str r2,[r7,#4]
mrc p15,0,r3,c9,c1,0
mov r3,r3,lsr #12
mov r3,r3,lsl #12
add r3,r3,#0x3f00
add r3,r3,#0xfc
ldr r2,[r3]
str r2,[r7,#8]
ldr r2,=cpu_arm9_timer_irq_handler
str r2,[r3]
ldr r3,=0x0400010c
ldr r2,[r3]
str r2,[r7,#12]
str r1,[r3]
ldr r8,=cpu_arm9_irq_capture
str r1,[r8]
ldr r2,=0xbaadc0de
str r2,[r8,#4]
str r2,[r8,#8]
mov r2,#64
str r2,[r5,#16]
str r2,[r5,#20]
ldr r0,[r7,#16]
mov r10,r0
eor r0,r0,#0xf0000000
orr r0,r0,#95
msr cpsr_fsxc,r0
mov r1,#1
str r1,[r5,#8]
ldr r2,=0x00c0ff80
ldr r8,=2f
b 9f
.ltorg
9:
msr cpsr_c,#0x50
msr cpsr_f,r10
msr cpsr_c,#0xd3
sub r9,sp,r7
str r2,[r3]
.rept 2048
nop
.endr
2:
mrs r0,cpsr
orr r0,r0,#0xc0
msr cpsr_c,r0
mov r1,#0
str r1,[r5,#8]
ldr r3,=0x0400010c
str r1,[r3]
mov r1,#64
str r1,[r5,#20]
ldr r2,[r7,#12]
str r2,[r3]
mrc p15,0,r3,c9,c1,0
mov r3,r3,lsr #12
mov r3,r3,lsl #12
add r3,r3,#0x3f00
add r3,r3,#0xfc
ldr r2,[r7,#8]
str r2,[r3]
ldr r2,[r7,#4]
str r2,[r5,#16]
ldr r2,[r7]
str r2,[r5,#8]
ldr r3,=cpu_arm9_irq_capture
ldr r0,[r3,#8]
ldr r2,=0xf00000ff
and r0,r0,r2
ldr r1,[r3,#4]
and r1,r1,#0xff
ldr r2,[r3]
eor r2,r2,#1
orr r1,r1,r2,lsl #16
cmp r9,#0
movne r9,#0x20000
orr r1,r1,r9
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#36
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_irq_entry_masked_i
.type cpu_arm9_irq_entry_masked_i, %function
cpu_arm9_irq_entry_masked_i:
push {r4-r11,lr}
sub sp,sp,#36
mov r7,sp
mov r4,r3
mrs r6,cpsr
str r0,[r7,#16]
orr r1,r6,#0xc0
msr cpsr_c,r1
ldr r5,=0x04000200
ldr r2,[r5,#8]
str r2,[r7]
mov r1,#0
str r1,[r5,#8]
ldr r2,[r5,#16]
str r2,[r7,#4]
mrc p15,0,r3,c9,c1,0
mov r3,r3,lsr #12
mov r3,r3,lsl #12
add r3,r3,#0x3f00
add r3,r3,#0xfc
ldr r2,[r3]
str r2,[r7,#8]
ldr r2,=cpu_arm9_timer_irq_handler
str r2,[r3]
ldr r3,=0x0400010c
ldr r2,[r3]
str r2,[r7,#12]
str r1,[r3]
ldr r8,=cpu_arm9_irq_capture
str r1,[r8]
ldr r2,=0xbaadc0de
str r2,[r8,#4]
str r2,[r8,#8]
mov r2,#64
str r2,[r5,#16]
str r2,[r5,#20]
ldr r0,[r7,#16]
mov r10,r0
orr r0,r0,#223
msr cpsr_fsxc,r0
mov r1,#1
str r1,[r5,#8]
ldr r2,=0x00c0ff80
ldr r8,=2f
b 9f
.ltorg
9:
str r2,[r3]
.rept 2048
nop
.endr
ldr r0,=cpu_arm9_irq_capture
ldr r11,[r0]
mrs r0,cpsr
bic r0,r0,#0x80
msr cpsr_c,r0
b 8f
.ltorg
8:
.rept 2048
nop
.endr
2:
mrs r0,cpsr
orr r0,r0,#0xc0
msr cpsr_c,r0
mov r1,#0
str r1,[r5,#8]
ldr r3,=0x0400010c
str r1,[r3]
mov r1,#64
str r1,[r5,#20]
ldr r2,[r7,#12]
str r2,[r3]
mrc p15,0,r3,c9,c1,0
mov r3,r3,lsr #12
mov r3,r3,lsl #12
add r3,r3,#0x3f00
add r3,r3,#0xfc
ldr r2,[r7,#8]
str r2,[r3]
ldr r2,[r7,#4]
str r2,[r5,#16]
ldr r2,[r7]
str r2,[r5,#8]
ldr r3,=cpu_arm9_irq_capture
ldr r0,[r3,#8]
ldr r2,=0xf00000ff
and r0,r0,r2
ldr r1,[r3,#4]
and r1,r1,#0xff
ldr r2,[r3]
eor r2,r2,#1
orr r1,r1,r2,lsl #16
cmp r11,#0
movne r11,#0x40000
orr r1,r1,r11
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#36
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_irq_entry_masked_ime
.type cpu_arm9_irq_entry_masked_ime, %function
cpu_arm9_irq_entry_masked_ime:
push {r4-r11,lr}
sub sp,sp,#36
mov r7,sp
mov r4,r3
mrs r6,cpsr
str r0,[r7,#16]
orr r1,r6,#0xc0
msr cpsr_c,r1
ldr r5,=0x04000200
ldr r2,[r5,#8]
str r2,[r7]
mov r1,#0
str r1,[r5,#8]
ldr r2,[r5,#16]
str r2,[r7,#4]
mrc p15,0,r3,c9,c1,0
mov r3,r3,lsr #12
mov r3,r3,lsl #12
add r3,r3,#0x3f00
add r3,r3,#0xfc
ldr r2,[r3]
str r2,[r7,#8]
ldr r2,=cpu_arm9_timer_irq_handler
str r2,[r3]
ldr r3,=0x0400010c
ldr r2,[r3]
str r2,[r7,#12]
str r1,[r3]
ldr r8,=cpu_arm9_irq_capture
str r1,[r8]
ldr r2,=0xbaadc0de
str r2,[r8,#4]
str r2,[r8,#8]
mov r2,#64
str r2,[r5,#16]
str r2,[r5,#20]
ldr r0,[r7,#16]
mov r10,r0
orr r0,r0,#95
msr cpsr_fsxc,r0
mov r1,#0
str r1,[r5,#8]
ldr r2,=0x00c0ff80
ldr r8,=2f
b 9f
.ltorg
9:
str r2,[r3]
.rept 2048
nop
.endr
ldr r0,=cpu_arm9_irq_capture
ldr r11,[r0]
mov r0,#1
str r0,[r5,#8]
b 8f
.ltorg
8:
.rept 2048
nop
.endr
2:
mrs r0,cpsr
orr r0,r0,#0xc0
msr cpsr_c,r0
mov r1,#0
str r1,[r5,#8]
ldr r3,=0x0400010c
str r1,[r3]
mov r1,#64
str r1,[r5,#20]
ldr r2,[r7,#12]
str r2,[r3]
mrc p15,0,r3,c9,c1,0
mov r3,r3,lsr #12
mov r3,r3,lsl #12
add r3,r3,#0x3f00
add r3,r3,#0xfc
ldr r2,[r7,#8]
str r2,[r3]
ldr r2,[r7,#4]
str r2,[r5,#16]
ldr r2,[r7]
str r2,[r5,#8]
ldr r3,=cpu_arm9_irq_capture
ldr r0,[r3,#8]
ldr r2,=0xf00000ff
and r0,r0,r2
ldr r1,[r3,#4]
and r1,r1,#0xff
ldr r2,[r3]
eor r2,r2,#1
orr r1,r1,r2,lsl #16
cmp r11,#0
movne r11,#0x40000
orr r1,r1,r11
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#36
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_irq_entry_masked_ie
.type cpu_arm9_irq_entry_masked_ie, %function
cpu_arm9_irq_entry_masked_ie:
push {r4-r11,lr}
sub sp,sp,#36
mov r7,sp
mov r4,r3
mrs r6,cpsr
str r0,[r7,#16]
orr r1,r6,#0xc0
msr cpsr_c,r1
ldr r5,=0x04000200
ldr r2,[r5,#8]
str r2,[r7]
mov r1,#0
str r1,[r5,#8]
ldr r2,[r5,#16]
str r2,[r7,#4]
mrc p15,0,r3,c9,c1,0
mov r3,r3,lsr #12
mov r3,r3,lsl #12
add r3,r3,#0x3f00
add r3,r3,#0xfc
ldr r2,[r3]
str r2,[r7,#8]
ldr r2,=cpu_arm9_timer_irq_handler
str r2,[r3]
ldr r3,=0x0400010c
ldr r2,[r3]
str r2,[r7,#12]
str r1,[r3]
ldr r8,=cpu_arm9_irq_capture
str r1,[r8]
ldr r2,=0xbaadc0de
str r2,[r8,#4]
str r2,[r8,#8]
mov r2,#64
str r2,[r5,#16]
str r2,[r5,#20]
mov r1,#0
str r1,[r5,#16]
ldr r0,[r7,#16]
mov r10,r0
orr r0,r0,#95
msr cpsr_fsxc,r0
mov r1,#1
str r1,[r5,#8]
ldr r2,=0x00c0ff80
ldr r8,=2f
b 9f
.ltorg
9:
str r2,[r3]
.rept 2048
nop
.endr
ldr r0,=cpu_arm9_irq_capture
ldr r11,[r0]
mov r0,#64
str r0,[r5,#16]
b 8f
.ltorg
8:
.rept 2048
nop
.endr
2:
mrs r0,cpsr
orr r0,r0,#0xc0
msr cpsr_c,r0
mov r1,#0
str r1,[r5,#8]
ldr r3,=0x0400010c
str r1,[r3]
mov r1,#64
str r1,[r5,#20]
ldr r2,[r7,#12]
str r2,[r3]
mrc p15,0,r3,c9,c1,0
mov r3,r3,lsr #12
mov r3,r3,lsl #12
add r3,r3,#0x3f00
add r3,r3,#0xfc
ldr r2,[r7,#8]
str r2,[r3]
ldr r2,[r7,#4]
str r2,[r5,#16]
ldr r2,[r7]
str r2,[r5,#8]
ldr r3,=cpu_arm9_irq_capture
ldr r0,[r3,#8]
ldr r2,=0xf00000ff
and r0,r0,r2
ldr r1,[r3,#4]
and r1,r1,#0xff
ldr r2,[r3]
eor r2,r2,#1
orr r1,r1,r2,lsl #16
cmp r11,#0
movne r11,#0x40000
orr r1,r1,r11
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#36
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_timer_irq_handler
cpu_arm9_timer_irq_handler:
ldr r0,=0x0400010c
mov r1,#0
strh r1,[r0,#2]
ldr r0,=0x04000214
mov r1,#64
str r1,[r0]
ldr r0,=cpu_arm9_irq_capture
mrs r1,cpsr
str r1,[r0,#4]
mrs r1,spsr
str r1,[r0,#8]
and r2,r1,#0x1f
cmp r2,#0x10
bne 1f
bic r1,r1,#0x1f
orr r1,r1,#0x1f
msr spsr_c,r1
1:
ldr r1,[r0]
add r1,r1,#1
str r1,[r0]
bx lr
.ltorg
.bss
.balign 4
cpu_arm9_irq_capture:
.space 12
.text
.arm
.balign 4
.global cpu_arm9_cp15_dcacheable
.type cpu_arm9_cp15_dcacheable, %function
cpu_arm9_cp15_dcacheable:
push {r4-r11,lr}
mov r4,r3
mrs r6,cpsr
orr r2,r6,#0xc0
msr cpsr_c,r2
mrc p15,0,r7,c1,c0,0
bic r2,r7,#1
mcr p15,0,r2,c1,c0,0
mrc p15,0,r8,c2,c0,0
mcr p15,0,r0,c2,c0,0
mrc p15,0,r0,c2,c0,0
mcr p15,0,r8,c2,c0,0
mrc p15,0,r1,c2,c0,0
eor r1,r1,r8
and r1,r1,#0xff
and r0,r0,#0xff
mcr p15,0,r7,c1,c0,0
msr cpsr_fsxc,r6
str r0,[r4]
str r1,[r4,#4]
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_cp15_icacheable
.type cpu_arm9_cp15_icacheable, %function
cpu_arm9_cp15_icacheable:
push {r4-r11,lr}
mov r4,r3
mrs r6,cpsr
orr r2,r6,#0xc0
msr cpsr_c,r2
mrc p15,0,r7,c1,c0,0
bic r2,r7,#1
mcr p15,0,r2,c1,c0,0
mrc p15,0,r8,c2,c0,1
mcr p15,0,r0,c2,c0,1
mrc p15,0,r0,c2,c0,1
mcr p15,0,r8,c2,c0,1
mrc p15,0,r1,c2,c0,1
eor r1,r1,r8
and r1,r1,#0xff
and r0,r0,#0xff
mcr p15,0,r7,c1,c0,0
msr cpsr_fsxc,r6
str r0,[r4]
str r1,[r4,#4]
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_cp15_bufferable
.type cpu_arm9_cp15_bufferable, %function
cpu_arm9_cp15_bufferable:
push {r4-r11,lr}
mov r4,r3
mrs r6,cpsr
orr r2,r6,#0xc0
msr cpsr_c,r2
mrc p15,0,r7,c1,c0,0
bic r2,r7,#1
mcr p15,0,r2,c1,c0,0
mrc p15,0,r8,c3,c0,0
mcr p15,0,r0,c3,c0,0
mrc p15,0,r0,c3,c0,0
mcr p15,0,r8,c3,c0,0
mrc p15,0,r1,c3,c0,0
eor r1,r1,r8
and r1,r1,#0xff
and r0,r0,#0xff
mcr p15,0,r7,c1,c0,0
msr cpsr_fsxc,r6
str r0,[r4]
str r1,[r4,#4]
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_cp15_region0_alias0
.type cpu_arm9_cp15_region0_alias0, %function
cpu_arm9_cp15_region0_alias0:
push {r4-r11,lr}
mov r4,r3
mrs r6,cpsr
orr r2,r6,#0xc0
msr cpsr_c,r2
mrc p15,0,r7,c1,c0,0
bic r2,r7,#1
mcr p15,0,r2,c1,c0,0
mrc p15,0,r8,c6,c0,0
mrc p15,0,r9,c6,c0,1
mcr p15,0,r0,c6,c0,0
mrc p15,0,r0,c6,c0,1
mcr p15,0,r8,c6,c0,0
mcr p15,0,r9,c6,c0,1
mrc p15,0,r1,c6,c0,0
eor r1,r1,r8
mrc p15,0,r2,c6,c0,1
eor r2,r2,r9
orr r1,r1,r2
mcr p15,0,r7,c1,c0,0
msr cpsr_fsxc,r6
str r0,[r4]
str r1,[r4,#4]
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_cp15_region0_alias1
.type cpu_arm9_cp15_region0_alias1, %function
cpu_arm9_cp15_region0_alias1:
push {r4-r11,lr}
mov r4,r3
mrs r6,cpsr
orr r2,r6,#0xc0
msr cpsr_c,r2
mrc p15,0,r7,c1,c0,0
bic r2,r7,#1
mcr p15,0,r2,c1,c0,0
mrc p15,0,r8,c6,c0,0
mrc p15,0,r9,c6,c0,1
mcr p15,0,r0,c6,c0,1
mrc p15,0,r0,c6,c0,0
mcr p15,0,r8,c6,c0,0
mcr p15,0,r9,c6,c0,1
mrc p15,0,r1,c6,c0,0
eor r1,r1,r8
mrc p15,0,r2,c6,c0,1
eor r2,r2,r9
orr r1,r1,r2
mcr p15,0,r7,c1,c0,0
msr cpsr_fsxc,r6
str r0,[r4]
str r1,[r4,#4]
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_cp15_region1_alias0
.type cpu_arm9_cp15_region1_alias0, %function
cpu_arm9_cp15_region1_alias0:
push {r4-r11,lr}
mov r4,r3
mrs r6,cpsr
orr r2,r6,#0xc0
msr cpsr_c,r2
mrc p15,0,r7,c1,c0,0
bic r2,r7,#1
mcr p15,0,r2,c1,c0,0
mrc p15,0,r8,c6,c1,0
mrc p15,0,r9,c6,c1,1
mcr p15,0,r0,c6,c1,0
mrc p15,0,r0,c6,c1,1
mcr p15,0,r8,c6,c1,0
mcr p15,0,r9,c6,c1,1
mrc p15,0,r1,c6,c1,0
eor r1,r1,r8
mrc p15,0,r2,c6,c1,1
eor r2,r2,r9
orr r1,r1,r2
mcr p15,0,r7,c1,c0,0
msr cpsr_fsxc,r6
str r0,[r4]
str r1,[r4,#4]
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_cp15_region1_alias1
.type cpu_arm9_cp15_region1_alias1, %function
cpu_arm9_cp15_region1_alias1:
push {r4-r11,lr}
mov r4,r3
mrs r6,cpsr
orr r2,r6,#0xc0
msr cpsr_c,r2
mrc p15,0,r7,c1,c0,0
bic r2,r7,#1
mcr p15,0,r2,c1,c0,0
mrc p15,0,r8,c6,c1,0
mrc p15,0,r9,c6,c1,1
mcr p15,0,r0,c6,c1,1
mrc p15,0,r0,c6,c1,0
mcr p15,0,r8,c6,c1,0
mcr p15,0,r9,c6,c1,1
mrc p15,0,r1,c6,c1,0
eor r1,r1,r8
mrc p15,0,r2,c6,c1,1
eor r2,r2,r9
orr r1,r1,r2
mcr p15,0,r7,c1,c0,0
msr cpsr_fsxc,r6
str r0,[r4]
str r1,[r4,#4]
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_cp15_region2_alias0
.type cpu_arm9_cp15_region2_alias0, %function
cpu_arm9_cp15_region2_alias0:
push {r4-r11,lr}
mov r4,r3
mrs r6,cpsr
orr r2,r6,#0xc0
msr cpsr_c,r2
mrc p15,0,r7,c1,c0,0
bic r2,r7,#1
mcr p15,0,r2,c1,c0,0
mrc p15,0,r8,c6,c2,0
mrc p15,0,r9,c6,c2,1
mcr p15,0,r0,c6,c2,0
mrc p15,0,r0,c6,c2,1
mcr p15,0,r8,c6,c2,0
mcr p15,0,r9,c6,c2,1
mrc p15,0,r1,c6,c2,0
eor r1,r1,r8
mrc p15,0,r2,c6,c2,1
eor r2,r2,r9
orr r1,r1,r2
mcr p15,0,r7,c1,c0,0
msr cpsr_fsxc,r6
str r0,[r4]
str r1,[r4,#4]
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_cp15_region2_alias1
.type cpu_arm9_cp15_region2_alias1, %function
cpu_arm9_cp15_region2_alias1:
push {r4-r11,lr}
mov r4,r3
mrs r6,cpsr
orr r2,r6,#0xc0
msr cpsr_c,r2
mrc p15,0,r7,c1,c0,0
bic r2,r7,#1
mcr p15,0,r2,c1,c0,0
mrc p15,0,r8,c6,c2,0
mrc p15,0,r9,c6,c2,1
mcr p15,0,r0,c6,c2,1
mrc p15,0,r0,c6,c2,0
mcr p15,0,r8,c6,c2,0
mcr p15,0,r9,c6,c2,1
mrc p15,0,r1,c6,c2,0
eor r1,r1,r8
mrc p15,0,r2,c6,c2,1
eor r2,r2,r9
orr r1,r1,r2
mcr p15,0,r7,c1,c0,0
msr cpsr_fsxc,r6
str r0,[r4]
str r1,[r4,#4]
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_cp15_region3_alias0
.type cpu_arm9_cp15_region3_alias0, %function
cpu_arm9_cp15_region3_alias0:
push {r4-r11,lr}
mov r4,r3
mrs r6,cpsr
orr r2,r6,#0xc0
msr cpsr_c,r2
mrc p15,0,r7,c1,c0,0
bic r2,r7,#1
mcr p15,0,r2,c1,c0,0
mrc p15,0,r8,c6,c3,0
mrc p15,0,r9,c6,c3,1
mcr p15,0,r0,c6,c3,0
mrc p15,0,r0,c6,c3,1
mcr p15,0,r8,c6,c3,0
mcr p15,0,r9,c6,c3,1
mrc p15,0,r1,c6,c3,0
eor r1,r1,r8
mrc p15,0,r2,c6,c3,1
eor r2,r2,r9
orr r1,r1,r2
mcr p15,0,r7,c1,c0,0
msr cpsr_fsxc,r6
str r0,[r4]
str r1,[r4,#4]
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_cp15_region3_alias1
.type cpu_arm9_cp15_region3_alias1, %function
cpu_arm9_cp15_region3_alias1:
push {r4-r11,lr}
mov r4,r3
mrs r6,cpsr
orr r2,r6,#0xc0
msr cpsr_c,r2
mrc p15,0,r7,c1,c0,0
bic r2,r7,#1
mcr p15,0,r2,c1,c0,0
mrc p15,0,r8,c6,c3,0
mrc p15,0,r9,c6,c3,1
mcr p15,0,r0,c6,c3,1
mrc p15,0,r0,c6,c3,0
mcr p15,0,r8,c6,c3,0
mcr p15,0,r9,c6,c3,1
mrc p15,0,r1,c6,c3,0
eor r1,r1,r8
mrc p15,0,r2,c6,c3,1
eor r2,r2,r9
orr r1,r1,r2
mcr p15,0,r7,c1,c0,0
msr cpsr_fsxc,r6
str r0,[r4]
str r1,[r4,#4]
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_cp15_region4_alias0
.type cpu_arm9_cp15_region4_alias0, %function
cpu_arm9_cp15_region4_alias0:
push {r4-r11,lr}
mov r4,r3
mrs r6,cpsr
orr r2,r6,#0xc0
msr cpsr_c,r2
mrc p15,0,r7,c1,c0,0
bic r2,r7,#1
mcr p15,0,r2,c1,c0,0
mrc p15,0,r8,c6,c4,0
mrc p15,0,r9,c6,c4,1
mcr p15,0,r0,c6,c4,0
mrc p15,0,r0,c6,c4,1
mcr p15,0,r8,c6,c4,0
mcr p15,0,r9,c6,c4,1
mrc p15,0,r1,c6,c4,0
eor r1,r1,r8
mrc p15,0,r2,c6,c4,1
eor r2,r2,r9
orr r1,r1,r2
mcr p15,0,r7,c1,c0,0
msr cpsr_fsxc,r6
str r0,[r4]
str r1,[r4,#4]
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_cp15_region4_alias1
.type cpu_arm9_cp15_region4_alias1, %function
cpu_arm9_cp15_region4_alias1:
push {r4-r11,lr}
mov r4,r3
mrs r6,cpsr
orr r2,r6,#0xc0
msr cpsr_c,r2
mrc p15,0,r7,c1,c0,0
bic r2,r7,#1
mcr p15,0,r2,c1,c0,0
mrc p15,0,r8,c6,c4,0
mrc p15,0,r9,c6,c4,1
mcr p15,0,r0,c6,c4,1
mrc p15,0,r0,c6,c4,0
mcr p15,0,r8,c6,c4,0
mcr p15,0,r9,c6,c4,1
mrc p15,0,r1,c6,c4,0
eor r1,r1,r8
mrc p15,0,r2,c6,c4,1
eor r2,r2,r9
orr r1,r1,r2
mcr p15,0,r7,c1,c0,0
msr cpsr_fsxc,r6
str r0,[r4]
str r1,[r4,#4]
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_cp15_region5_alias0
.type cpu_arm9_cp15_region5_alias0, %function
cpu_arm9_cp15_region5_alias0:
push {r4-r11,lr}
mov r4,r3
mrs r6,cpsr
orr r2,r6,#0xc0
msr cpsr_c,r2
mrc p15,0,r7,c1,c0,0
bic r2,r7,#1
mcr p15,0,r2,c1,c0,0
mrc p15,0,r8,c6,c5,0
mrc p15,0,r9,c6,c5,1
mcr p15,0,r0,c6,c5,0
mrc p15,0,r0,c6,c5,1
mcr p15,0,r8,c6,c5,0
mcr p15,0,r9,c6,c5,1
mrc p15,0,r1,c6,c5,0
eor r1,r1,r8
mrc p15,0,r2,c6,c5,1
eor r2,r2,r9
orr r1,r1,r2
mcr p15,0,r7,c1,c0,0
msr cpsr_fsxc,r6
str r0,[r4]
str r1,[r4,#4]
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_cp15_region5_alias1
.type cpu_arm9_cp15_region5_alias1, %function
cpu_arm9_cp15_region5_alias1:
push {r4-r11,lr}
mov r4,r3
mrs r6,cpsr
orr r2,r6,#0xc0
msr cpsr_c,r2
mrc p15,0,r7,c1,c0,0
bic r2,r7,#1
mcr p15,0,r2,c1,c0,0
mrc p15,0,r8,c6,c5,0
mrc p15,0,r9,c6,c5,1
mcr p15,0,r0,c6,c5,1
mrc p15,0,r0,c6,c5,0
mcr p15,0,r8,c6,c5,0
mcr p15,0,r9,c6,c5,1
mrc p15,0,r1,c6,c5,0
eor r1,r1,r8
mrc p15,0,r2,c6,c5,1
eor r2,r2,r9
orr r1,r1,r2
mcr p15,0,r7,c1,c0,0
msr cpsr_fsxc,r6
str r0,[r4]
str r1,[r4,#4]
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_cp15_region6_alias0
.type cpu_arm9_cp15_region6_alias0, %function
cpu_arm9_cp15_region6_alias0:
push {r4-r11,lr}
mov r4,r3
mrs r6,cpsr
orr r2,r6,#0xc0
msr cpsr_c,r2
mrc p15,0,r7,c1,c0,0
bic r2,r7,#1
mcr p15,0,r2,c1,c0,0
mrc p15,0,r8,c6,c6,0
mrc p15,0,r9,c6,c6,1
mcr p15,0,r0,c6,c6,0
mrc p15,0,r0,c6,c6,1
mcr p15,0,r8,c6,c6,0
mcr p15,0,r9,c6,c6,1
mrc p15,0,r1,c6,c6,0
eor r1,r1,r8
mrc p15,0,r2,c6,c6,1
eor r2,r2,r9
orr r1,r1,r2
mcr p15,0,r7,c1,c0,0
msr cpsr_fsxc,r6
str r0,[r4]
str r1,[r4,#4]
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_cp15_region6_alias1
.type cpu_arm9_cp15_region6_alias1, %function
cpu_arm9_cp15_region6_alias1:
push {r4-r11,lr}
mov r4,r3
mrs r6,cpsr
orr r2,r6,#0xc0
msr cpsr_c,r2
mrc p15,0,r7,c1,c0,0
bic r2,r7,#1
mcr p15,0,r2,c1,c0,0
mrc p15,0,r8,c6,c6,0
mrc p15,0,r9,c6,c6,1
mcr p15,0,r0,c6,c6,1
mrc p15,0,r0,c6,c6,0
mcr p15,0,r8,c6,c6,0
mcr p15,0,r9,c6,c6,1
mrc p15,0,r1,c6,c6,0
eor r1,r1,r8
mrc p15,0,r2,c6,c6,1
eor r2,r2,r9
orr r1,r1,r2
mcr p15,0,r7,c1,c0,0
msr cpsr_fsxc,r6
str r0,[r4]
str r1,[r4,#4]
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_cp15_region7_alias0
.type cpu_arm9_cp15_region7_alias0, %function
cpu_arm9_cp15_region7_alias0:
push {r4-r11,lr}
mov r4,r3
mrs r6,cpsr
orr r2,r6,#0xc0
msr cpsr_c,r2
mrc p15,0,r7,c1,c0,0
bic r2,r7,#1
mcr p15,0,r2,c1,c0,0
mrc p15,0,r8,c6,c7,0
mrc p15,0,r9,c6,c7,1
mcr p15,0,r0,c6,c7,0
mrc p15,0,r0,c6,c7,1
mcr p15,0,r8,c6,c7,0
mcr p15,0,r9,c6,c7,1
mrc p15,0,r1,c6,c7,0
eor r1,r1,r8
mrc p15,0,r2,c6,c7,1
eor r2,r2,r9
orr r1,r1,r2
mcr p15,0,r7,c1,c0,0
msr cpsr_fsxc,r6
str r0,[r4]
str r1,[r4,#4]
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_cp15_region7_alias1
.type cpu_arm9_cp15_region7_alias1, %function
cpu_arm9_cp15_region7_alias1:
push {r4-r11,lr}
mov r4,r3
mrs r6,cpsr
orr r2,r6,#0xc0
msr cpsr_c,r2
mrc p15,0,r7,c1,c0,0
bic r2,r7,#1
mcr p15,0,r2,c1,c0,0
mrc p15,0,r8,c6,c7,0
mrc p15,0,r9,c6,c7,1
mcr p15,0,r0,c6,c7,1
mrc p15,0,r0,c6,c7,0
mcr p15,0,r8,c6,c7,0
mcr p15,0,r9,c6,c7,1
mrc p15,0,r1,c6,c7,0
eor r1,r1,r8
mrc p15,0,r2,c6,c7,1
eor r2,r2,r9
orr r1,r1,r2
mcr p15,0,r7,c1,c0,0
msr cpsr_fsxc,r6
str r0,[r4]
str r1,[r4,#4]
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_cp15_data_ap_short_to_ext
.type cpu_arm9_cp15_data_ap_short_to_ext, %function
cpu_arm9_cp15_data_ap_short_to_ext:
push {r4-r11,lr}
mov r4,r3
mrs r6,cpsr
orr r2,r6,#0xc0
msr cpsr_c,r2
mrc p15,0,r7,c1,c0,0
bic r2,r7,#1
mcr p15,0,r2,c1,c0,0
mrc p15,0,r8,c5,c0,0
mrc p15,0,r9,c5,c0,2
mov r2,#0
mcr p15,0,r2,c5,c0,0
mcr p15,0,r2,c5,c0,2
mcr p15,0,r0,c5,c0,0
mrc p15,0,r0,c5,c0,2
mcr p15,0,r8,c5,c0,0
mcr p15,0,r9,c5,c0,2
mrc p15,0,r1,c5,c0,0
eor r1,r1,r8
mrc p15,0,r2,c5,c0,2
eor r2,r2,r9
orr r1,r1,r2
mcr p15,0,r7,c1,c0,0
msr cpsr_fsxc,r6
str r0,[r4]
str r1,[r4,#4]
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_cp15_data_ap_ext_to_short
.type cpu_arm9_cp15_data_ap_ext_to_short, %function
cpu_arm9_cp15_data_ap_ext_to_short:
push {r4-r11,lr}
mov r4,r3
mrs r6,cpsr
orr r2,r6,#0xc0
msr cpsr_c,r2
mrc p15,0,r7,c1,c0,0
bic r2,r7,#1
mcr p15,0,r2,c1,c0,0
mrc p15,0,r8,c5,c0,0
mrc p15,0,r9,c5,c0,2
mov r2,#0
mcr p15,0,r2,c5,c0,0
mcr p15,0,r2,c5,c0,2
mcr p15,0,r0,c5,c0,2
mrc p15,0,r0,c5,c0,0
mcr p15,0,r8,c5,c0,0
mcr p15,0,r9,c5,c0,2
mrc p15,0,r1,c5,c0,0
eor r1,r1,r8
mrc p15,0,r2,c5,c0,2
eor r2,r2,r9
orr r1,r1,r2
mcr p15,0,r7,c1,c0,0
msr cpsr_fsxc,r6
str r0,[r4]
str r1,[r4,#4]
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_cp15_code_ap_short_to_ext
.type cpu_arm9_cp15_code_ap_short_to_ext, %function
cpu_arm9_cp15_code_ap_short_to_ext:
push {r4-r11,lr}
mov r4,r3
mrs r6,cpsr
orr r2,r6,#0xc0
msr cpsr_c,r2
mrc p15,0,r7,c1,c0,0
bic r2,r7,#1
mcr p15,0,r2,c1,c0,0
mrc p15,0,r8,c5,c0,1
mrc p15,0,r9,c5,c0,3
mov r2,#0
mcr p15,0,r2,c5,c0,1
mcr p15,0,r2,c5,c0,3
mcr p15,0,r0,c5,c0,1
mrc p15,0,r0,c5,c0,3
mcr p15,0,r8,c5,c0,1
mcr p15,0,r9,c5,c0,3
mrc p15,0,r1,c5,c0,1
eor r1,r1,r8
mrc p15,0,r2,c5,c0,3
eor r2,r2,r9
orr r1,r1,r2
mcr p15,0,r7,c1,c0,0
msr cpsr_fsxc,r6
str r0,[r4]
str r1,[r4,#4]
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_cp15_code_ap_ext_to_short
.type cpu_arm9_cp15_code_ap_ext_to_short, %function
cpu_arm9_cp15_code_ap_ext_to_short:
push {r4-r11,lr}
mov r4,r3
mrs r6,cpsr
orr r2,r6,#0xc0
msr cpsr_c,r2
mrc p15,0,r7,c1,c0,0
bic r2,r7,#1
mcr p15,0,r2,c1,c0,0
mrc p15,0,r8,c5,c0,1
mrc p15,0,r9,c5,c0,3
mov r2,#0
mcr p15,0,r2,c5,c0,1
mcr p15,0,r2,c5,c0,3
mcr p15,0,r0,c5,c0,3
mrc p15,0,r0,c5,c0,1
mcr p15,0,r8,c5,c0,1
mcr p15,0,r9,c5,c0,3
mrc p15,0,r1,c5,c0,1
eor r1,r1,r8
mrc p15,0,r2,c5,c0,3
eor r2,r2,r9
orr r1,r1,r2
mcr p15,0,r7,c1,c0,0
msr cpsr_fsxc,r6
str r0,[r4]
str r1,[r4,#4]
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_raw_swi_arm_entry
.type cpu_arm9_raw_swi_arm_entry, %function
cpu_arm9_raw_swi_arm_entry:
push {r4-r11,lr}
sub sp,sp,#44
mov r7,sp
mov r4,r3
mrs r6,cpsr
str r0,[r7,#8]
orr r2,r6,#0xc0
msr cpsr_c,r2
msr cpsr_c,#211
mrs r2,spsr
str r2,[r7]
str lr,[r7,#4]
orr r2,r6,#0xc0
msr cpsr_c,r2
mrc p15,0,r8,c1,c0,0
mov r9,#0
mov r2,#0x0c
nop
bic r2,r8,#0x2000
bic r2,r2,#0x1000
bic r2,r2,#5
bic r2,r2,#0x80000
orr r2,r2,#0x40000
mcr p15,0,r2,c1,c0,0
mov r5,#8
ldr r10,[r5]
ldr r11,[r5,#4]
ldr r2,=0xe51ff004
str r2,[r5]
ldr r2,=cpu_arm9_raw_swi_handler
str r2,[r5,#4]
mov r2,#0
mcr p15,0,r2,c7,c10,4
mcr p15,0,r2,c7,c5,0
ldr r3,=cpu_arm9_exception_capture
str r2,[r3]
ldr r2,=0xbaadc0de
str r2,[r3,#4]
str r2,[r3,#8]
str r2,[r3,#12]
adr r2,1f
str r2,[r7,#20]
adr r5,2f
ldr r0,[r7,#8]
orr r0,r0,#0xdf
msr cpsr_fsxc,r0
1:
svc #0x0d0000
2:
mrs r5,cpsr
str r5,[r7,#16]
mov r5,#8
str r10,[r5]
str r11,[r5,#4]
ldr r0,[r5]
eor r0,r0,r10
ldr r1,[r5,#4]
eor r1,r1,r11
orr r0,r0,r1
str r0,[r7,#24]
mov r2,#0
mcr p15,0,r2,c7,c10,4
mcr p15,0,r2,c7,c5,0
nop
mcr p15,0,r8,c1,c0,0
mrc p15,0,r2,c1,c0,0
eor r2,r2,r8
str r2,[r7,#12]
mov r2,#0
eor r2,r2,r9
ldr r3,[r7,#12]
orr r2,r2,r3
str r2,[r7,#12]
msr cpsr_c,#211
ldr r2,[r7]
msr spsr_fsxc,r2
ldr lr,[r7,#4]
orr r2,r6,#0xc0
msr cpsr_c,r2
ldr r3,=cpu_arm9_exception_capture
ldr r2,=0xf00000ff
ldr r0,[r3,#4]
and r0,r0,r2
ldr r1,[r3,#8]
and r1,r1,r2
ldr r2,[r3]
cmp r2,#1
orrne r1,r1,#0x100
ldr r2,[r7,#12]
cmp r2,#0
orrne r1,r1,#0x400
ldr r2,[r7,#24]
cmp r2,#0
orrne r1,r1,#0x800
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#44
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_raw_swi_arm_link_return
.type cpu_arm9_raw_swi_arm_link_return, %function
cpu_arm9_raw_swi_arm_link_return:
push {r4-r11,lr}
sub sp,sp,#44
mov r7,sp
mov r4,r3
mrs r6,cpsr
str r0,[r7,#8]
orr r2,r6,#0xc0
msr cpsr_c,r2
msr cpsr_c,#211
mrs r2,spsr
str r2,[r7]
str lr,[r7,#4]
orr r2,r6,#0xc0
msr cpsr_c,r2
mrc p15,0,r8,c1,c0,0
mov r9,#0
mov r2,#0x0c
nop
bic r2,r8,#0x2000
bic r2,r2,#0x1000
bic r2,r2,#5
bic r2,r2,#0x80000
orr r2,r2,#0x40000
mcr p15,0,r2,c1,c0,0
mov r5,#8
ldr r10,[r5]
ldr r11,[r5,#4]
ldr r2,=0xe51ff004
str r2,[r5]
ldr r2,=cpu_arm9_raw_swi_handler
str r2,[r5,#4]
mov r2,#0
mcr p15,0,r2,c7,c10,4
mcr p15,0,r2,c7,c5,0
ldr r3,=cpu_arm9_exception_capture
str r2,[r3]
ldr r2,=0xbaadc0de
str r2,[r3,#4]
str r2,[r3,#8]
str r2,[r3,#12]
adr r2,1f
str r2,[r7,#20]
adr r5,2f
ldr r0,[r7,#8]
orr r0,r0,#0xdf
msr cpsr_fsxc,r0
1:
svc #0x0d0000
2:
mrs r5,cpsr
str r5,[r7,#16]
mov r5,#8
str r10,[r5]
str r11,[r5,#4]
ldr r0,[r5]
eor r0,r0,r10
ldr r1,[r5,#4]
eor r1,r1,r11
orr r0,r0,r1
str r0,[r7,#24]
mov r2,#0
mcr p15,0,r2,c7,c10,4
mcr p15,0,r2,c7,c5,0
nop
mcr p15,0,r8,c1,c0,0
mrc p15,0,r2,c1,c0,0
eor r2,r2,r8
str r2,[r7,#12]
mov r2,#0
eor r2,r2,r9
ldr r3,[r7,#12]
orr r2,r2,r3
str r2,[r7,#12]
msr cpsr_c,#211
ldr r2,[r7]
msr spsr_fsxc,r2
ldr lr,[r7,#4]
orr r2,r6,#0xc0
msr cpsr_c,r2
ldr r3,=cpu_arm9_exception_capture
ldr r2,=0xf00000ff
ldr r0,[r3,#12]
ldr r1,[r7,#20]
sub r0,r0,r1
ldr r1,[r7,#16]
and r1,r1,r2
ldr r2,[r3]
cmp r2,#1
orrne r1,r1,#0x100
ldr r2,[r7,#12]
cmp r2,#0
orrne r1,r1,#0x400
ldr r2,[r7,#24]
cmp r2,#0
orrne r1,r1,#0x800
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#44
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_raw_swi_thumb_entry
.type cpu_arm9_raw_swi_thumb_entry, %function
cpu_arm9_raw_swi_thumb_entry:
push {r4-r11,lr}
sub sp,sp,#44
mov r7,sp
mov r4,r3
mrs r6,cpsr
str r0,[r7,#8]
orr r2,r6,#0xc0
msr cpsr_c,r2
msr cpsr_c,#211
mrs r2,spsr
str r2,[r7]
str lr,[r7,#4]
orr r2,r6,#0xc0
msr cpsr_c,r2
mrc p15,0,r8,c1,c0,0
mov r9,#0
mov r2,#0x0c
nop
bic r2,r8,#0x2000
bic r2,r2,#0x1000
bic r2,r2,#5
bic r2,r2,#0x80000
orr r2,r2,#0x40000
mcr p15,0,r2,c1,c0,0
mov r5,#8
ldr r10,[r5]
ldr r11,[r5,#4]
ldr r2,=0xe51ff004
str r2,[r5]
ldr r2,=cpu_arm9_raw_swi_handler
str r2,[r5,#4]
mov r2,#0
mcr p15,0,r2,c7,c10,4
mcr p15,0,r2,c7,c5,0
ldr r3,=cpu_arm9_exception_capture
str r2,[r3]
ldr r2,=0xbaadc0de
str r2,[r3,#4]
str r2,[r3,#8]
str r2,[r3,#12]
adr r2,1f
str r2,[r7,#20]
adr r5,2f
ldr r0,[r7,#8]
orr r0,r0,#0xdf
msr cpsr_fsxc,r0
adr r0,0f
orr r0,r0,#1
bx r0
.thumb
.balign 4
0:
1:
svc #0x0d
bx r5
bx r5
.arm
.balign 4
2:
mrs r5,cpsr
str r5,[r7,#16]
mov r5,#8
str r10,[r5]
str r11,[r5,#4]
ldr r0,[r5]
eor r0,r0,r10
ldr r1,[r5,#4]
eor r1,r1,r11
orr r0,r0,r1
str r0,[r7,#24]
mov r2,#0
mcr p15,0,r2,c7,c10,4
mcr p15,0,r2,c7,c5,0
nop
mcr p15,0,r8,c1,c0,0
mrc p15,0,r2,c1,c0,0
eor r2,r2,r8
str r2,[r7,#12]
mov r2,#0
eor r2,r2,r9
ldr r3,[r7,#12]
orr r2,r2,r3
str r2,[r7,#12]
msr cpsr_c,#211
ldr r2,[r7]
msr spsr_fsxc,r2
ldr lr,[r7,#4]
orr r2,r6,#0xc0
msr cpsr_c,r2
ldr r3,=cpu_arm9_exception_capture
ldr r2,=0xf00000ff
ldr r0,[r3,#4]
and r0,r0,r2
ldr r1,[r3,#8]
and r1,r1,r2
ldr r2,[r3]
cmp r2,#1
orrne r1,r1,#0x100
ldr r2,[r7,#12]
cmp r2,#0
orrne r1,r1,#0x400
ldr r2,[r7,#24]
cmp r2,#0
orrne r1,r1,#0x800
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#44
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_raw_swi_thumb_link_return
.type cpu_arm9_raw_swi_thumb_link_return, %function
cpu_arm9_raw_swi_thumb_link_return:
push {r4-r11,lr}
sub sp,sp,#44
mov r7,sp
mov r4,r3
mrs r6,cpsr
str r0,[r7,#8]
orr r2,r6,#0xc0
msr cpsr_c,r2
msr cpsr_c,#211
mrs r2,spsr
str r2,[r7]
str lr,[r7,#4]
orr r2,r6,#0xc0
msr cpsr_c,r2
mrc p15,0,r8,c1,c0,0
mov r9,#0
mov r2,#0x0c
nop
bic r2,r8,#0x2000
bic r2,r2,#0x1000
bic r2,r2,#5
bic r2,r2,#0x80000
orr r2,r2,#0x40000
mcr p15,0,r2,c1,c0,0
mov r5,#8
ldr r10,[r5]
ldr r11,[r5,#4]
ldr r2,=0xe51ff004
str r2,[r5]
ldr r2,=cpu_arm9_raw_swi_handler
str r2,[r5,#4]
mov r2,#0
mcr p15,0,r2,c7,c10,4
mcr p15,0,r2,c7,c5,0
ldr r3,=cpu_arm9_exception_capture
str r2,[r3]
ldr r2,=0xbaadc0de
str r2,[r3,#4]
str r2,[r3,#8]
str r2,[r3,#12]
adr r2,1f
str r2,[r7,#20]
adr r5,2f
ldr r0,[r7,#8]
orr r0,r0,#0xdf
msr cpsr_fsxc,r0
adr r0,0f
orr r0,r0,#1
bx r0
.thumb
.balign 4
0:
1:
svc #0x0d
bx r5
bx r5
.arm
.balign 4
2:
mrs r5,cpsr
str r5,[r7,#16]
mov r5,#8
str r10,[r5]
str r11,[r5,#4]
ldr r0,[r5]
eor r0,r0,r10
ldr r1,[r5,#4]
eor r1,r1,r11
orr r0,r0,r1
str r0,[r7,#24]
mov r2,#0
mcr p15,0,r2,c7,c10,4
mcr p15,0,r2,c7,c5,0
nop
mcr p15,0,r8,c1,c0,0
mrc p15,0,r2,c1,c0,0
eor r2,r2,r8
str r2,[r7,#12]
mov r2,#0
eor r2,r2,r9
ldr r3,[r7,#12]
orr r2,r2,r3
str r2,[r7,#12]
msr cpsr_c,#211
ldr r2,[r7]
msr spsr_fsxc,r2
ldr lr,[r7,#4]
orr r2,r6,#0xc0
msr cpsr_c,r2
ldr r3,=cpu_arm9_exception_capture
ldr r2,=0xf00000ff
ldr r0,[r3,#12]
ldr r1,[r7,#20]
sub r0,r0,r1
ldr r1,[r7,#16]
and r1,r1,r2
ldr r2,[r3]
cmp r2,#1
orrne r1,r1,#0x100
ldr r2,[r7,#12]
cmp r2,#0
orrne r1,r1,#0x400
ldr r2,[r7,#24]
cmp r2,#0
orrne r1,r1,#0x800
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#44
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_raw_swi_handler
cpu_arm9_raw_swi_handler:
ldr r3,=cpu_arm9_exception_capture
mrs r0,cpsr
str r0,[r3,#4]
mrs r0,spsr
str r0,[r3,#8]
str lr,[r3,#12]
ldr r0,[r3]
add r0,r0,#1
str r0,[r3]
movs pc,lr
.ltorg
.arm
.balign 4
.global cpu_arm9_raw_undefined_arm_entry
.type cpu_arm9_raw_undefined_arm_entry, %function
cpu_arm9_raw_undefined_arm_entry:
push {r4-r11,lr}
sub sp,sp,#44
mov r7,sp
mov r4,r3
mrs r6,cpsr
str r0,[r7,#8]
orr r2,r6,#0xc0
msr cpsr_c,r2
msr cpsr_c,#219
mrs r2,spsr
str r2,[r7]
str lr,[r7,#4]
orr r2,r6,#0xc0
msr cpsr_c,r2
mrc p15,0,r8,c1,c0,0
mov r9,#0
mov r2,#0x0c
nop
bic r2,r8,#0x2000
bic r2,r2,#0x1000
bic r2,r2,#5
bic r2,r2,#0x80000
orr r2,r2,#0x40000
mcr p15,0,r2,c1,c0,0
mov r5,#4
ldr r10,[r5]
ldr r11,[r5,#4]
ldr r2,=0xe51ff004
str r2,[r5]
ldr r2,=cpu_arm9_raw_undefined_handler
str r2,[r5,#4]
mov r2,#0
mcr p15,0,r2,c7,c10,4
mcr p15,0,r2,c7,c5,0
ldr r3,=cpu_arm9_exception_capture
str r2,[r3]
ldr r2,=0xbaadc0de
str r2,[r3,#4]
str r2,[r3,#8]
str r2,[r3,#12]
adr r2,1f
str r2,[r7,#20]
adr r5,2f
ldr r0,[r7,#8]
orr r0,r0,#0xdf
msr cpsr_fsxc,r0
1:
.word 0xe7f000f0
2:
mrs r5,cpsr
str r5,[r7,#16]
mov r5,#4
str r10,[r5]
str r11,[r5,#4]
ldr r0,[r5]
eor r0,r0,r10
ldr r1,[r5,#4]
eor r1,r1,r11
orr r0,r0,r1
str r0,[r7,#24]
mov r2,#0
mcr p15,0,r2,c7,c10,4
mcr p15,0,r2,c7,c5,0
nop
mcr p15,0,r8,c1,c0,0
mrc p15,0,r2,c1,c0,0
eor r2,r2,r8
str r2,[r7,#12]
mov r2,#0
eor r2,r2,r9
ldr r3,[r7,#12]
orr r2,r2,r3
str r2,[r7,#12]
msr cpsr_c,#219
ldr r2,[r7]
msr spsr_fsxc,r2
ldr lr,[r7,#4]
orr r2,r6,#0xc0
msr cpsr_c,r2
ldr r3,=cpu_arm9_exception_capture
ldr r2,=0xf00000ff
ldr r0,[r3,#4]
and r0,r0,r2
ldr r1,[r3,#8]
and r1,r1,r2
ldr r2,[r3]
cmp r2,#1
orrne r1,r1,#0x100
ldr r2,[r7,#12]
cmp r2,#0
orrne r1,r1,#0x400
ldr r2,[r7,#24]
cmp r2,#0
orrne r1,r1,#0x800
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#44
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_raw_undefined_arm_link_return
.type cpu_arm9_raw_undefined_arm_link_return, %function
cpu_arm9_raw_undefined_arm_link_return:
push {r4-r11,lr}
sub sp,sp,#44
mov r7,sp
mov r4,r3
mrs r6,cpsr
str r0,[r7,#8]
orr r2,r6,#0xc0
msr cpsr_c,r2
msr cpsr_c,#219
mrs r2,spsr
str r2,[r7]
str lr,[r7,#4]
orr r2,r6,#0xc0
msr cpsr_c,r2
mrc p15,0,r8,c1,c0,0
mov r9,#0
mov r2,#0x0c
nop
bic r2,r8,#0x2000
bic r2,r2,#0x1000
bic r2,r2,#5
bic r2,r2,#0x80000
orr r2,r2,#0x40000
mcr p15,0,r2,c1,c0,0
mov r5,#4
ldr r10,[r5]
ldr r11,[r5,#4]
ldr r2,=0xe51ff004
str r2,[r5]
ldr r2,=cpu_arm9_raw_undefined_handler
str r2,[r5,#4]
mov r2,#0
mcr p15,0,r2,c7,c10,4
mcr p15,0,r2,c7,c5,0
ldr r3,=cpu_arm9_exception_capture
str r2,[r3]
ldr r2,=0xbaadc0de
str r2,[r3,#4]
str r2,[r3,#8]
str r2,[r3,#12]
adr r2,1f
str r2,[r7,#20]
adr r5,2f
ldr r0,[r7,#8]
orr r0,r0,#0xdf
msr cpsr_fsxc,r0
1:
.word 0xe7f000f0
2:
mrs r5,cpsr
str r5,[r7,#16]
mov r5,#4
str r10,[r5]
str r11,[r5,#4]
ldr r0,[r5]
eor r0,r0,r10
ldr r1,[r5,#4]
eor r1,r1,r11
orr r0,r0,r1
str r0,[r7,#24]
mov r2,#0
mcr p15,0,r2,c7,c10,4
mcr p15,0,r2,c7,c5,0
nop
mcr p15,0,r8,c1,c0,0
mrc p15,0,r2,c1,c0,0
eor r2,r2,r8
str r2,[r7,#12]
mov r2,#0
eor r2,r2,r9
ldr r3,[r7,#12]
orr r2,r2,r3
str r2,[r7,#12]
msr cpsr_c,#219
ldr r2,[r7]
msr spsr_fsxc,r2
ldr lr,[r7,#4]
orr r2,r6,#0xc0
msr cpsr_c,r2
ldr r3,=cpu_arm9_exception_capture
ldr r2,=0xf00000ff
ldr r0,[r3,#12]
ldr r1,[r7,#20]
sub r0,r0,r1
ldr r1,[r7,#16]
and r1,r1,r2
ldr r2,[r3]
cmp r2,#1
orrne r1,r1,#0x100
ldr r2,[r7,#12]
cmp r2,#0
orrne r1,r1,#0x400
ldr r2,[r7,#24]
cmp r2,#0
orrne r1,r1,#0x800
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#44
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_raw_undefined_thumb_entry
.type cpu_arm9_raw_undefined_thumb_entry, %function
cpu_arm9_raw_undefined_thumb_entry:
push {r4-r11,lr}
sub sp,sp,#44
mov r7,sp
mov r4,r3
mrs r6,cpsr
str r0,[r7,#8]
orr r2,r6,#0xc0
msr cpsr_c,r2
msr cpsr_c,#219
mrs r2,spsr
str r2,[r7]
str lr,[r7,#4]
orr r2,r6,#0xc0
msr cpsr_c,r2
mrc p15,0,r8,c1,c0,0
mov r9,#0
mov r2,#0x0c
nop
bic r2,r8,#0x2000
bic r2,r2,#0x1000
bic r2,r2,#5
bic r2,r2,#0x80000
orr r2,r2,#0x40000
mcr p15,0,r2,c1,c0,0
mov r5,#4
ldr r10,[r5]
ldr r11,[r5,#4]
ldr r2,=0xe51ff004
str r2,[r5]
ldr r2,=cpu_arm9_raw_undefined_handler
str r2,[r5,#4]
mov r2,#0
mcr p15,0,r2,c7,c10,4
mcr p15,0,r2,c7,c5,0
ldr r3,=cpu_arm9_exception_capture
str r2,[r3]
ldr r2,=0xbaadc0de
str r2,[r3,#4]
str r2,[r3,#8]
str r2,[r3,#12]
adr r2,1f
str r2,[r7,#20]
adr r5,2f
ldr r0,[r7,#8]
orr r0,r0,#0xdf
msr cpsr_fsxc,r0
adr r0,0f
orr r0,r0,#1
bx r0
.thumb
.balign 4
0:
1:
.hword 0xde00
bx r5
bx r5
.arm
.balign 4
2:
mrs r5,cpsr
str r5,[r7,#16]
mov r5,#4
str r10,[r5]
str r11,[r5,#4]
ldr r0,[r5]
eor r0,r0,r10
ldr r1,[r5,#4]
eor r1,r1,r11
orr r0,r0,r1
str r0,[r7,#24]
mov r2,#0
mcr p15,0,r2,c7,c10,4
mcr p15,0,r2,c7,c5,0
nop
mcr p15,0,r8,c1,c0,0
mrc p15,0,r2,c1,c0,0
eor r2,r2,r8
str r2,[r7,#12]
mov r2,#0
eor r2,r2,r9
ldr r3,[r7,#12]
orr r2,r2,r3
str r2,[r7,#12]
msr cpsr_c,#219
ldr r2,[r7]
msr spsr_fsxc,r2
ldr lr,[r7,#4]
orr r2,r6,#0xc0
msr cpsr_c,r2
ldr r3,=cpu_arm9_exception_capture
ldr r2,=0xf00000ff
ldr r0,[r3,#4]
and r0,r0,r2
ldr r1,[r3,#8]
and r1,r1,r2
ldr r2,[r3]
cmp r2,#1
orrne r1,r1,#0x100
ldr r2,[r7,#12]
cmp r2,#0
orrne r1,r1,#0x400
ldr r2,[r7,#24]
cmp r2,#0
orrne r1,r1,#0x800
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#44
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_raw_undefined_thumb_link_return
.type cpu_arm9_raw_undefined_thumb_link_return, %function
cpu_arm9_raw_undefined_thumb_link_return:
push {r4-r11,lr}
sub sp,sp,#44
mov r7,sp
mov r4,r3
mrs r6,cpsr
str r0,[r7,#8]
orr r2,r6,#0xc0
msr cpsr_c,r2
msr cpsr_c,#219
mrs r2,spsr
str r2,[r7]
str lr,[r7,#4]
orr r2,r6,#0xc0
msr cpsr_c,r2
mrc p15,0,r8,c1,c0,0
mov r9,#0
mov r2,#0x0c
nop
bic r2,r8,#0x2000
bic r2,r2,#0x1000
bic r2,r2,#5
bic r2,r2,#0x80000
orr r2,r2,#0x40000
mcr p15,0,r2,c1,c0,0
mov r5,#4
ldr r10,[r5]
ldr r11,[r5,#4]
ldr r2,=0xe51ff004
str r2,[r5]
ldr r2,=cpu_arm9_raw_undefined_handler
str r2,[r5,#4]
mov r2,#0
mcr p15,0,r2,c7,c10,4
mcr p15,0,r2,c7,c5,0
ldr r3,=cpu_arm9_exception_capture
str r2,[r3]
ldr r2,=0xbaadc0de
str r2,[r3,#4]
str r2,[r3,#8]
str r2,[r3,#12]
adr r2,1f
str r2,[r7,#20]
adr r5,2f
ldr r0,[r7,#8]
orr r0,r0,#0xdf
msr cpsr_fsxc,r0
adr r0,0f
orr r0,r0,#1
bx r0
.thumb
.balign 4
0:
1:
.hword 0xde00
bx r5
bx r5
.arm
.balign 4
2:
mrs r5,cpsr
str r5,[r7,#16]
mov r5,#4
str r10,[r5]
str r11,[r5,#4]
ldr r0,[r5]
eor r0,r0,r10
ldr r1,[r5,#4]
eor r1,r1,r11
orr r0,r0,r1
str r0,[r7,#24]
mov r2,#0
mcr p15,0,r2,c7,c10,4
mcr p15,0,r2,c7,c5,0
nop
mcr p15,0,r8,c1,c0,0
mrc p15,0,r2,c1,c0,0
eor r2,r2,r8
str r2,[r7,#12]
mov r2,#0
eor r2,r2,r9
ldr r3,[r7,#12]
orr r2,r2,r3
str r2,[r7,#12]
msr cpsr_c,#219
ldr r2,[r7]
msr spsr_fsxc,r2
ldr lr,[r7,#4]
orr r2,r6,#0xc0
msr cpsr_c,r2
ldr r3,=cpu_arm9_exception_capture
ldr r2,=0xf00000ff
ldr r0,[r3,#12]
ldr r1,[r7,#20]
sub r0,r0,r1
ldr r1,[r7,#16]
and r1,r1,r2
ldr r2,[r3]
cmp r2,#1
orrne r1,r1,#0x100
ldr r2,[r7,#12]
cmp r2,#0
orrne r1,r1,#0x400
ldr r2,[r7,#24]
cmp r2,#0
orrne r1,r1,#0x800
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#44
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_raw_undefined_handler
cpu_arm9_raw_undefined_handler:
ldr r3,=cpu_arm9_exception_capture
mrs r0,cpsr
str r0,[r3,#4]
mrs r0,spsr
str r0,[r3,#8]
str lr,[r3,#12]
ldr r0,[r3]
add r0,r0,#1
str r0,[r3]
movs pc,lr
.ltorg
.arm
.balign 4
.global cpu_arm9_raw_data_abort_arm_entry
.type cpu_arm9_raw_data_abort_arm_entry, %function
cpu_arm9_raw_data_abort_arm_entry:
push {r4-r11,lr}
sub sp,sp,#100
mov r7,sp
mov r4,r3
mrs r6,cpsr
str r0,[r7,#8]
orr r2,r6,#0xc0
msr cpsr_c,r2
msr cpsr_c,#215
mrs r2,spsr
str r2,[r7]
str lr,[r7,#4]
orr r2,r6,#0xc0
msr cpsr_c,r2
mrc p15,0,r8,c1,c0,0
mov r9,#0
mov r2,#0x0c
nop
bic r2,r8,#0x2000
bic r2,r2,#0x1000
bic r2,r2,#5
bic r2,r2,#0x80000
orr r2,r2,#0x40000
mcr p15,0,r2,c1,c0,0
mov r5,#16
ldr r10,[r5]
ldr r11,[r5,#4]
ldr r2,=0xe51ff004
str r2,[r5]
ldr r2,=cpu_arm9_raw_data_abort_handler
str r2,[r5,#4]
mov r2,#0
mcr p15,0,r2,c7,c10,4
mcr p15,0,r2,c7,c5,0
ldr r3,=cpu_arm9_exception_capture
str r2,[r3]
ldr r2,=0xbaadc0de
str r2,[r3,#4]
str r2,[r3,#8]
str r2,[r3,#12]
adr r2,1f
str r2,[r7,#20]
adr r5,2f
ldr r0,[r7,#8]
orr r0,r0,#0xdf
msr cpsr_fsxc,r0
ldr r3,=0x021f0000
ldr r2,[r3]
str r2,[r7,#24]
mrc p15,0,r2,c5,c0,2
str r2,[r7,#28]
mrc p15,0,r2,c5,c0,3
str r2,[r7,#32]
mrc p15,0,r2,c5,c0,0
str r2,[r7,#68]
mrc p15,0,r2,c5,c0,1
str r2,[r7,#72]
mrc p15,0,r2,c6,c0,0
str r2,[r7,#36]
mov r2,#0
mcr p15,0,r2,c6,c0,0
mrc p15,0,r2,c6,c1,0
str r2,[r7,#40]
mov r2,#0
mcr p15,0,r2,c6,c1,0
mrc p15,0,r2,c6,c2,0
str r2,[r7,#44]
mov r2,#0
mcr p15,0,r2,c6,c2,0
mrc p15,0,r2,c6,c3,0
str r2,[r7,#48]
mov r2,#0
mcr p15,0,r2,c6,c3,0
mrc p15,0,r2,c6,c4,0
str r2,[r7,#52]
mov r2,#0
mcr p15,0,r2,c6,c4,0
mrc p15,0,r2,c6,c5,0
str r2,[r7,#56]
mov r2,#0
mcr p15,0,r2,c6,c5,0
mrc p15,0,r2,c6,c6,0
str r2,[r7,#60]
mov r2,#0
mcr p15,0,r2,c6,c6,0
mrc p15,0,r2,c6,c7,0
str r2,[r7,#64]
mov r2,#0
mcr p15,0,r2,c6,c7,0
mov r2,#0x3f
mcr p15,0,r2,c6,c0,0
ldr r2,=0x021f0017
mcr p15,0,r2,c6,c7,0
mov r2,#3
mcr p15,0,r2,c5,c0,2
ldr r2,=0x30000003
mcr p15,0,r2,c5,c0,3
mov r12,r5
ldr r5,=0x021f0000
mov r2,#0
mcr p15,0,r2,c7,c10,4
mcr p15,0,r2,c7,c5,0
mrc p15,0,r2,c1,c0,0
orr r2,r2,#1
mcr p15,0,r2,c1,c0,0
mrc p15,0,r2,c1,c0,0
str r2,[r7,#76]
1:
ldr r0,[r5]
2:
mrs r5,cpsr
str r5,[r7,#16]
mrc p15,0,r2,c1,c0,0
bic r2,r2,#1
mcr p15,0,r2,c1,c0,0
ldr r3,=0x021f0000
ldr r2,[r7,#24]
str r2,[r3]
ldr r2,[r7,#36]
mcr p15,0,r2,c6,c0,0
ldr r2,[r7,#40]
mcr p15,0,r2,c6,c1,0
ldr r2,[r7,#44]
mcr p15,0,r2,c6,c2,0
ldr r2,[r7,#48]
mcr p15,0,r2,c6,c3,0
ldr r2,[r7,#52]
mcr p15,0,r2,c6,c4,0
ldr r2,[r7,#56]
mcr p15,0,r2,c6,c5,0
ldr r2,[r7,#60]
mcr p15,0,r2,c6,c6,0
ldr r2,[r7,#64]
mcr p15,0,r2,c6,c7,0
ldr r2,[r7,#68]
mcr p15,0,r2,c5,c0,0
ldr r2,[r7,#72]
mcr p15,0,r2,c5,c0,1
ldr r2,[r7,#28]
mcr p15,0,r2,c5,c0,2
ldr r2,[r7,#32]
mcr p15,0,r2,c5,c0,3
mov r5,#16
str r10,[r5]
str r11,[r5,#4]
ldr r0,[r5]
eor r0,r0,r10
ldr r1,[r5,#4]
eor r1,r1,r11
orr r0,r0,r1
str r0,[r7,#24]
mov r2,#0
mcr p15,0,r2,c7,c10,4
mcr p15,0,r2,c7,c5,0
nop
mcr p15,0,r8,c1,c0,0
mrc p15,0,r2,c1,c0,0
eor r2,r2,r8
str r2,[r7,#12]
mov r2,#0
eor r2,r2,r9
ldr r3,[r7,#12]
orr r2,r2,r3
str r2,[r7,#12]
msr cpsr_c,#215
ldr r2,[r7]
msr spsr_fsxc,r2
ldr lr,[r7,#4]
orr r2,r6,#0xc0
msr cpsr_c,r2
ldr r3,=cpu_arm9_exception_capture
ldr r2,=0xf00000ff
ldr r0,[r3,#4]
and r0,r0,r2
ldr r1,[r3,#8]
and r1,r1,r2
ldr r2,[r3]
cmp r2,#1
orrne r1,r1,#0x100
ldr r2,[r7,#12]
cmp r2,#0
orrne r1,r1,#0x400
ldr r2,[r7,#24]
cmp r2,#0
orrne r1,r1,#0x800
ldr r2,[r7,#76]
tst r2,#1
orreq r1,r1,#0x200
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#100
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_raw_data_abort_arm_link_return
.type cpu_arm9_raw_data_abort_arm_link_return, %function
cpu_arm9_raw_data_abort_arm_link_return:
push {r4-r11,lr}
sub sp,sp,#100
mov r7,sp
mov r4,r3
mrs r6,cpsr
str r0,[r7,#8]
orr r2,r6,#0xc0
msr cpsr_c,r2
msr cpsr_c,#215
mrs r2,spsr
str r2,[r7]
str lr,[r7,#4]
orr r2,r6,#0xc0
msr cpsr_c,r2
mrc p15,0,r8,c1,c0,0
mov r9,#0
mov r2,#0x0c
nop
bic r2,r8,#0x2000
bic r2,r2,#0x1000
bic r2,r2,#5
bic r2,r2,#0x80000
orr r2,r2,#0x40000
mcr p15,0,r2,c1,c0,0
mov r5,#16
ldr r10,[r5]
ldr r11,[r5,#4]
ldr r2,=0xe51ff004
str r2,[r5]
ldr r2,=cpu_arm9_raw_data_abort_handler
str r2,[r5,#4]
mov r2,#0
mcr p15,0,r2,c7,c10,4
mcr p15,0,r2,c7,c5,0
ldr r3,=cpu_arm9_exception_capture
str r2,[r3]
ldr r2,=0xbaadc0de
str r2,[r3,#4]
str r2,[r3,#8]
str r2,[r3,#12]
adr r2,1f
str r2,[r7,#20]
adr r5,2f
ldr r0,[r7,#8]
orr r0,r0,#0xdf
msr cpsr_fsxc,r0
ldr r3,=0x021f0000
ldr r2,[r3]
str r2,[r7,#24]
mrc p15,0,r2,c5,c0,2
str r2,[r7,#28]
mrc p15,0,r2,c5,c0,3
str r2,[r7,#32]
mrc p15,0,r2,c5,c0,0
str r2,[r7,#68]
mrc p15,0,r2,c5,c0,1
str r2,[r7,#72]
mrc p15,0,r2,c6,c0,0
str r2,[r7,#36]
mov r2,#0
mcr p15,0,r2,c6,c0,0
mrc p15,0,r2,c6,c1,0
str r2,[r7,#40]
mov r2,#0
mcr p15,0,r2,c6,c1,0
mrc p15,0,r2,c6,c2,0
str r2,[r7,#44]
mov r2,#0
mcr p15,0,r2,c6,c2,0
mrc p15,0,r2,c6,c3,0
str r2,[r7,#48]
mov r2,#0
mcr p15,0,r2,c6,c3,0
mrc p15,0,r2,c6,c4,0
str r2,[r7,#52]
mov r2,#0
mcr p15,0,r2,c6,c4,0
mrc p15,0,r2,c6,c5,0
str r2,[r7,#56]
mov r2,#0
mcr p15,0,r2,c6,c5,0
mrc p15,0,r2,c6,c6,0
str r2,[r7,#60]
mov r2,#0
mcr p15,0,r2,c6,c6,0
mrc p15,0,r2,c6,c7,0
str r2,[r7,#64]
mov r2,#0
mcr p15,0,r2,c6,c7,0
mov r2,#0x3f
mcr p15,0,r2,c6,c0,0
ldr r2,=0x021f0017
mcr p15,0,r2,c6,c7,0
mov r2,#3
mcr p15,0,r2,c5,c0,2
ldr r2,=0x30000003
mcr p15,0,r2,c5,c0,3
mov r12,r5
ldr r5,=0x021f0000
mov r2,#0
mcr p15,0,r2,c7,c10,4
mcr p15,0,r2,c7,c5,0
mrc p15,0,r2,c1,c0,0
orr r2,r2,#1
mcr p15,0,r2,c1,c0,0
mrc p15,0,r2,c1,c0,0
str r2,[r7,#76]
1:
ldr r0,[r5]
2:
mrs r5,cpsr
str r5,[r7,#16]
mrc p15,0,r2,c1,c0,0
bic r2,r2,#1
mcr p15,0,r2,c1,c0,0
ldr r3,=0x021f0000
ldr r2,[r7,#24]
str r2,[r3]
ldr r2,[r7,#36]
mcr p15,0,r2,c6,c0,0
ldr r2,[r7,#40]
mcr p15,0,r2,c6,c1,0
ldr r2,[r7,#44]
mcr p15,0,r2,c6,c2,0
ldr r2,[r7,#48]
mcr p15,0,r2,c6,c3,0
ldr r2,[r7,#52]
mcr p15,0,r2,c6,c4,0
ldr r2,[r7,#56]
mcr p15,0,r2,c6,c5,0
ldr r2,[r7,#60]
mcr p15,0,r2,c6,c6,0
ldr r2,[r7,#64]
mcr p15,0,r2,c6,c7,0
ldr r2,[r7,#68]
mcr p15,0,r2,c5,c0,0
ldr r2,[r7,#72]
mcr p15,0,r2,c5,c0,1
ldr r2,[r7,#28]
mcr p15,0,r2,c5,c0,2
ldr r2,[r7,#32]
mcr p15,0,r2,c5,c0,3
mov r5,#16
str r10,[r5]
str r11,[r5,#4]
ldr r0,[r5]
eor r0,r0,r10
ldr r1,[r5,#4]
eor r1,r1,r11
orr r0,r0,r1
str r0,[r7,#24]
mov r2,#0
mcr p15,0,r2,c7,c10,4
mcr p15,0,r2,c7,c5,0
nop
mcr p15,0,r8,c1,c0,0
mrc p15,0,r2,c1,c0,0
eor r2,r2,r8
str r2,[r7,#12]
mov r2,#0
eor r2,r2,r9
ldr r3,[r7,#12]
orr r2,r2,r3
str r2,[r7,#12]
msr cpsr_c,#215
ldr r2,[r7]
msr spsr_fsxc,r2
ldr lr,[r7,#4]
orr r2,r6,#0xc0
msr cpsr_c,r2
ldr r3,=cpu_arm9_exception_capture
ldr r2,=0xf00000ff
ldr r0,[r3,#12]
ldr r1,[r7,#20]
sub r0,r0,r1
ldr r1,[r7,#16]
and r1,r1,r2
ldr r2,[r3]
cmp r2,#1
orrne r1,r1,#0x100
ldr r2,[r7,#12]
cmp r2,#0
orrne r1,r1,#0x400
ldr r2,[r7,#24]
cmp r2,#0
orrne r1,r1,#0x800
ldr r2,[r7,#76]
tst r2,#1
orreq r1,r1,#0x200
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#100
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_raw_data_abort_thumb_entry
.type cpu_arm9_raw_data_abort_thumb_entry, %function
cpu_arm9_raw_data_abort_thumb_entry:
push {r4-r11,lr}
sub sp,sp,#100
mov r7,sp
mov r4,r3
mrs r6,cpsr
str r0,[r7,#8]
orr r2,r6,#0xc0
msr cpsr_c,r2
msr cpsr_c,#215
mrs r2,spsr
str r2,[r7]
str lr,[r7,#4]
orr r2,r6,#0xc0
msr cpsr_c,r2
mrc p15,0,r8,c1,c0,0
mov r9,#0
mov r2,#0x0c
nop
bic r2,r8,#0x2000
bic r2,r2,#0x1000
bic r2,r2,#5
bic r2,r2,#0x80000
orr r2,r2,#0x40000
mcr p15,0,r2,c1,c0,0
mov r5,#16
ldr r10,[r5]
ldr r11,[r5,#4]
ldr r2,=0xe51ff004
str r2,[r5]
ldr r2,=cpu_arm9_raw_data_abort_handler
str r2,[r5,#4]
mov r2,#0
mcr p15,0,r2,c7,c10,4
mcr p15,0,r2,c7,c5,0
ldr r3,=cpu_arm9_exception_capture
str r2,[r3]
ldr r2,=0xbaadc0de
str r2,[r3,#4]
str r2,[r3,#8]
str r2,[r3,#12]
adr r2,1f
str r2,[r7,#20]
adr r5,2f
ldr r0,[r7,#8]
orr r0,r0,#0xdf
msr cpsr_fsxc,r0
ldr r3,=0x021f0000
ldr r2,[r3]
str r2,[r7,#24]
mrc p15,0,r2,c5,c0,2
str r2,[r7,#28]
mrc p15,0,r2,c5,c0,3
str r2,[r7,#32]
mrc p15,0,r2,c5,c0,0
str r2,[r7,#68]
mrc p15,0,r2,c5,c0,1
str r2,[r7,#72]
mrc p15,0,r2,c6,c0,0
str r2,[r7,#36]
mov r2,#0
mcr p15,0,r2,c6,c0,0
mrc p15,0,r2,c6,c1,0
str r2,[r7,#40]
mov r2,#0
mcr p15,0,r2,c6,c1,0
mrc p15,0,r2,c6,c2,0
str r2,[r7,#44]
mov r2,#0
mcr p15,0,r2,c6,c2,0
mrc p15,0,r2,c6,c3,0
str r2,[r7,#48]
mov r2,#0
mcr p15,0,r2,c6,c3,0
mrc p15,0,r2,c6,c4,0
str r2,[r7,#52]
mov r2,#0
mcr p15,0,r2,c6,c4,0
mrc p15,0,r2,c6,c5,0
str r2,[r7,#56]
mov r2,#0
mcr p15,0,r2,c6,c5,0
mrc p15,0,r2,c6,c6,0
str r2,[r7,#60]
mov r2,#0
mcr p15,0,r2,c6,c6,0
mrc p15,0,r2,c6,c7,0
str r2,[r7,#64]
mov r2,#0
mcr p15,0,r2,c6,c7,0
mov r2,#0x3f
mcr p15,0,r2,c6,c0,0
ldr r2,=0x021f0017
mcr p15,0,r2,c6,c7,0
mov r2,#3
mcr p15,0,r2,c5,c0,2
ldr r2,=0x30000003
mcr p15,0,r2,c5,c0,3
mov r12,r5
ldr r5,=0x021f0000
mov r2,#0
mcr p15,0,r2,c7,c10,4
mcr p15,0,r2,c7,c5,0
mrc p15,0,r2,c1,c0,0
orr r2,r2,#1
mcr p15,0,r2,c1,c0,0
mrc p15,0,r2,c1,c0,0
str r2,[r7,#76]
adr r0,0f
orr r0,r0,#1
bx r0
.thumb
.balign 4
0:
1:
ldr r0,[r5]
bx r12
bx r12
.arm
.balign 4
2:
mrs r5,cpsr
str r5,[r7,#16]
mrc p15,0,r2,c1,c0,0
bic r2,r2,#1
mcr p15,0,r2,c1,c0,0
ldr r3,=0x021f0000
ldr r2,[r7,#24]
str r2,[r3]
ldr r2,[r7,#36]
mcr p15,0,r2,c6,c0,0
ldr r2,[r7,#40]
mcr p15,0,r2,c6,c1,0
ldr r2,[r7,#44]
mcr p15,0,r2,c6,c2,0
ldr r2,[r7,#48]
mcr p15,0,r2,c6,c3,0
ldr r2,[r7,#52]
mcr p15,0,r2,c6,c4,0
ldr r2,[r7,#56]
mcr p15,0,r2,c6,c5,0
ldr r2,[r7,#60]
mcr p15,0,r2,c6,c6,0
ldr r2,[r7,#64]
mcr p15,0,r2,c6,c7,0
ldr r2,[r7,#68]
mcr p15,0,r2,c5,c0,0
ldr r2,[r7,#72]
mcr p15,0,r2,c5,c0,1
ldr r2,[r7,#28]
mcr p15,0,r2,c5,c0,2
ldr r2,[r7,#32]
mcr p15,0,r2,c5,c0,3
mov r5,#16
str r10,[r5]
str r11,[r5,#4]
ldr r0,[r5]
eor r0,r0,r10
ldr r1,[r5,#4]
eor r1,r1,r11
orr r0,r0,r1
str r0,[r7,#24]
mov r2,#0
mcr p15,0,r2,c7,c10,4
mcr p15,0,r2,c7,c5,0
nop
mcr p15,0,r8,c1,c0,0
mrc p15,0,r2,c1,c0,0
eor r2,r2,r8
str r2,[r7,#12]
mov r2,#0
eor r2,r2,r9
ldr r3,[r7,#12]
orr r2,r2,r3
str r2,[r7,#12]
msr cpsr_c,#215
ldr r2,[r7]
msr spsr_fsxc,r2
ldr lr,[r7,#4]
orr r2,r6,#0xc0
msr cpsr_c,r2
ldr r3,=cpu_arm9_exception_capture
ldr r2,=0xf00000ff
ldr r0,[r3,#4]
and r0,r0,r2
ldr r1,[r3,#8]
and r1,r1,r2
ldr r2,[r3]
cmp r2,#1
orrne r1,r1,#0x100
ldr r2,[r7,#12]
cmp r2,#0
orrne r1,r1,#0x400
ldr r2,[r7,#24]
cmp r2,#0
orrne r1,r1,#0x800
ldr r2,[r7,#76]
tst r2,#1
orreq r1,r1,#0x200
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#100
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_raw_data_abort_thumb_link_return
.type cpu_arm9_raw_data_abort_thumb_link_return, %function
cpu_arm9_raw_data_abort_thumb_link_return:
push {r4-r11,lr}
sub sp,sp,#100
mov r7,sp
mov r4,r3
mrs r6,cpsr
str r0,[r7,#8]
orr r2,r6,#0xc0
msr cpsr_c,r2
msr cpsr_c,#215
mrs r2,spsr
str r2,[r7]
str lr,[r7,#4]
orr r2,r6,#0xc0
msr cpsr_c,r2
mrc p15,0,r8,c1,c0,0
mov r9,#0
mov r2,#0x0c
nop
bic r2,r8,#0x2000
bic r2,r2,#0x1000
bic r2,r2,#5
bic r2,r2,#0x80000
orr r2,r2,#0x40000
mcr p15,0,r2,c1,c0,0
mov r5,#16
ldr r10,[r5]
ldr r11,[r5,#4]
ldr r2,=0xe51ff004
str r2,[r5]
ldr r2,=cpu_arm9_raw_data_abort_handler
str r2,[r5,#4]
mov r2,#0
mcr p15,0,r2,c7,c10,4
mcr p15,0,r2,c7,c5,0
ldr r3,=cpu_arm9_exception_capture
str r2,[r3]
ldr r2,=0xbaadc0de
str r2,[r3,#4]
str r2,[r3,#8]
str r2,[r3,#12]
adr r2,1f
str r2,[r7,#20]
adr r5,2f
ldr r0,[r7,#8]
orr r0,r0,#0xdf
msr cpsr_fsxc,r0
ldr r3,=0x021f0000
ldr r2,[r3]
str r2,[r7,#24]
mrc p15,0,r2,c5,c0,2
str r2,[r7,#28]
mrc p15,0,r2,c5,c0,3
str r2,[r7,#32]
mrc p15,0,r2,c5,c0,0
str r2,[r7,#68]
mrc p15,0,r2,c5,c0,1
str r2,[r7,#72]
mrc p15,0,r2,c6,c0,0
str r2,[r7,#36]
mov r2,#0
mcr p15,0,r2,c6,c0,0
mrc p15,0,r2,c6,c1,0
str r2,[r7,#40]
mov r2,#0
mcr p15,0,r2,c6,c1,0
mrc p15,0,r2,c6,c2,0
str r2,[r7,#44]
mov r2,#0
mcr p15,0,r2,c6,c2,0
mrc p15,0,r2,c6,c3,0
str r2,[r7,#48]
mov r2,#0
mcr p15,0,r2,c6,c3,0
mrc p15,0,r2,c6,c4,0
str r2,[r7,#52]
mov r2,#0
mcr p15,0,r2,c6,c4,0
mrc p15,0,r2,c6,c5,0
str r2,[r7,#56]
mov r2,#0
mcr p15,0,r2,c6,c5,0
mrc p15,0,r2,c6,c6,0
str r2,[r7,#60]
mov r2,#0
mcr p15,0,r2,c6,c6,0
mrc p15,0,r2,c6,c7,0
str r2,[r7,#64]
mov r2,#0
mcr p15,0,r2,c6,c7,0
mov r2,#0x3f
mcr p15,0,r2,c6,c0,0
ldr r2,=0x021f0017
mcr p15,0,r2,c6,c7,0
mov r2,#3
mcr p15,0,r2,c5,c0,2
ldr r2,=0x30000003
mcr p15,0,r2,c5,c0,3
mov r12,r5
ldr r5,=0x021f0000
mov r2,#0
mcr p15,0,r2,c7,c10,4
mcr p15,0,r2,c7,c5,0
mrc p15,0,r2,c1,c0,0
orr r2,r2,#1
mcr p15,0,r2,c1,c0,0
mrc p15,0,r2,c1,c0,0
str r2,[r7,#76]
adr r0,0f
orr r0,r0,#1
bx r0
.thumb
.balign 4
0:
1:
ldr r0,[r5]
bx r12
bx r12
.arm
.balign 4
2:
mrs r5,cpsr
str r5,[r7,#16]
mrc p15,0,r2,c1,c0,0
bic r2,r2,#1
mcr p15,0,r2,c1,c0,0
ldr r3,=0x021f0000
ldr r2,[r7,#24]
str r2,[r3]
ldr r2,[r7,#36]
mcr p15,0,r2,c6,c0,0
ldr r2,[r7,#40]
mcr p15,0,r2,c6,c1,0
ldr r2,[r7,#44]
mcr p15,0,r2,c6,c2,0
ldr r2,[r7,#48]
mcr p15,0,r2,c6,c3,0
ldr r2,[r7,#52]
mcr p15,0,r2,c6,c4,0
ldr r2,[r7,#56]
mcr p15,0,r2,c6,c5,0
ldr r2,[r7,#60]
mcr p15,0,r2,c6,c6,0
ldr r2,[r7,#64]
mcr p15,0,r2,c6,c7,0
ldr r2,[r7,#68]
mcr p15,0,r2,c5,c0,0
ldr r2,[r7,#72]
mcr p15,0,r2,c5,c0,1
ldr r2,[r7,#28]
mcr p15,0,r2,c5,c0,2
ldr r2,[r7,#32]
mcr p15,0,r2,c5,c0,3
mov r5,#16
str r10,[r5]
str r11,[r5,#4]
ldr r0,[r5]
eor r0,r0,r10
ldr r1,[r5,#4]
eor r1,r1,r11
orr r0,r0,r1
str r0,[r7,#24]
mov r2,#0
mcr p15,0,r2,c7,c10,4
mcr p15,0,r2,c7,c5,0
nop
mcr p15,0,r8,c1,c0,0
mrc p15,0,r2,c1,c0,0
eor r2,r2,r8
str r2,[r7,#12]
mov r2,#0
eor r2,r2,r9
ldr r3,[r7,#12]
orr r2,r2,r3
str r2,[r7,#12]
msr cpsr_c,#215
ldr r2,[r7]
msr spsr_fsxc,r2
ldr lr,[r7,#4]
orr r2,r6,#0xc0
msr cpsr_c,r2
ldr r3,=cpu_arm9_exception_capture
ldr r2,=0xf00000ff
ldr r0,[r3,#12]
ldr r1,[r7,#20]
sub r0,r0,r1
ldr r1,[r7,#16]
and r1,r1,r2
ldr r2,[r3]
cmp r2,#1
orrne r1,r1,#0x100
ldr r2,[r7,#12]
cmp r2,#0
orrne r1,r1,#0x400
ldr r2,[r7,#24]
cmp r2,#0
orrne r1,r1,#0x800
ldr r2,[r7,#76]
tst r2,#1
orreq r1,r1,#0x200
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#100
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_raw_data_abort_handler
cpu_arm9_raw_data_abort_handler:
ldr r3,=cpu_arm9_exception_capture
mrs r0,cpsr
str r0,[r3,#4]
mrs r0,spsr
str r0,[r3,#8]
str lr,[r3,#12]
ldr r0,[r3]
add r0,r0,#1
str r0,[r3]
mrs r0,spsr
tst r0,#0x20
subne lr,lr,#6
subeq lr,lr,#4
movs pc,lr
.ltorg
.arm
.balign 4
.global cpu_arm9_raw_prefetch_abort_arm_entry
.type cpu_arm9_raw_prefetch_abort_arm_entry, %function
cpu_arm9_raw_prefetch_abort_arm_entry:
push {r4-r11,lr}
sub sp,sp,#100
mov r7,sp
mov r4,r3
mrs r6,cpsr
str r0,[r7,#8]
orr r2,r6,#0xc0
msr cpsr_c,r2
msr cpsr_c,#215
mrs r2,spsr
str r2,[r7]
str lr,[r7,#4]
orr r2,r6,#0xc0
msr cpsr_c,r2
mrc p15,0,r8,c1,c0,0
mov r9,#0
mov r2,#0x0c
nop
bic r2,r8,#0x2000
bic r2,r2,#0x1000
bic r2,r2,#5
bic r2,r2,#0x80000
orr r2,r2,#0x40000
mcr p15,0,r2,c1,c0,0
mov r5,#12
ldr r10,[r5]
ldr r11,[r5,#4]
ldr r2,=0xe51ff004
str r2,[r5]
ldr r2,=cpu_arm9_raw_prefetch_abort_handler
str r2,[r5,#4]
mov r2,#0
mcr p15,0,r2,c7,c10,4
mcr p15,0,r2,c7,c5,0
ldr r3,=cpu_arm9_exception_capture
str r2,[r3]
ldr r2,=0xbaadc0de
str r2,[r3,#4]
str r2,[r3,#8]
str r2,[r3,#12]
adr r2,1f
str r2,[r7,#20]
adr r5,2f
ldr r0,[r7,#8]
orr r0,r0,#0xdf
msr cpsr_fsxc,r0
ldr r3,=0x021f0000
ldr r2,[r3]
str r2,[r7,#24]
mrc p15,0,r2,c5,c0,2
str r2,[r7,#28]
mrc p15,0,r2,c5,c0,3
str r2,[r7,#32]
mrc p15,0,r2,c5,c0,0
str r2,[r7,#68]
mrc p15,0,r2,c5,c0,1
str r2,[r7,#72]
mrc p15,0,r2,c6,c0,0
str r2,[r7,#36]
mov r2,#0
mcr p15,0,r2,c6,c0,0
mrc p15,0,r2,c6,c1,0
str r2,[r7,#40]
mov r2,#0
mcr p15,0,r2,c6,c1,0
mrc p15,0,r2,c6,c2,0
str r2,[r7,#44]
mov r2,#0
mcr p15,0,r2,c6,c2,0
mrc p15,0,r2,c6,c3,0
str r2,[r7,#48]
mov r2,#0
mcr p15,0,r2,c6,c3,0
mrc p15,0,r2,c6,c4,0
str r2,[r7,#52]
mov r2,#0
mcr p15,0,r2,c6,c4,0
mrc p15,0,r2,c6,c5,0
str r2,[r7,#56]
mov r2,#0
mcr p15,0,r2,c6,c5,0
mrc p15,0,r2,c6,c6,0
str r2,[r7,#60]
mov r2,#0
mcr p15,0,r2,c6,c6,0
mrc p15,0,r2,c6,c7,0
str r2,[r7,#64]
mov r2,#0
mcr p15,0,r2,c6,c7,0
mov r2,#0x3f
mcr p15,0,r2,c6,c0,0
ldr r2,=0x021f0017
mcr p15,0,r2,c6,c7,0
ldr r2,=0x30000003
mcr p15,0,r2,c5,c0,2
mov r2,#3
mcr p15,0,r2,c5,c0,3
mov r12,r5
ldr r5,=0x021f0000
ldr r2,=0xe12fff1c
str r2,[r5]
str r5,[r7,#20]
ldr r3,=cpu_arm9_exception_capture
str r12,[r3,#16]
mov r2,#0
mcr p15,0,r2,c7,c10,4
mcr p15,0,r2,c7,c5,0
mrc p15,0,r2,c1,c0,0
orr r2,r2,#1
mcr p15,0,r2,c1,c0,0
mrc p15,0,r2,c1,c0,0
str r2,[r7,#76]
1:
bx r5
2:
mrs r5,cpsr
str r5,[r7,#16]
mrc p15,0,r2,c1,c0,0
bic r2,r2,#1
mcr p15,0,r2,c1,c0,0
ldr r3,=0x021f0000
ldr r2,[r7,#24]
str r2,[r3]
ldr r2,[r7,#36]
mcr p15,0,r2,c6,c0,0
ldr r2,[r7,#40]
mcr p15,0,r2,c6,c1,0
ldr r2,[r7,#44]
mcr p15,0,r2,c6,c2,0
ldr r2,[r7,#48]
mcr p15,0,r2,c6,c3,0
ldr r2,[r7,#52]
mcr p15,0,r2,c6,c4,0
ldr r2,[r7,#56]
mcr p15,0,r2,c6,c5,0
ldr r2,[r7,#60]
mcr p15,0,r2,c6,c6,0
ldr r2,[r7,#64]
mcr p15,0,r2,c6,c7,0
ldr r2,[r7,#68]
mcr p15,0,r2,c5,c0,0
ldr r2,[r7,#72]
mcr p15,0,r2,c5,c0,1
ldr r2,[r7,#28]
mcr p15,0,r2,c5,c0,2
ldr r2,[r7,#32]
mcr p15,0,r2,c5,c0,3
mov r5,#12
str r10,[r5]
str r11,[r5,#4]
ldr r0,[r5]
eor r0,r0,r10
ldr r1,[r5,#4]
eor r1,r1,r11
orr r0,r0,r1
str r0,[r7,#24]
mov r2,#0
mcr p15,0,r2,c7,c10,4
mcr p15,0,r2,c7,c5,0
nop
mcr p15,0,r8,c1,c0,0
mrc p15,0,r2,c1,c0,0
eor r2,r2,r8
str r2,[r7,#12]
mov r2,#0
eor r2,r2,r9
ldr r3,[r7,#12]
orr r2,r2,r3
str r2,[r7,#12]
msr cpsr_c,#215
ldr r2,[r7]
msr spsr_fsxc,r2
ldr lr,[r7,#4]
orr r2,r6,#0xc0
msr cpsr_c,r2
ldr r3,=cpu_arm9_exception_capture
ldr r2,=0xf00000ff
ldr r0,[r3,#4]
and r0,r0,r2
ldr r1,[r3,#8]
and r1,r1,r2
ldr r2,[r3]
cmp r2,#1
orrne r1,r1,#0x100
ldr r2,[r7,#12]
cmp r2,#0
orrne r1,r1,#0x400
ldr r2,[r7,#24]
cmp r2,#0
orrne r1,r1,#0x800
ldr r2,[r7,#76]
tst r2,#1
orreq r1,r1,#0x200
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#100
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_raw_prefetch_abort_arm_link_return
.type cpu_arm9_raw_prefetch_abort_arm_link_return, %function
cpu_arm9_raw_prefetch_abort_arm_link_return:
push {r4-r11,lr}
sub sp,sp,#100
mov r7,sp
mov r4,r3
mrs r6,cpsr
str r0,[r7,#8]
orr r2,r6,#0xc0
msr cpsr_c,r2
msr cpsr_c,#215
mrs r2,spsr
str r2,[r7]
str lr,[r7,#4]
orr r2,r6,#0xc0
msr cpsr_c,r2
mrc p15,0,r8,c1,c0,0
mov r9,#0
mov r2,#0x0c
nop
bic r2,r8,#0x2000
bic r2,r2,#0x1000
bic r2,r2,#5
bic r2,r2,#0x80000
orr r2,r2,#0x40000
mcr p15,0,r2,c1,c0,0
mov r5,#12
ldr r10,[r5]
ldr r11,[r5,#4]
ldr r2,=0xe51ff004
str r2,[r5]
ldr r2,=cpu_arm9_raw_prefetch_abort_handler
str r2,[r5,#4]
mov r2,#0
mcr p15,0,r2,c7,c10,4
mcr p15,0,r2,c7,c5,0
ldr r3,=cpu_arm9_exception_capture
str r2,[r3]
ldr r2,=0xbaadc0de
str r2,[r3,#4]
str r2,[r3,#8]
str r2,[r3,#12]
adr r2,1f
str r2,[r7,#20]
adr r5,2f
ldr r0,[r7,#8]
orr r0,r0,#0xdf
msr cpsr_fsxc,r0
ldr r3,=0x021f0000
ldr r2,[r3]
str r2,[r7,#24]
mrc p15,0,r2,c5,c0,2
str r2,[r7,#28]
mrc p15,0,r2,c5,c0,3
str r2,[r7,#32]
mrc p15,0,r2,c5,c0,0
str r2,[r7,#68]
mrc p15,0,r2,c5,c0,1
str r2,[r7,#72]
mrc p15,0,r2,c6,c0,0
str r2,[r7,#36]
mov r2,#0
mcr p15,0,r2,c6,c0,0
mrc p15,0,r2,c6,c1,0
str r2,[r7,#40]
mov r2,#0
mcr p15,0,r2,c6,c1,0
mrc p15,0,r2,c6,c2,0
str r2,[r7,#44]
mov r2,#0
mcr p15,0,r2,c6,c2,0
mrc p15,0,r2,c6,c3,0
str r2,[r7,#48]
mov r2,#0
mcr p15,0,r2,c6,c3,0
mrc p15,0,r2,c6,c4,0
str r2,[r7,#52]
mov r2,#0
mcr p15,0,r2,c6,c4,0
mrc p15,0,r2,c6,c5,0
str r2,[r7,#56]
mov r2,#0
mcr p15,0,r2,c6,c5,0
mrc p15,0,r2,c6,c6,0
str r2,[r7,#60]
mov r2,#0
mcr p15,0,r2,c6,c6,0
mrc p15,0,r2,c6,c7,0
str r2,[r7,#64]
mov r2,#0
mcr p15,0,r2,c6,c7,0
mov r2,#0x3f
mcr p15,0,r2,c6,c0,0
ldr r2,=0x021f0017
mcr p15,0,r2,c6,c7,0
ldr r2,=0x30000003
mcr p15,0,r2,c5,c0,2
mov r2,#3
mcr p15,0,r2,c5,c0,3
mov r12,r5
ldr r5,=0x021f0000
ldr r2,=0xe12fff1c
str r2,[r5]
str r5,[r7,#20]
ldr r3,=cpu_arm9_exception_capture
str r12,[r3,#16]
mov r2,#0
mcr p15,0,r2,c7,c10,4
mcr p15,0,r2,c7,c5,0
mrc p15,0,r2,c1,c0,0
orr r2,r2,#1
mcr p15,0,r2,c1,c0,0
mrc p15,0,r2,c1,c0,0
str r2,[r7,#76]
1:
bx r5
2:
mrs r5,cpsr
str r5,[r7,#16]
mrc p15,0,r2,c1,c0,0
bic r2,r2,#1
mcr p15,0,r2,c1,c0,0
ldr r3,=0x021f0000
ldr r2,[r7,#24]
str r2,[r3]
ldr r2,[r7,#36]
mcr p15,0,r2,c6,c0,0
ldr r2,[r7,#40]
mcr p15,0,r2,c6,c1,0
ldr r2,[r7,#44]
mcr p15,0,r2,c6,c2,0
ldr r2,[r7,#48]
mcr p15,0,r2,c6,c3,0
ldr r2,[r7,#52]
mcr p15,0,r2,c6,c4,0
ldr r2,[r7,#56]
mcr p15,0,r2,c6,c5,0
ldr r2,[r7,#60]
mcr p15,0,r2,c6,c6,0
ldr r2,[r7,#64]
mcr p15,0,r2,c6,c7,0
ldr r2,[r7,#68]
mcr p15,0,r2,c5,c0,0
ldr r2,[r7,#72]
mcr p15,0,r2,c5,c0,1
ldr r2,[r7,#28]
mcr p15,0,r2,c5,c0,2
ldr r2,[r7,#32]
mcr p15,0,r2,c5,c0,3
mov r5,#12
str r10,[r5]
str r11,[r5,#4]
ldr r0,[r5]
eor r0,r0,r10
ldr r1,[r5,#4]
eor r1,r1,r11
orr r0,r0,r1
str r0,[r7,#24]
mov r2,#0
mcr p15,0,r2,c7,c10,4
mcr p15,0,r2,c7,c5,0
nop
mcr p15,0,r8,c1,c0,0
mrc p15,0,r2,c1,c0,0
eor r2,r2,r8
str r2,[r7,#12]
mov r2,#0
eor r2,r2,r9
ldr r3,[r7,#12]
orr r2,r2,r3
str r2,[r7,#12]
msr cpsr_c,#215
ldr r2,[r7]
msr spsr_fsxc,r2
ldr lr,[r7,#4]
orr r2,r6,#0xc0
msr cpsr_c,r2
ldr r3,=cpu_arm9_exception_capture
ldr r2,=0xf00000ff
ldr r0,[r3,#12]
ldr r1,[r7,#20]
sub r0,r0,r1
ldr r1,[r7,#16]
and r1,r1,r2
ldr r2,[r3]
cmp r2,#1
orrne r1,r1,#0x100
ldr r2,[r7,#12]
cmp r2,#0
orrne r1,r1,#0x400
ldr r2,[r7,#24]
cmp r2,#0
orrne r1,r1,#0x800
ldr r2,[r7,#76]
tst r2,#1
orreq r1,r1,#0x200
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#100
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_raw_prefetch_abort_thumb_entry
.type cpu_arm9_raw_prefetch_abort_thumb_entry, %function
cpu_arm9_raw_prefetch_abort_thumb_entry:
push {r4-r11,lr}
sub sp,sp,#100
mov r7,sp
mov r4,r3
mrs r6,cpsr
str r0,[r7,#8]
orr r2,r6,#0xc0
msr cpsr_c,r2
msr cpsr_c,#215
mrs r2,spsr
str r2,[r7]
str lr,[r7,#4]
orr r2,r6,#0xc0
msr cpsr_c,r2
mrc p15,0,r8,c1,c0,0
mov r9,#0
mov r2,#0x0c
nop
bic r2,r8,#0x2000
bic r2,r2,#0x1000
bic r2,r2,#5
bic r2,r2,#0x80000
orr r2,r2,#0x40000
mcr p15,0,r2,c1,c0,0
mov r5,#12
ldr r10,[r5]
ldr r11,[r5,#4]
ldr r2,=0xe51ff004
str r2,[r5]
ldr r2,=cpu_arm9_raw_prefetch_abort_handler
str r2,[r5,#4]
mov r2,#0
mcr p15,0,r2,c7,c10,4
mcr p15,0,r2,c7,c5,0
ldr r3,=cpu_arm9_exception_capture
str r2,[r3]
ldr r2,=0xbaadc0de
str r2,[r3,#4]
str r2,[r3,#8]
str r2,[r3,#12]
adr r2,1f
str r2,[r7,#20]
adr r5,2f
ldr r0,[r7,#8]
orr r0,r0,#0xdf
msr cpsr_fsxc,r0
ldr r3,=0x021f0000
ldr r2,[r3]
str r2,[r7,#24]
mrc p15,0,r2,c5,c0,2
str r2,[r7,#28]
mrc p15,0,r2,c5,c0,3
str r2,[r7,#32]
mrc p15,0,r2,c5,c0,0
str r2,[r7,#68]
mrc p15,0,r2,c5,c0,1
str r2,[r7,#72]
mrc p15,0,r2,c6,c0,0
str r2,[r7,#36]
mov r2,#0
mcr p15,0,r2,c6,c0,0
mrc p15,0,r2,c6,c1,0
str r2,[r7,#40]
mov r2,#0
mcr p15,0,r2,c6,c1,0
mrc p15,0,r2,c6,c2,0
str r2,[r7,#44]
mov r2,#0
mcr p15,0,r2,c6,c2,0
mrc p15,0,r2,c6,c3,0
str r2,[r7,#48]
mov r2,#0
mcr p15,0,r2,c6,c3,0
mrc p15,0,r2,c6,c4,0
str r2,[r7,#52]
mov r2,#0
mcr p15,0,r2,c6,c4,0
mrc p15,0,r2,c6,c5,0
str r2,[r7,#56]
mov r2,#0
mcr p15,0,r2,c6,c5,0
mrc p15,0,r2,c6,c6,0
str r2,[r7,#60]
mov r2,#0
mcr p15,0,r2,c6,c6,0
mrc p15,0,r2,c6,c7,0
str r2,[r7,#64]
mov r2,#0
mcr p15,0,r2,c6,c7,0
mov r2,#0x3f
mcr p15,0,r2,c6,c0,0
ldr r2,=0x021f0017
mcr p15,0,r2,c6,c7,0
ldr r2,=0x30000003
mcr p15,0,r2,c5,c0,2
mov r2,#3
mcr p15,0,r2,c5,c0,3
mov r12,r5
ldr r5,=0x021f0000
ldr r2,=0x46c04760
str r2,[r5]
str r5,[r7,#20]
ldr r3,=cpu_arm9_exception_capture
str r12,[r3,#16]
mov r2,#0
mcr p15,0,r2,c7,c10,4
mcr p15,0,r2,c7,c5,0
mrc p15,0,r2,c1,c0,0
orr r2,r2,#1
mcr p15,0,r2,c1,c0,0
mrc p15,0,r2,c1,c0,0
str r2,[r7,#76]
1:
orr r5,r5,#1
bx r5
2:
mrs r5,cpsr
str r5,[r7,#16]
mrc p15,0,r2,c1,c0,0
bic r2,r2,#1
mcr p15,0,r2,c1,c0,0
ldr r3,=0x021f0000
ldr r2,[r7,#24]
str r2,[r3]
ldr r2,[r7,#36]
mcr p15,0,r2,c6,c0,0
ldr r2,[r7,#40]
mcr p15,0,r2,c6,c1,0
ldr r2,[r7,#44]
mcr p15,0,r2,c6,c2,0
ldr r2,[r7,#48]
mcr p15,0,r2,c6,c3,0
ldr r2,[r7,#52]
mcr p15,0,r2,c6,c4,0
ldr r2,[r7,#56]
mcr p15,0,r2,c6,c5,0
ldr r2,[r7,#60]
mcr p15,0,r2,c6,c6,0
ldr r2,[r7,#64]
mcr p15,0,r2,c6,c7,0
ldr r2,[r7,#68]
mcr p15,0,r2,c5,c0,0
ldr r2,[r7,#72]
mcr p15,0,r2,c5,c0,1
ldr r2,[r7,#28]
mcr p15,0,r2,c5,c0,2
ldr r2,[r7,#32]
mcr p15,0,r2,c5,c0,3
mov r5,#12
str r10,[r5]
str r11,[r5,#4]
ldr r0,[r5]
eor r0,r0,r10
ldr r1,[r5,#4]
eor r1,r1,r11
orr r0,r0,r1
str r0,[r7,#24]
mov r2,#0
mcr p15,0,r2,c7,c10,4
mcr p15,0,r2,c7,c5,0
nop
mcr p15,0,r8,c1,c0,0
mrc p15,0,r2,c1,c0,0
eor r2,r2,r8
str r2,[r7,#12]
mov r2,#0
eor r2,r2,r9
ldr r3,[r7,#12]
orr r2,r2,r3
str r2,[r7,#12]
msr cpsr_c,#215
ldr r2,[r7]
msr spsr_fsxc,r2
ldr lr,[r7,#4]
orr r2,r6,#0xc0
msr cpsr_c,r2
ldr r3,=cpu_arm9_exception_capture
ldr r2,=0xf00000ff
ldr r0,[r3,#4]
and r0,r0,r2
ldr r1,[r3,#8]
and r1,r1,r2
ldr r2,[r3]
cmp r2,#1
orrne r1,r1,#0x100
ldr r2,[r7,#12]
cmp r2,#0
orrne r1,r1,#0x400
ldr r2,[r7,#24]
cmp r2,#0
orrne r1,r1,#0x800
ldr r2,[r7,#76]
tst r2,#1
orreq r1,r1,#0x200
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#100
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_raw_prefetch_abort_thumb_link_return
.type cpu_arm9_raw_prefetch_abort_thumb_link_return, %function
cpu_arm9_raw_prefetch_abort_thumb_link_return:
push {r4-r11,lr}
sub sp,sp,#100
mov r7,sp
mov r4,r3
mrs r6,cpsr
str r0,[r7,#8]
orr r2,r6,#0xc0
msr cpsr_c,r2
msr cpsr_c,#215
mrs r2,spsr
str r2,[r7]
str lr,[r7,#4]
orr r2,r6,#0xc0
msr cpsr_c,r2
mrc p15,0,r8,c1,c0,0
mov r9,#0
mov r2,#0x0c
nop
bic r2,r8,#0x2000
bic r2,r2,#0x1000
bic r2,r2,#5
bic r2,r2,#0x80000
orr r2,r2,#0x40000
mcr p15,0,r2,c1,c0,0
mov r5,#12
ldr r10,[r5]
ldr r11,[r5,#4]
ldr r2,=0xe51ff004
str r2,[r5]
ldr r2,=cpu_arm9_raw_prefetch_abort_handler
str r2,[r5,#4]
mov r2,#0
mcr p15,0,r2,c7,c10,4
mcr p15,0,r2,c7,c5,0
ldr r3,=cpu_arm9_exception_capture
str r2,[r3]
ldr r2,=0xbaadc0de
str r2,[r3,#4]
str r2,[r3,#8]
str r2,[r3,#12]
adr r2,1f
str r2,[r7,#20]
adr r5,2f
ldr r0,[r7,#8]
orr r0,r0,#0xdf
msr cpsr_fsxc,r0
ldr r3,=0x021f0000
ldr r2,[r3]
str r2,[r7,#24]
mrc p15,0,r2,c5,c0,2
str r2,[r7,#28]
mrc p15,0,r2,c5,c0,3
str r2,[r7,#32]
mrc p15,0,r2,c5,c0,0
str r2,[r7,#68]
mrc p15,0,r2,c5,c0,1
str r2,[r7,#72]
mrc p15,0,r2,c6,c0,0
str r2,[r7,#36]
mov r2,#0
mcr p15,0,r2,c6,c0,0
mrc p15,0,r2,c6,c1,0
str r2,[r7,#40]
mov r2,#0
mcr p15,0,r2,c6,c1,0
mrc p15,0,r2,c6,c2,0
str r2,[r7,#44]
mov r2,#0
mcr p15,0,r2,c6,c2,0
mrc p15,0,r2,c6,c3,0
str r2,[r7,#48]
mov r2,#0
mcr p15,0,r2,c6,c3,0
mrc p15,0,r2,c6,c4,0
str r2,[r7,#52]
mov r2,#0
mcr p15,0,r2,c6,c4,0
mrc p15,0,r2,c6,c5,0
str r2,[r7,#56]
mov r2,#0
mcr p15,0,r2,c6,c5,0
mrc p15,0,r2,c6,c6,0
str r2,[r7,#60]
mov r2,#0
mcr p15,0,r2,c6,c6,0
mrc p15,0,r2,c6,c7,0
str r2,[r7,#64]
mov r2,#0
mcr p15,0,r2,c6,c7,0
mov r2,#0x3f
mcr p15,0,r2,c6,c0,0
ldr r2,=0x021f0017
mcr p15,0,r2,c6,c7,0
ldr r2,=0x30000003
mcr p15,0,r2,c5,c0,2
mov r2,#3
mcr p15,0,r2,c5,c0,3
mov r12,r5
ldr r5,=0x021f0000
ldr r2,=0x46c04760
str r2,[r5]
str r5,[r7,#20]
ldr r3,=cpu_arm9_exception_capture
str r12,[r3,#16]
mov r2,#0
mcr p15,0,r2,c7,c10,4
mcr p15,0,r2,c7,c5,0
mrc p15,0,r2,c1,c0,0
orr r2,r2,#1
mcr p15,0,r2,c1,c0,0
mrc p15,0,r2,c1,c0,0
str r2,[r7,#76]
1:
orr r5,r5,#1
bx r5
2:
mrs r5,cpsr
str r5,[r7,#16]
mrc p15,0,r2,c1,c0,0
bic r2,r2,#1
mcr p15,0,r2,c1,c0,0
ldr r3,=0x021f0000
ldr r2,[r7,#24]
str r2,[r3]
ldr r2,[r7,#36]
mcr p15,0,r2,c6,c0,0
ldr r2,[r7,#40]
mcr p15,0,r2,c6,c1,0
ldr r2,[r7,#44]
mcr p15,0,r2,c6,c2,0
ldr r2,[r7,#48]
mcr p15,0,r2,c6,c3,0
ldr r2,[r7,#52]
mcr p15,0,r2,c6,c4,0
ldr r2,[r7,#56]
mcr p15,0,r2,c6,c5,0
ldr r2,[r7,#60]
mcr p15,0,r2,c6,c6,0
ldr r2,[r7,#64]
mcr p15,0,r2,c6,c7,0
ldr r2,[r7,#68]
mcr p15,0,r2,c5,c0,0
ldr r2,[r7,#72]
mcr p15,0,r2,c5,c0,1
ldr r2,[r7,#28]
mcr p15,0,r2,c5,c0,2
ldr r2,[r7,#32]
mcr p15,0,r2,c5,c0,3
mov r5,#12
str r10,[r5]
str r11,[r5,#4]
ldr r0,[r5]
eor r0,r0,r10
ldr r1,[r5,#4]
eor r1,r1,r11
orr r0,r0,r1
str r0,[r7,#24]
mov r2,#0
mcr p15,0,r2,c7,c10,4
mcr p15,0,r2,c7,c5,0
nop
mcr p15,0,r8,c1,c0,0
mrc p15,0,r2,c1,c0,0
eor r2,r2,r8
str r2,[r7,#12]
mov r2,#0
eor r2,r2,r9
ldr r3,[r7,#12]
orr r2,r2,r3
str r2,[r7,#12]
msr cpsr_c,#215
ldr r2,[r7]
msr spsr_fsxc,r2
ldr lr,[r7,#4]
orr r2,r6,#0xc0
msr cpsr_c,r2
ldr r3,=cpu_arm9_exception_capture
ldr r2,=0xf00000ff
ldr r0,[r3,#12]
ldr r1,[r7,#20]
sub r0,r0,r1
ldr r1,[r7,#16]
and r1,r1,r2
ldr r2,[r3]
cmp r2,#1
orrne r1,r1,#0x100
ldr r2,[r7,#12]
cmp r2,#0
orrne r1,r1,#0x400
ldr r2,[r7,#24]
cmp r2,#0
orrne r1,r1,#0x800
ldr r2,[r7,#76]
tst r2,#1
orreq r1,r1,#0x200
msr cpsr_fsxc,r6
mov sp,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#100
pop {r4-r11,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm9_raw_prefetch_abort_handler
cpu_arm9_raw_prefetch_abort_handler:
ldr r3,=cpu_arm9_exception_capture
mrs r0,cpsr
str r0,[r3,#4]
mrs r0,spsr
str r0,[r3,#8]
str lr,[r3,#12]
ldr r0,[r3]
add r0,r0,#1
str r0,[r3]
mrs r0,spsr
bic r0,r0,#0x20
msr spsr_c,r0
ldr lr,[r3,#16]
movs pc,lr
.ltorg
.bss
.balign 4
cpu_arm9_exception_capture:
.space 20
.text
.arm
.balign 4
.global cpu_arm9_adc_register_operand2_lsl
.type cpu_arm9_adc_register_operand2_lsl, %function
cpu_arm9_adc_register_operand2_lsl:
push {r4-r6,lr}
mov r4,r3
mrs r6,cpsr
msr cpsr_f,r2
adcs r0,r0,r1,lsl r2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
msr cpsr_f,r6
pop {r4-r6,lr}
bx lr
.arm
.balign 4
.global cpu_arm9_adc_register_operand2_lsr
.type cpu_arm9_adc_register_operand2_lsr, %function
cpu_arm9_adc_register_operand2_lsr:
push {r4-r6,lr}
mov r4,r3
mrs r6,cpsr
msr cpsr_f,r2
adcs r0,r0,r1,lsr r2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
msr cpsr_f,r6
pop {r4-r6,lr}
bx lr
.arm
.balign 4
.global cpu_arm9_adc_register_operand2_asr
.type cpu_arm9_adc_register_operand2_asr, %function
cpu_arm9_adc_register_operand2_asr:
push {r4-r6,lr}
mov r4,r3
mrs r6,cpsr
msr cpsr_f,r2
adcs r0,r0,r1,asr r2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
msr cpsr_f,r6
pop {r4-r6,lr}
bx lr
.arm
.balign 4
.global cpu_arm9_adc_register_operand2_ror
.type cpu_arm9_adc_register_operand2_ror, %function
cpu_arm9_adc_register_operand2_ror:
push {r4-r6,lr}
mov r4,r3
mrs r6,cpsr
msr cpsr_f,r2
adcs r0,r0,r1,ror r2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
msr cpsr_f,r6
pop {r4-r6,lr}
bx lr
.arm
.balign 4
.global cpu_arm9_sbc_register_operand2_lsl
.type cpu_arm9_sbc_register_operand2_lsl, %function
cpu_arm9_sbc_register_operand2_lsl:
push {r4-r6,lr}
mov r4,r3
mrs r6,cpsr
msr cpsr_f,r2
sbcs r0,r0,r1,lsl r2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
msr cpsr_f,r6
pop {r4-r6,lr}
bx lr
.arm
.balign 4
.global cpu_arm9_sbc_register_operand2_lsr
.type cpu_arm9_sbc_register_operand2_lsr, %function
cpu_arm9_sbc_register_operand2_lsr:
push {r4-r6,lr}
mov r4,r3
mrs r6,cpsr
msr cpsr_f,r2
sbcs r0,r0,r1,lsr r2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
msr cpsr_f,r6
pop {r4-r6,lr}
bx lr
.arm
.balign 4
.global cpu_arm9_sbc_register_operand2_asr
.type cpu_arm9_sbc_register_operand2_asr, %function
cpu_arm9_sbc_register_operand2_asr:
push {r4-r6,lr}
mov r4,r3
mrs r6,cpsr
msr cpsr_f,r2
sbcs r0,r0,r1,asr r2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
msr cpsr_f,r6
pop {r4-r6,lr}
bx lr
.arm
.balign 4
.global cpu_arm9_sbc_register_operand2_ror
.type cpu_arm9_sbc_register_operand2_ror, %function
cpu_arm9_sbc_register_operand2_ror:
push {r4-r6,lr}
mov r4,r3
mrs r6,cpsr
msr cpsr_f,r2
sbcs r0,r0,r1,ror r2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
msr cpsr_f,r6
pop {r4-r6,lr}
bx lr
.arm
.balign 4
.global cpu_arm9_and_register_operand2_lsl
.type cpu_arm9_and_register_operand2_lsl, %function
cpu_arm9_and_register_operand2_lsl:
push {r4-r6,lr}
mov r4,r3
mrs r6,cpsr
msr cpsr_f,r2
ands r0,r0,r1,lsl r2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
msr cpsr_f,r6
pop {r4-r6,lr}
bx lr
.arm
.balign 4
.global cpu_arm9_and_register_operand2_lsr
.type cpu_arm9_and_register_operand2_lsr, %function
cpu_arm9_and_register_operand2_lsr:
push {r4-r6,lr}
mov r4,r3
mrs r6,cpsr
msr cpsr_f,r2
ands r0,r0,r1,lsr r2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
msr cpsr_f,r6
pop {r4-r6,lr}
bx lr
.arm
.balign 4
.global cpu_arm9_and_register_operand2_asr
.type cpu_arm9_and_register_operand2_asr, %function
cpu_arm9_and_register_operand2_asr:
push {r4-r6,lr}
mov r4,r3
mrs r6,cpsr
msr cpsr_f,r2
ands r0,r0,r1,asr r2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
msr cpsr_f,r6
pop {r4-r6,lr}
bx lr
.arm
.balign 4
.global cpu_arm9_and_register_operand2_ror
.type cpu_arm9_and_register_operand2_ror, %function
cpu_arm9_and_register_operand2_ror:
push {r4-r6,lr}
mov r4,r3
mrs r6,cpsr
msr cpsr_f,r2
ands r0,r0,r1,ror r2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
msr cpsr_f,r6
pop {r4-r6,lr}
bx lr
.arm
.balign 4
.global cpu_arm9_eor_register_operand2_lsl
.type cpu_arm9_eor_register_operand2_lsl, %function
cpu_arm9_eor_register_operand2_lsl:
push {r4-r6,lr}
mov r4,r3
mrs r6,cpsr
msr cpsr_f,r2
eors r0,r0,r1,lsl r2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
msr cpsr_f,r6
pop {r4-r6,lr}
bx lr
.arm
.balign 4
.global cpu_arm9_eor_register_operand2_lsr
.type cpu_arm9_eor_register_operand2_lsr, %function
cpu_arm9_eor_register_operand2_lsr:
push {r4-r6,lr}
mov r4,r3
mrs r6,cpsr
msr cpsr_f,r2
eors r0,r0,r1,lsr r2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
msr cpsr_f,r6
pop {r4-r6,lr}
bx lr
.arm
.balign 4
.global cpu_arm9_eor_register_operand2_asr
.type cpu_arm9_eor_register_operand2_asr, %function
cpu_arm9_eor_register_operand2_asr:
push {r4-r6,lr}
mov r4,r3
mrs r6,cpsr
msr cpsr_f,r2
eors r0,r0,r1,asr r2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
msr cpsr_f,r6
pop {r4-r6,lr}
bx lr
.arm
.balign 4
.global cpu_arm9_eor_register_operand2_ror
.type cpu_arm9_eor_register_operand2_ror, %function
cpu_arm9_eor_register_operand2_ror:
push {r4-r6,lr}
mov r4,r3
mrs r6,cpsr
msr cpsr_f,r2
eors r0,r0,r1,ror r2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
msr cpsr_f,r6
pop {r4-r6,lr}
bx lr
.arm
.balign 4
.global cpu_arm9_tst_register_operand2_lsl
.type cpu_arm9_tst_register_operand2_lsl, %function
cpu_arm9_tst_register_operand2_lsl:
push {r4-r6,lr}
mov r4,r3
mrs r6,cpsr
msr cpsr_f,r2
tst r0,r1,lsl r2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
msr cpsr_f,r6
pop {r4-r6,lr}
bx lr
.arm
.balign 4
.global cpu_arm9_tst_register_operand2_lsr
.type cpu_arm9_tst_register_operand2_lsr, %function
cpu_arm9_tst_register_operand2_lsr:
push {r4-r6,lr}
mov r4,r3
mrs r6,cpsr
msr cpsr_f,r2
tst r0,r1,lsr r2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
msr cpsr_f,r6
pop {r4-r6,lr}
bx lr
.arm
.balign 4
.global cpu_arm9_tst_register_operand2_asr
.type cpu_arm9_tst_register_operand2_asr, %function
cpu_arm9_tst_register_operand2_asr:
push {r4-r6,lr}
mov r4,r3
mrs r6,cpsr
msr cpsr_f,r2
tst r0,r1,asr r2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
msr cpsr_f,r6
pop {r4-r6,lr}
bx lr
.arm
.balign 4
.global cpu_arm9_tst_register_operand2_ror
.type cpu_arm9_tst_register_operand2_ror, %function
cpu_arm9_tst_register_operand2_ror:
push {r4-r6,lr}
mov r4,r3
mrs r6,cpsr
msr cpsr_f,r2
tst r0,r1,ror r2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
msr cpsr_f,r6
pop {r4-r6,lr}
bx lr
.arm
.balign 4
.global cpu_arm9_teq_register_operand2_lsl
.type cpu_arm9_teq_register_operand2_lsl, %function
cpu_arm9_teq_register_operand2_lsl:
push {r4-r6,lr}
mov r4,r3
mrs r6,cpsr
msr cpsr_f,r2
teq r0,r1,lsl r2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
msr cpsr_f,r6
pop {r4-r6,lr}
bx lr
.arm
.balign 4
.global cpu_arm9_teq_register_operand2_lsr
.type cpu_arm9_teq_register_operand2_lsr, %function
cpu_arm9_teq_register_operand2_lsr:
push {r4-r6,lr}
mov r4,r3
mrs r6,cpsr
msr cpsr_f,r2
teq r0,r1,lsr r2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
msr cpsr_f,r6
pop {r4-r6,lr}
bx lr
.arm
.balign 4
.global cpu_arm9_teq_register_operand2_asr
.type cpu_arm9_teq_register_operand2_asr, %function
cpu_arm9_teq_register_operand2_asr:
push {r4-r6,lr}
mov r4,r3
mrs r6,cpsr
msr cpsr_f,r2
teq r0,r1,asr r2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
msr cpsr_f,r6
pop {r4-r6,lr}
bx lr
.arm
.balign 4
.global cpu_arm9_teq_register_operand2_ror
.type cpu_arm9_teq_register_operand2_ror, %function
cpu_arm9_teq_register_operand2_ror:
push {r4-r6,lr}
mov r4,r3
mrs r6,cpsr
msr cpsr_f,r2
teq r0,r1,ror r2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
msr cpsr_f,r6
pop {r4-r6,lr}
bx lr
.arm
.balign 4
.global cpu_arm9_cmp_register_operand2_lsl
.type cpu_arm9_cmp_register_operand2_lsl, %function
cpu_arm9_cmp_register_operand2_lsl:
push {r4-r6,lr}
mov r4,r3
mrs r6,cpsr
msr cpsr_f,r2
cmp r0,r1,lsl r2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
msr cpsr_f,r6
pop {r4-r6,lr}
bx lr
.arm
.balign 4
.global cpu_arm9_cmp_register_operand2_lsr
.type cpu_arm9_cmp_register_operand2_lsr, %function
cpu_arm9_cmp_register_operand2_lsr:
push {r4-r6,lr}
mov r4,r3
mrs r6,cpsr
msr cpsr_f,r2
cmp r0,r1,lsr r2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
msr cpsr_f,r6
pop {r4-r6,lr}
bx lr
.arm
.balign 4
.global cpu_arm9_cmp_register_operand2_asr
.type cpu_arm9_cmp_register_operand2_asr, %function
cpu_arm9_cmp_register_operand2_asr:
push {r4-r6,lr}
mov r4,r3
mrs r6,cpsr
msr cpsr_f,r2
cmp r0,r1,asr r2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
msr cpsr_f,r6
pop {r4-r6,lr}
bx lr
.arm
.balign 4
.global cpu_arm9_cmp_register_operand2_ror
.type cpu_arm9_cmp_register_operand2_ror, %function
cpu_arm9_cmp_register_operand2_ror:
push {r4-r6,lr}
mov r4,r3
mrs r6,cpsr
msr cpsr_f,r2
cmp r0,r1,ror r2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
msr cpsr_f,r6
pop {r4-r6,lr}
bx lr
.arm
.balign 4
.global cpu_arm9_cmn_register_operand2_lsl
.type cpu_arm9_cmn_register_operand2_lsl, %function
cpu_arm9_cmn_register_operand2_lsl:
push {r4-r6,lr}
mov r4,r3
mrs r6,cpsr
msr cpsr_f,r2
cmn r0,r1,lsl r2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
msr cpsr_f,r6
pop {r4-r6,lr}
bx lr
.arm
.balign 4
.global cpu_arm9_cmn_register_operand2_lsr
.type cpu_arm9_cmn_register_operand2_lsr, %function
cpu_arm9_cmn_register_operand2_lsr:
push {r4-r6,lr}
mov r4,r3
mrs r6,cpsr
msr cpsr_f,r2
cmn r0,r1,lsr r2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
msr cpsr_f,r6
pop {r4-r6,lr}
bx lr
.arm
.balign 4
.global cpu_arm9_cmn_register_operand2_asr
.type cpu_arm9_cmn_register_operand2_asr, %function
cpu_arm9_cmn_register_operand2_asr:
push {r4-r6,lr}
mov r4,r3
mrs r6,cpsr
msr cpsr_f,r2
cmn r0,r1,asr r2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
msr cpsr_f,r6
pop {r4-r6,lr}
bx lr
.arm
.balign 4
.global cpu_arm9_cmn_register_operand2_ror
.type cpu_arm9_cmn_register_operand2_ror, %function
cpu_arm9_cmn_register_operand2_ror:
push {r4-r6,lr}
mov r4,r3
mrs r6,cpsr
msr cpsr_f,r2
cmn r0,r1,ror r2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
msr cpsr_f,r6
pop {r4-r6,lr}
bx lr
.arm
.balign 4
.global cpu_arm9_add_register_operand2_lsl
.type cpu_arm9_add_register_operand2_lsl, %function
cpu_arm9_add_register_operand2_lsl:
push {r4-r6,lr}
mov r4,r3
mrs r6,cpsr
msr cpsr_f,r2
adds r0,r0,r1,lsl r2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
msr cpsr_f,r6
pop {r4-r6,lr}
bx lr
.arm
.balign 4
.global cpu_arm9_add_register_operand2_lsr
.type cpu_arm9_add_register_operand2_lsr, %function
cpu_arm9_add_register_operand2_lsr:
push {r4-r6,lr}
mov r4,r3
mrs r6,cpsr
msr cpsr_f,r2
adds r0,r0,r1,lsr r2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
msr cpsr_f,r6
pop {r4-r6,lr}
bx lr
.arm
.balign 4
.global cpu_arm9_add_register_operand2_asr
.type cpu_arm9_add_register_operand2_asr, %function
cpu_arm9_add_register_operand2_asr:
push {r4-r6,lr}
mov r4,r3
mrs r6,cpsr
msr cpsr_f,r2
adds r0,r0,r1,asr r2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
msr cpsr_f,r6
pop {r4-r6,lr}
bx lr
.arm
.balign 4
.global cpu_arm9_add_register_operand2_ror
.type cpu_arm9_add_register_operand2_ror, %function
cpu_arm9_add_register_operand2_ror:
push {r4-r6,lr}
mov r4,r3
mrs r6,cpsr
msr cpsr_f,r2
adds r0,r0,r1,ror r2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
msr cpsr_f,r6
pop {r4-r6,lr}
bx lr
.arm
.balign 4
.global cpu_arm9_sub_register_operand2_lsl
.type cpu_arm9_sub_register_operand2_lsl, %function
cpu_arm9_sub_register_operand2_lsl:
push {r4-r6,lr}
mov r4,r3
mrs r6,cpsr
msr cpsr_f,r2
subs r0,r0,r1,lsl r2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
msr cpsr_f,r6
pop {r4-r6,lr}
bx lr
.arm
.balign 4
.global cpu_arm9_sub_register_operand2_lsr
.type cpu_arm9_sub_register_operand2_lsr, %function
cpu_arm9_sub_register_operand2_lsr:
push {r4-r6,lr}
mov r4,r3
mrs r6,cpsr
msr cpsr_f,r2
subs r0,r0,r1,lsr r2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
msr cpsr_f,r6
pop {r4-r6,lr}
bx lr
.arm
.balign 4
.global cpu_arm9_sub_register_operand2_asr
.type cpu_arm9_sub_register_operand2_asr, %function
cpu_arm9_sub_register_operand2_asr:
push {r4-r6,lr}
mov r4,r3
mrs r6,cpsr
msr cpsr_f,r2
subs r0,r0,r1,asr r2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
msr cpsr_f,r6
pop {r4-r6,lr}
bx lr
.arm
.balign 4
.global cpu_arm9_sub_register_operand2_ror
.type cpu_arm9_sub_register_operand2_ror, %function
cpu_arm9_sub_register_operand2_ror:
push {r4-r6,lr}
mov r4,r3
mrs r6,cpsr
msr cpsr_f,r2
subs r0,r0,r1,ror r2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
msr cpsr_f,r6
pop {r4-r6,lr}
bx lr
.arm
.balign 4
.global cpu_arm9_rsb_register_operand2_lsl
.type cpu_arm9_rsb_register_operand2_lsl, %function
cpu_arm9_rsb_register_operand2_lsl:
push {r4-r6,lr}
mov r4,r3
mrs r6,cpsr
msr cpsr_f,r2
rsbs r0,r0,r1,lsl r2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
msr cpsr_f,r6
pop {r4-r6,lr}
bx lr
.arm
.balign 4
.global cpu_arm9_rsb_register_operand2_lsr
.type cpu_arm9_rsb_register_operand2_lsr, %function
cpu_arm9_rsb_register_operand2_lsr:
push {r4-r6,lr}
mov r4,r3
mrs r6,cpsr
msr cpsr_f,r2
rsbs r0,r0,r1,lsr r2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
msr cpsr_f,r6
pop {r4-r6,lr}
bx lr
.arm
.balign 4
.global cpu_arm9_rsb_register_operand2_asr
.type cpu_arm9_rsb_register_operand2_asr, %function
cpu_arm9_rsb_register_operand2_asr:
push {r4-r6,lr}
mov r4,r3
mrs r6,cpsr
msr cpsr_f,r2
rsbs r0,r0,r1,asr r2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
msr cpsr_f,r6
pop {r4-r6,lr}
bx lr
.arm
.balign 4
.global cpu_arm9_rsb_register_operand2_ror
.type cpu_arm9_rsb_register_operand2_ror, %function
cpu_arm9_rsb_register_operand2_ror:
push {r4-r6,lr}
mov r4,r3
mrs r6,cpsr
msr cpsr_f,r2
rsbs r0,r0,r1,ror r2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
msr cpsr_f,r6
pop {r4-r6,lr}
bx lr
.arm
.balign 4
.global cpu_arm9_rsc_register_operand2_lsl
.type cpu_arm9_rsc_register_operand2_lsl, %function
cpu_arm9_rsc_register_operand2_lsl:
push {r4-r6,lr}
mov r4,r3
mrs r6,cpsr
msr cpsr_f,r2
rscs r0,r0,r1,lsl r2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
msr cpsr_f,r6
pop {r4-r6,lr}
bx lr
.arm
.balign 4
.global cpu_arm9_rsc_register_operand2_lsr
.type cpu_arm9_rsc_register_operand2_lsr, %function
cpu_arm9_rsc_register_operand2_lsr:
push {r4-r6,lr}
mov r4,r3
mrs r6,cpsr
msr cpsr_f,r2
rscs r0,r0,r1,lsr r2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
msr cpsr_f,r6
pop {r4-r6,lr}
bx lr
.arm
.balign 4
.global cpu_arm9_rsc_register_operand2_asr
.type cpu_arm9_rsc_register_operand2_asr, %function
cpu_arm9_rsc_register_operand2_asr:
push {r4-r6,lr}
mov r4,r3
mrs r6,cpsr
msr cpsr_f,r2
rscs r0,r0,r1,asr r2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
msr cpsr_f,r6
pop {r4-r6,lr}
bx lr
.arm
.balign 4
.global cpu_arm9_rsc_register_operand2_ror
.type cpu_arm9_rsc_register_operand2_ror, %function
cpu_arm9_rsc_register_operand2_ror:
push {r4-r6,lr}
mov r4,r3
mrs r6,cpsr
msr cpsr_f,r2
rscs r0,r0,r1,ror r2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
msr cpsr_f,r6
pop {r4-r6,lr}
bx lr
.arm
.balign 4
.global cpu_arm9_orr_register_operand2_lsl
.type cpu_arm9_orr_register_operand2_lsl, %function
cpu_arm9_orr_register_operand2_lsl:
push {r4-r6,lr}
mov r4,r3
mrs r6,cpsr
msr cpsr_f,r2
orrs r0,r0,r1,lsl r2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
msr cpsr_f,r6
pop {r4-r6,lr}
bx lr
.arm
.balign 4
.global cpu_arm9_orr_register_operand2_lsr
.type cpu_arm9_orr_register_operand2_lsr, %function
cpu_arm9_orr_register_operand2_lsr:
push {r4-r6,lr}
mov r4,r3
mrs r6,cpsr
msr cpsr_f,r2
orrs r0,r0,r1,lsr r2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
msr cpsr_f,r6
pop {r4-r6,lr}
bx lr
.arm
.balign 4
.global cpu_arm9_orr_register_operand2_asr
.type cpu_arm9_orr_register_operand2_asr, %function
cpu_arm9_orr_register_operand2_asr:
push {r4-r6,lr}
mov r4,r3
mrs r6,cpsr
msr cpsr_f,r2
orrs r0,r0,r1,asr r2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
msr cpsr_f,r6
pop {r4-r6,lr}
bx lr
.arm
.balign 4
.global cpu_arm9_orr_register_operand2_ror
.type cpu_arm9_orr_register_operand2_ror, %function
cpu_arm9_orr_register_operand2_ror:
push {r4-r6,lr}
mov r4,r3
mrs r6,cpsr
msr cpsr_f,r2
orrs r0,r0,r1,ror r2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
msr cpsr_f,r6
pop {r4-r6,lr}
bx lr
.arm
.balign 4
.global cpu_arm9_bic_register_operand2_lsl
.type cpu_arm9_bic_register_operand2_lsl, %function
cpu_arm9_bic_register_operand2_lsl:
push {r4-r6,lr}
mov r4,r3
mrs r6,cpsr
msr cpsr_f,r2
bics r0,r0,r1,lsl r2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
msr cpsr_f,r6
pop {r4-r6,lr}
bx lr
.arm
.balign 4
.global cpu_arm9_bic_register_operand2_lsr
.type cpu_arm9_bic_register_operand2_lsr, %function
cpu_arm9_bic_register_operand2_lsr:
push {r4-r6,lr}
mov r4,r3
mrs r6,cpsr
msr cpsr_f,r2
bics r0,r0,r1,lsr r2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
msr cpsr_f,r6
pop {r4-r6,lr}
bx lr
.arm
.balign 4
.global cpu_arm9_bic_register_operand2_asr
.type cpu_arm9_bic_register_operand2_asr, %function
cpu_arm9_bic_register_operand2_asr:
push {r4-r6,lr}
mov r4,r3
mrs r6,cpsr
msr cpsr_f,r2
bics r0,r0,r1,asr r2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
msr cpsr_f,r6
pop {r4-r6,lr}
bx lr
.arm
.balign 4
.global cpu_arm9_bic_register_operand2_ror
.type cpu_arm9_bic_register_operand2_ror, %function
cpu_arm9_bic_register_operand2_ror:
push {r4-r6,lr}
mov r4,r3
mrs r6,cpsr
msr cpsr_f,r2
bics r0,r0,r1,ror r2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
msr cpsr_f,r6
pop {r4-r6,lr}
bx lr
.arm
.balign 4
.global cpu_arm9_mov_register_operand2_lsl
.type cpu_arm9_mov_register_operand2_lsl, %function
cpu_arm9_mov_register_operand2_lsl:
push {r4-r6,lr}
mov r4,r3
mrs r6,cpsr
msr cpsr_f,r2
movs r0,r1,lsl r2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
msr cpsr_f,r6
pop {r4-r6,lr}
bx lr
.arm
.balign 4
.global cpu_arm9_mov_register_operand2_lsr
.type cpu_arm9_mov_register_operand2_lsr, %function
cpu_arm9_mov_register_operand2_lsr:
push {r4-r6,lr}
mov r4,r3
mrs r6,cpsr
msr cpsr_f,r2
movs r0,r1,lsr r2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
msr cpsr_f,r6
pop {r4-r6,lr}
bx lr
.arm
.balign 4
.global cpu_arm9_mov_register_operand2_asr
.type cpu_arm9_mov_register_operand2_asr, %function
cpu_arm9_mov_register_operand2_asr:
push {r4-r6,lr}
mov r4,r3
mrs r6,cpsr
msr cpsr_f,r2
movs r0,r1,asr r2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
msr cpsr_f,r6
pop {r4-r6,lr}
bx lr
.arm
.balign 4
.global cpu_arm9_mov_register_operand2_ror
.type cpu_arm9_mov_register_operand2_ror, %function
cpu_arm9_mov_register_operand2_ror:
push {r4-r6,lr}
mov r4,r3
mrs r6,cpsr
msr cpsr_f,r2
movs r0,r1,ror r2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
msr cpsr_f,r6
pop {r4-r6,lr}
bx lr
.arm
.balign 4
.global cpu_arm9_mvn_register_operand2_lsl
.type cpu_arm9_mvn_register_operand2_lsl, %function
cpu_arm9_mvn_register_operand2_lsl:
push {r4-r6,lr}
mov r4,r3
mrs r6,cpsr
msr cpsr_f,r2
mvns r0,r1,lsl r2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
msr cpsr_f,r6
pop {r4-r6,lr}
bx lr
.arm
.balign 4
.global cpu_arm9_mvn_register_operand2_lsr
.type cpu_arm9_mvn_register_operand2_lsr, %function
cpu_arm9_mvn_register_operand2_lsr:
push {r4-r6,lr}
mov r4,r3
mrs r6,cpsr
msr cpsr_f,r2
mvns r0,r1,lsr r2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
msr cpsr_f,r6
pop {r4-r6,lr}
bx lr
.arm
.balign 4
.global cpu_arm9_mvn_register_operand2_asr
.type cpu_arm9_mvn_register_operand2_asr, %function
cpu_arm9_mvn_register_operand2_asr:
push {r4-r6,lr}
mov r4,r3
mrs r6,cpsr
msr cpsr_f,r2
mvns r0,r1,asr r2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
msr cpsr_f,r6
pop {r4-r6,lr}
bx lr
.arm
.balign 4
.global cpu_arm9_mvn_register_operand2_ror
.type cpu_arm9_mvn_register_operand2_ror, %function
cpu_arm9_mvn_register_operand2_ror:
push {r4-r6,lr}
mov r4,r3
mrs r6,cpsr
msr cpsr_f,r2
mvns r0,r1,ror r2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
msr cpsr_f,r6
pop {r4-r6,lr}
bx lr
.arm
.balign 4
.global cpu_arm9_thumb_edges_lsl_distinct
.type cpu_arm9_thumb_edges_lsl_distinct, %function
cpu_arm9_thumb_edges_lsl_distinct:
push {r4-r8,lr}
mov r4,r3
mrs r6,cpsr
adr r5,2f
adr r7,1f
orr r7,r7,#1
msr cpsr_f,r2
bx r7
.thumb
.balign 4
1:
.global cpu_arm9_thumb_edges_lsl_distinct_instruction
cpu_arm9_thumb_edges_lsl_distinct_instruction:
lsls r0,r1
bx r5
.arm
.balign 4
2:
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
msr cpsr_f,r6
pop {r4-r8,lr}
bx lr
.arm
.balign 4
.global cpu_arm9_thumb_edges_lsl_alias
.type cpu_arm9_thumb_edges_lsl_alias, %function
cpu_arm9_thumb_edges_lsl_alias:
push {r4-r8,lr}
mov r4,r3
mrs r6,cpsr
adr r5,2f
adr r7,1f
orr r7,r7,#1
msr cpsr_f,r2
bx r7
.thumb
.balign 4
1:
.global cpu_arm9_thumb_edges_lsl_alias_instruction
cpu_arm9_thumb_edges_lsl_alias_instruction:
lsls r0,r0
bx r5
.arm
.balign 4
2:
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
msr cpsr_f,r6
pop {r4-r8,lr}
bx lr
.arm
.balign 4
.global cpu_arm9_thumb_edges_lsr_distinct
.type cpu_arm9_thumb_edges_lsr_distinct, %function
cpu_arm9_thumb_edges_lsr_distinct:
push {r4-r8,lr}
mov r4,r3
mrs r6,cpsr
adr r5,2f
adr r7,1f
orr r7,r7,#1
msr cpsr_f,r2
bx r7
.thumb
.balign 4
1:
.global cpu_arm9_thumb_edges_lsr_distinct_instruction
cpu_arm9_thumb_edges_lsr_distinct_instruction:
lsrs r0,r1
bx r5
.arm
.balign 4
2:
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
msr cpsr_f,r6
pop {r4-r8,lr}
bx lr
.arm
.balign 4
.global cpu_arm9_thumb_edges_lsr_alias
.type cpu_arm9_thumb_edges_lsr_alias, %function
cpu_arm9_thumb_edges_lsr_alias:
push {r4-r8,lr}
mov r4,r3
mrs r6,cpsr
adr r5,2f
adr r7,1f
orr r7,r7,#1
msr cpsr_f,r2
bx r7
.thumb
.balign 4
1:
.global cpu_arm9_thumb_edges_lsr_alias_instruction
cpu_arm9_thumb_edges_lsr_alias_instruction:
lsrs r0,r0
bx r5
.arm
.balign 4
2:
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
msr cpsr_f,r6
pop {r4-r8,lr}
bx lr
.arm
.balign 4
.global cpu_arm9_thumb_edges_asr_distinct
.type cpu_arm9_thumb_edges_asr_distinct, %function
cpu_arm9_thumb_edges_asr_distinct:
push {r4-r8,lr}
mov r4,r3
mrs r6,cpsr
adr r5,2f
adr r7,1f
orr r7,r7,#1
msr cpsr_f,r2
bx r7
.thumb
.balign 4
1:
.global cpu_arm9_thumb_edges_asr_distinct_instruction
cpu_arm9_thumb_edges_asr_distinct_instruction:
asrs r0,r1
bx r5
.arm
.balign 4
2:
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
msr cpsr_f,r6
pop {r4-r8,lr}
bx lr
.arm
.balign 4
.global cpu_arm9_thumb_edges_asr_alias
.type cpu_arm9_thumb_edges_asr_alias, %function
cpu_arm9_thumb_edges_asr_alias:
push {r4-r8,lr}
mov r4,r3
mrs r6,cpsr
adr r5,2f
adr r7,1f
orr r7,r7,#1
msr cpsr_f,r2
bx r7
.thumb
.balign 4
1:
.global cpu_arm9_thumb_edges_asr_alias_instruction
cpu_arm9_thumb_edges_asr_alias_instruction:
asrs r0,r0
bx r5
.arm
.balign 4
2:
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
msr cpsr_f,r6
pop {r4-r8,lr}
bx lr
.arm
.balign 4
.global cpu_arm9_thumb_edges_ror_distinct
.type cpu_arm9_thumb_edges_ror_distinct, %function
cpu_arm9_thumb_edges_ror_distinct:
push {r4-r8,lr}
mov r4,r3
mrs r6,cpsr
adr r5,2f
adr r7,1f
orr r7,r7,#1
msr cpsr_f,r2
bx r7
.thumb
.balign 4
1:
.global cpu_arm9_thumb_edges_ror_distinct_instruction
cpu_arm9_thumb_edges_ror_distinct_instruction:
rors r0,r1
bx r5
.arm
.balign 4
2:
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
msr cpsr_f,r6
pop {r4-r8,lr}
bx lr
.arm
.balign 4
.global cpu_arm9_thumb_edges_ror_alias
.type cpu_arm9_thumb_edges_ror_alias, %function
cpu_arm9_thumb_edges_ror_alias:
push {r4-r8,lr}
mov r4,r3
mrs r6,cpsr
adr r5,2f
adr r7,1f
orr r7,r7,#1
msr cpsr_f,r2
bx r7
.thumb
.balign 4
1:
.global cpu_arm9_thumb_edges_ror_alias_instruction
cpu_arm9_thumb_edges_ror_alias_instruction:
rors r0,r0
bx r5
.arm
.balign 4
2:
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
msr cpsr_f,r6
pop {r4-r8,lr}
bx lr
.arm
.balign 4
.global cpu_arm9_thumb_edges_adc_distinct
.type cpu_arm9_thumb_edges_adc_distinct, %function
cpu_arm9_thumb_edges_adc_distinct:
push {r4-r8,lr}
mov r4,r3
mrs r6,cpsr
adr r5,2f
adr r7,1f
orr r7,r7,#1
msr cpsr_f,r2
bx r7
.thumb
.balign 4
1:
.global cpu_arm9_thumb_edges_adc_distinct_instruction
cpu_arm9_thumb_edges_adc_distinct_instruction:
adcs r0,r1
bx r5
.arm
.balign 4
2:
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
msr cpsr_f,r6
pop {r4-r8,lr}
bx lr
.arm
.balign 4
.global cpu_arm9_thumb_edges_adc_alias
.type cpu_arm9_thumb_edges_adc_alias, %function
cpu_arm9_thumb_edges_adc_alias:
push {r4-r8,lr}
mov r4,r3
mrs r6,cpsr
adr r5,2f
adr r7,1f
orr r7,r7,#1
msr cpsr_f,r2
bx r7
.thumb
.balign 4
1:
.global cpu_arm9_thumb_edges_adc_alias_instruction
cpu_arm9_thumb_edges_adc_alias_instruction:
adcs r0,r0
bx r5
.arm
.balign 4
2:
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
msr cpsr_f,r6
pop {r4-r8,lr}
bx lr
.arm
.balign 4
.global cpu_arm9_thumb_edges_sbc_distinct
.type cpu_arm9_thumb_edges_sbc_distinct, %function
cpu_arm9_thumb_edges_sbc_distinct:
push {r4-r8,lr}
mov r4,r3
mrs r6,cpsr
adr r5,2f
adr r7,1f
orr r7,r7,#1
msr cpsr_f,r2
bx r7
.thumb
.balign 4
1:
.global cpu_arm9_thumb_edges_sbc_distinct_instruction
cpu_arm9_thumb_edges_sbc_distinct_instruction:
sbcs r0,r1
bx r5
.arm
.balign 4
2:
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
msr cpsr_f,r6
pop {r4-r8,lr}
bx lr
.arm
.balign 4
.global cpu_arm9_thumb_edges_sbc_alias
.type cpu_arm9_thumb_edges_sbc_alias, %function
cpu_arm9_thumb_edges_sbc_alias:
push {r4-r8,lr}
mov r4,r3
mrs r6,cpsr
adr r5,2f
adr r7,1f
orr r7,r7,#1
msr cpsr_f,r2
bx r7
.thumb
.balign 4
1:
.global cpu_arm9_thumb_edges_sbc_alias_instruction
cpu_arm9_thumb_edges_sbc_alias_instruction:
sbcs r0,r0
bx r5
.arm
.balign 4
2:
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
msr cpsr_f,r6
pop {r4-r8,lr}
bx lr
.arm
.balign 4
.global cpu_arm9_thumb_edges_neg_distinct
.type cpu_arm9_thumb_edges_neg_distinct, %function
cpu_arm9_thumb_edges_neg_distinct:
push {r4-r8,lr}
mov r4,r3
mrs r6,cpsr
adr r5,2f
adr r7,1f
orr r7,r7,#1
msr cpsr_f,r2
bx r7
.thumb
.balign 4
1:
.global cpu_arm9_thumb_edges_neg_distinct_instruction
cpu_arm9_thumb_edges_neg_distinct_instruction:
rsbs r0,r1,#0
bx r5
.arm
.balign 4
2:
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
msr cpsr_f,r6
pop {r4-r8,lr}
bx lr
.arm
.balign 4
.global cpu_arm9_thumb_edges_mul_distinct
.type cpu_arm9_thumb_edges_mul_distinct, %function
cpu_arm9_thumb_edges_mul_distinct:
push {r4-r8,lr}
mov r4,r3
mrs r6,cpsr
adr r5,2f
adr r7,1f
orr r7,r7,#1
msr cpsr_f,r2
bx r7
.thumb
.balign 4
1:
.global cpu_arm9_thumb_edges_mul_distinct_instruction
cpu_arm9_thumb_edges_mul_distinct_instruction:
muls r0,r1,r0
bx r5
.arm
.balign 4
2:
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
msr cpsr_f,r6
pop {r4-r8,lr}
bx lr
.arm
.balign 4
.global cpu_arm9_thumb_edges_mul_alias
.type cpu_arm9_thumb_edges_mul_alias, %function
cpu_arm9_thumb_edges_mul_alias:
push {r4-r8,lr}
mov r4,r3
mrs r6,cpsr
adr r5,2f
adr r7,1f
orr r7,r7,#1
msr cpsr_f,r2
bx r7
.thumb
.balign 4
1:
.global cpu_arm9_thumb_edges_mul_alias_instruction
cpu_arm9_thumb_edges_mul_alias_instruction:
muls r0,r0,r0
bx r5
.arm
.balign 4
2:
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
msr cpsr_f,r6
pop {r4-r8,lr}
bx lr
