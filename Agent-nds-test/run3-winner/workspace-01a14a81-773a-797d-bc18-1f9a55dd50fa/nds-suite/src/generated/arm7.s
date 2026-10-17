.syntax unified
.arch armv4t
.text
.arm
.balign 4
.global cpu_arm7_arm_ldr_alignment
.type cpu_arm7_arm_ldr_alignment, %function
cpu_arm7_arm_ldr_alignment:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#16
mov r5,sp
mov r6,r1
ldr r7,=0x6d37c2a9
str r7,[r5,#4]
str r0,[r5]
ldr r0,[r5,r6]
ldr r1,[r5,#4]
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#16
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_arm_ldrh_alignment
.type cpu_arm7_arm_ldrh_alignment, %function
cpu_arm7_arm_ldrh_alignment:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#16
mov r5,sp
mov r6,r1
ldr r7,=0x6d37c2a9
str r7,[r5,#4]
str r0,[r5]
ldrh r0,[r5,r6]
ldr r1,[r5,#4]
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#16
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_arm_ldrsh_alignment
.type cpu_arm7_arm_ldrsh_alignment, %function
cpu_arm7_arm_ldrsh_alignment:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#16
mov r5,sp
mov r6,r1
ldr r7,=0x6d37c2a9
str r7,[r5,#4]
str r0,[r5]
ldrsh r0,[r5,r6]
ldr r1,[r5,#4]
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#16
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_arm_str_alignment
.type cpu_arm7_arm_str_alignment, %function
cpu_arm7_arm_str_alignment:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#16
mov r5,sp
mov r6,r1
ldr r7,=0x6d37c2a9
str r7,[r5,#4]
ldr r7,=0xa5a5a5a5
str r7,[r5]
str r0,[r5,r6]
ldr r0,[r5]
ldr r1,[r5,#4]
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#16
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_arm_strh_alignment
.type cpu_arm7_arm_strh_alignment, %function
cpu_arm7_arm_strh_alignment:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#16
mov r5,sp
mov r6,r1
ldr r7,=0x6d37c2a9
str r7,[r5,#4]
ldr r7,=0xa5a5a5a5
str r7,[r5]
strh r0,[r5,r6]
ldr r0,[r5]
ldr r1,[r5,#4]
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#16
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_ldr_alignment
.type cpu_arm7_thumb_ldr_alignment, %function
cpu_arm7_thumb_ldr_alignment:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#16
mov r5,sp
mov r6,r1
ldr r7,=0x6d37c2a9
str r7,[r5,#4]
str r0,[r5]
adr r7,1f
orr r7,r7,#1
adr r8,2f
bx r7
.thumb
.balign 4
1:
ldr r0,[r5,r6]
bx r8
.arm
.balign 4
2:
ldr r1,[r5,#4]
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#16
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_ldrh_alignment
.type cpu_arm7_thumb_ldrh_alignment, %function
cpu_arm7_thumb_ldrh_alignment:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#16
mov r5,sp
mov r6,r1
ldr r7,=0x6d37c2a9
str r7,[r5,#4]
str r0,[r5]
adr r7,1f
orr r7,r7,#1
adr r8,2f
bx r7
.thumb
.balign 4
1:
ldrh r0,[r5,r6]
bx r8
.arm
.balign 4
2:
ldr r1,[r5,#4]
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#16
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_ldrsh_alignment
.type cpu_arm7_thumb_ldrsh_alignment, %function
cpu_arm7_thumb_ldrsh_alignment:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#16
mov r5,sp
mov r6,r1
ldr r7,=0x6d37c2a9
str r7,[r5,#4]
str r0,[r5]
adr r7,1f
orr r7,r7,#1
adr r8,2f
bx r7
.thumb
.balign 4
1:
ldrsh r0,[r5,r6]
bx r8
.arm
.balign 4
2:
ldr r1,[r5,#4]
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#16
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_str_alignment
.type cpu_arm7_thumb_str_alignment, %function
cpu_arm7_thumb_str_alignment:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#16
mov r5,sp
mov r6,r1
ldr r7,=0x6d37c2a9
str r7,[r5,#4]
ldr r7,=0xa5a5a5a5
str r7,[r5]
adr r7,1f
orr r7,r7,#1
adr r8,2f
bx r7
.thumb
.balign 4
1:
str r0,[r5,r6]
ldr r0,[r5]
bx r8
.arm
.balign 4
2:
ldr r1,[r5,#4]
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#16
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_strh_alignment
.type cpu_arm7_thumb_strh_alignment, %function
cpu_arm7_thumb_strh_alignment:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#16
mov r5,sp
mov r6,r1
ldr r7,=0x6d37c2a9
str r7,[r5,#4]
ldr r7,=0xa5a5a5a5
str r7,[r5]
adr r7,1f
orr r7,r7,#1
adr r8,2f
bx r7
.thumb
.balign 4
1:
strh r0,[r5,r6]
ldr r0,[r5]
bx r8
.arm
.balign 4
2:
ldr r1,[r5,#4]
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#16
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_arm_swp_alignment
.type cpu_arm7_arm_swp_alignment, %function
cpu_arm7_arm_swp_alignment:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#16
mov r5,sp
str r0,[r5]
add r6,r5,r1
mvn r7,r0
swp r0,r7,[r6]
ldr r1,[r5]
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#16
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_arm_pc_mov
.type cpu_arm7_arm_pc_mov, %function
cpu_arm7_arm_pc_mov:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#16
adr r5,1f
mov r1,#0
mov r2,#0
mov r7,sp
1:
mov r0,pc
sub r0,r0,r5
mov r1,#0
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#16
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_arm_pc_shift_rm
.type cpu_arm7_arm_pc_shift_rm, %function
cpu_arm7_arm_pc_shift_rm:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#16
adr r5,1f
mov r1,#0
mov r2,#0
mov r7,sp
1:
mov r0,pc,lsl r1
sub r0,r0,r5
mov r1,#0
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#16
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_arm_pc_shift_rn
.type cpu_arm7_arm_pc_shift_rn, %function
cpu_arm7_arm_pc_shift_rn:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#16
adr r5,1f
mov r1,#0
mov r2,#0
mov r7,sp
1:
add r0,pc,r1,lsl r2
sub r0,r0,r5
mov r1,#0
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#16
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_arm_pc_str
.type cpu_arm7_arm_pc_str, %function
cpu_arm7_arm_pc_str:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#16
adr r5,1f
mov r1,#0
mov r2,#0
mov r7,sp
1:
str pc,[sp]
ldr r0,[sp]
sub r0,r0,r5
mov r1,#0
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#16
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_arm_pc_stm
.type cpu_arm7_arm_pc_stm, %function
cpu_arm7_arm_pc_stm:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#16
adr r5,1f
mov r1,#0
mov r2,#0
mov r7,sp
1:
stmia r7,{pc}
ldr r0,[sp]
sub r0,r0,r5
mov r1,#0
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#16
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_pc_mov_0
.type cpu_arm7_thumb_pc_mov_0, %function
cpu_arm7_thumb_pc_mov_0:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#16
mov r0,#0
adr r5,2f
adr r6,3f
adr r7,1f
orr r7,r7,#1
bx r7
.thumb
.balign 4
1:
2:
mov r0,pc
bx r6
.arm
.balign 4
3:
sub r0,r0,r5
mov r1,#0
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#16
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_pc_mov_2
.type cpu_arm7_thumb_pc_mov_2, %function
cpu_arm7_thumb_pc_mov_2:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#16
mov r0,#0
adr r5,2f
adr r6,3f
adr r7,1f
orr r7,r7,#1
bx r7
.thumb
.balign 4
1:
nop
2:
mov r0,pc
bx r6
.arm
.balign 4
3:
sub r0,r0,r5
mov r1,#0
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#16
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_pc_add_0
.type cpu_arm7_thumb_pc_add_0, %function
cpu_arm7_thumb_pc_add_0:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#16
mov r0,#0
adr r5,2f
adr r6,3f
adr r7,1f
orr r7,r7,#1
bx r7
.thumb
.balign 4
1:
2:
add r0,pc
bx r6
.arm
.balign 4
3:
sub r0,r0,r5
mov r1,#0
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#16
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_pc_add_2
.type cpu_arm7_thumb_pc_add_2, %function
cpu_arm7_thumb_pc_add_2:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#16
mov r0,#0
adr r5,2f
adr r6,3f
adr r7,1f
orr r7,r7,#1
bx r7
.thumb
.balign 4
1:
nop
2:
add r0,pc
bx r6
.arm
.balign 4
3:
sub r0,r0,r5
mov r1,#0
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#16
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_arm_ldr_pc_no_exchange
.type cpu_arm7_arm_ldr_pc_no_exchange, %function
cpu_arm7_arm_ldr_pc_no_exchange:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#16
adr r0,1f
orr r0,r0,#1
str r0,[sp]
ldr pc,[sp]
.balign 4
1:
mov r0,#0x79
mrs r1,cpsr
and r1,r1,#0x20
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#16
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_arm_ldm_pc_no_exchange
.type cpu_arm7_arm_ldm_pc_no_exchange, %function
cpu_arm7_arm_ldm_pc_no_exchange:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#16
adr r0,1f
orr r0,r0,#1
str r0,[sp]
ldmia sp,{pc}
.balign 4
1:
mov r0,#0x79
mrs r1,cpsr
and r1,r1,#0x20
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#16
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_pop_pc_no_exchange
.type cpu_arm7_thumb_pop_pc_no_exchange, %function
cpu_arm7_thumb_pop_pc_no_exchange:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#16
mov r5,sp
adr r0,2f
str r0,[sp,#-4]!
adr r6,3f
adr r7,1f
orr r7,r7,#1
bx r7
.thumb
.balign 4
1:
pop {pc}
.balign 4
2:
movs r0,#0x7d
bx r6
.arm
.balign 4
3:
sub r1,sp,r5
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#16
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_bank_sp_sys_fiq
.type cpu_arm7_bank_sp_sys_fiq, %function
cpu_arm7_bank_sp_sys_fiq:
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
.global cpu_arm7_bank_lr_sys_fiq
.type cpu_arm7_bank_lr_sys_fiq, %function
cpu_arm7_bank_lr_sys_fiq:
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
.global cpu_arm7_bank_sp_sys_irq
.type cpu_arm7_bank_sp_sys_irq, %function
cpu_arm7_bank_sp_sys_irq:
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
.global cpu_arm7_bank_lr_sys_irq
.type cpu_arm7_bank_lr_sys_irq, %function
cpu_arm7_bank_lr_sys_irq:
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
.global cpu_arm7_bank_sp_sys_svc
.type cpu_arm7_bank_sp_sys_svc, %function
cpu_arm7_bank_sp_sys_svc:
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
.global cpu_arm7_bank_lr_sys_svc
.type cpu_arm7_bank_lr_sys_svc, %function
cpu_arm7_bank_lr_sys_svc:
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
.global cpu_arm7_bank_sp_sys_abt
.type cpu_arm7_bank_sp_sys_abt, %function
cpu_arm7_bank_sp_sys_abt:
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
.global cpu_arm7_bank_lr_sys_abt
.type cpu_arm7_bank_lr_sys_abt, %function
cpu_arm7_bank_lr_sys_abt:
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
.global cpu_arm7_bank_sp_sys_und
.type cpu_arm7_bank_sp_sys_und, %function
cpu_arm7_bank_sp_sys_und:
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
.global cpu_arm7_bank_lr_sys_und
.type cpu_arm7_bank_lr_sys_und, %function
cpu_arm7_bank_lr_sys_und:
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
.global cpu_arm7_bank_sp_fiq_sys
.type cpu_arm7_bank_sp_fiq_sys, %function
cpu_arm7_bank_sp_fiq_sys:
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
.global cpu_arm7_bank_lr_fiq_sys
.type cpu_arm7_bank_lr_fiq_sys, %function
cpu_arm7_bank_lr_fiq_sys:
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
.global cpu_arm7_bank_sp_fiq_irq
.type cpu_arm7_bank_sp_fiq_irq, %function
cpu_arm7_bank_sp_fiq_irq:
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
.global cpu_arm7_bank_lr_fiq_irq
.type cpu_arm7_bank_lr_fiq_irq, %function
cpu_arm7_bank_lr_fiq_irq:
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
.global cpu_arm7_bank_sp_fiq_svc
.type cpu_arm7_bank_sp_fiq_svc, %function
cpu_arm7_bank_sp_fiq_svc:
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
.global cpu_arm7_bank_lr_fiq_svc
.type cpu_arm7_bank_lr_fiq_svc, %function
cpu_arm7_bank_lr_fiq_svc:
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
.global cpu_arm7_bank_sp_fiq_abt
.type cpu_arm7_bank_sp_fiq_abt, %function
cpu_arm7_bank_sp_fiq_abt:
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
.global cpu_arm7_bank_lr_fiq_abt
.type cpu_arm7_bank_lr_fiq_abt, %function
cpu_arm7_bank_lr_fiq_abt:
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
.global cpu_arm7_bank_sp_fiq_und
.type cpu_arm7_bank_sp_fiq_und, %function
cpu_arm7_bank_sp_fiq_und:
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
.global cpu_arm7_bank_lr_fiq_und
.type cpu_arm7_bank_lr_fiq_und, %function
cpu_arm7_bank_lr_fiq_und:
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
.global cpu_arm7_bank_sp_irq_sys
.type cpu_arm7_bank_sp_irq_sys, %function
cpu_arm7_bank_sp_irq_sys:
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
.global cpu_arm7_bank_lr_irq_sys
.type cpu_arm7_bank_lr_irq_sys, %function
cpu_arm7_bank_lr_irq_sys:
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
.global cpu_arm7_bank_sp_irq_fiq
.type cpu_arm7_bank_sp_irq_fiq, %function
cpu_arm7_bank_sp_irq_fiq:
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
.global cpu_arm7_bank_lr_irq_fiq
.type cpu_arm7_bank_lr_irq_fiq, %function
cpu_arm7_bank_lr_irq_fiq:
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
.global cpu_arm7_bank_sp_irq_svc
.type cpu_arm7_bank_sp_irq_svc, %function
cpu_arm7_bank_sp_irq_svc:
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
.global cpu_arm7_bank_lr_irq_svc
.type cpu_arm7_bank_lr_irq_svc, %function
cpu_arm7_bank_lr_irq_svc:
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
.global cpu_arm7_bank_sp_irq_abt
.type cpu_arm7_bank_sp_irq_abt, %function
cpu_arm7_bank_sp_irq_abt:
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
.global cpu_arm7_bank_lr_irq_abt
.type cpu_arm7_bank_lr_irq_abt, %function
cpu_arm7_bank_lr_irq_abt:
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
.global cpu_arm7_bank_sp_irq_und
.type cpu_arm7_bank_sp_irq_und, %function
cpu_arm7_bank_sp_irq_und:
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
.global cpu_arm7_bank_lr_irq_und
.type cpu_arm7_bank_lr_irq_und, %function
cpu_arm7_bank_lr_irq_und:
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
.global cpu_arm7_bank_sp_svc_sys
.type cpu_arm7_bank_sp_svc_sys, %function
cpu_arm7_bank_sp_svc_sys:
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
.global cpu_arm7_bank_lr_svc_sys
.type cpu_arm7_bank_lr_svc_sys, %function
cpu_arm7_bank_lr_svc_sys:
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
.global cpu_arm7_bank_sp_svc_fiq
.type cpu_arm7_bank_sp_svc_fiq, %function
cpu_arm7_bank_sp_svc_fiq:
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
.global cpu_arm7_bank_lr_svc_fiq
.type cpu_arm7_bank_lr_svc_fiq, %function
cpu_arm7_bank_lr_svc_fiq:
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
.global cpu_arm7_bank_sp_svc_irq
.type cpu_arm7_bank_sp_svc_irq, %function
cpu_arm7_bank_sp_svc_irq:
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
.global cpu_arm7_bank_lr_svc_irq
.type cpu_arm7_bank_lr_svc_irq, %function
cpu_arm7_bank_lr_svc_irq:
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
.global cpu_arm7_bank_sp_svc_abt
.type cpu_arm7_bank_sp_svc_abt, %function
cpu_arm7_bank_sp_svc_abt:
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
.global cpu_arm7_bank_lr_svc_abt
.type cpu_arm7_bank_lr_svc_abt, %function
cpu_arm7_bank_lr_svc_abt:
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
.global cpu_arm7_bank_sp_svc_und
.type cpu_arm7_bank_sp_svc_und, %function
cpu_arm7_bank_sp_svc_und:
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
.global cpu_arm7_bank_lr_svc_und
.type cpu_arm7_bank_lr_svc_und, %function
cpu_arm7_bank_lr_svc_und:
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
.global cpu_arm7_bank_sp_abt_sys
.type cpu_arm7_bank_sp_abt_sys, %function
cpu_arm7_bank_sp_abt_sys:
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
.global cpu_arm7_bank_lr_abt_sys
.type cpu_arm7_bank_lr_abt_sys, %function
cpu_arm7_bank_lr_abt_sys:
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
.global cpu_arm7_bank_sp_abt_fiq
.type cpu_arm7_bank_sp_abt_fiq, %function
cpu_arm7_bank_sp_abt_fiq:
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
.global cpu_arm7_bank_lr_abt_fiq
.type cpu_arm7_bank_lr_abt_fiq, %function
cpu_arm7_bank_lr_abt_fiq:
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
.global cpu_arm7_bank_sp_abt_irq
.type cpu_arm7_bank_sp_abt_irq, %function
cpu_arm7_bank_sp_abt_irq:
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
.global cpu_arm7_bank_lr_abt_irq
.type cpu_arm7_bank_lr_abt_irq, %function
cpu_arm7_bank_lr_abt_irq:
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
.global cpu_arm7_bank_sp_abt_svc
.type cpu_arm7_bank_sp_abt_svc, %function
cpu_arm7_bank_sp_abt_svc:
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
.global cpu_arm7_bank_lr_abt_svc
.type cpu_arm7_bank_lr_abt_svc, %function
cpu_arm7_bank_lr_abt_svc:
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
.global cpu_arm7_bank_sp_abt_und
.type cpu_arm7_bank_sp_abt_und, %function
cpu_arm7_bank_sp_abt_und:
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
.global cpu_arm7_bank_lr_abt_und
.type cpu_arm7_bank_lr_abt_und, %function
cpu_arm7_bank_lr_abt_und:
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
.global cpu_arm7_bank_sp_und_sys
.type cpu_arm7_bank_sp_und_sys, %function
cpu_arm7_bank_sp_und_sys:
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
.global cpu_arm7_bank_lr_und_sys
.type cpu_arm7_bank_lr_und_sys, %function
cpu_arm7_bank_lr_und_sys:
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
.global cpu_arm7_bank_sp_und_fiq
.type cpu_arm7_bank_sp_und_fiq, %function
cpu_arm7_bank_sp_und_fiq:
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
.global cpu_arm7_bank_lr_und_fiq
.type cpu_arm7_bank_lr_und_fiq, %function
cpu_arm7_bank_lr_und_fiq:
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
.global cpu_arm7_bank_sp_und_irq
.type cpu_arm7_bank_sp_und_irq, %function
cpu_arm7_bank_sp_und_irq:
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
.global cpu_arm7_bank_lr_und_irq
.type cpu_arm7_bank_lr_und_irq, %function
cpu_arm7_bank_lr_und_irq:
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
.global cpu_arm7_bank_sp_und_svc
.type cpu_arm7_bank_sp_und_svc, %function
cpu_arm7_bank_sp_und_svc:
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
.global cpu_arm7_bank_lr_und_svc
.type cpu_arm7_bank_lr_und_svc, %function
cpu_arm7_bank_lr_und_svc:
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
.global cpu_arm7_bank_sp_und_abt
.type cpu_arm7_bank_sp_und_abt, %function
cpu_arm7_bank_sp_und_abt:
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
.global cpu_arm7_bank_lr_und_abt
.type cpu_arm7_bank_lr_und_abt, %function
cpu_arm7_bank_lr_und_abt:
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
.global cpu_arm7_bank_r8_sys_fiq
.type cpu_arm7_bank_r8_sys_fiq, %function
cpu_arm7_bank_r8_sys_fiq:
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
.global cpu_arm7_bank_r9_sys_fiq
.type cpu_arm7_bank_r9_sys_fiq, %function
cpu_arm7_bank_r9_sys_fiq:
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
.global cpu_arm7_bank_r10_sys_fiq
.type cpu_arm7_bank_r10_sys_fiq, %function
cpu_arm7_bank_r10_sys_fiq:
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
.global cpu_arm7_bank_r11_sys_fiq
.type cpu_arm7_bank_r11_sys_fiq, %function
cpu_arm7_bank_r11_sys_fiq:
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
.global cpu_arm7_bank_r12_sys_fiq
.type cpu_arm7_bank_r12_sys_fiq, %function
cpu_arm7_bank_r12_sys_fiq:
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
.global cpu_arm7_spsr_banks_fiq_irq
.type cpu_arm7_spsr_banks_fiq_irq, %function
cpu_arm7_spsr_banks_fiq_irq:
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
.global cpu_arm7_spsr_banks_fiq_svc
.type cpu_arm7_spsr_banks_fiq_svc, %function
cpu_arm7_spsr_banks_fiq_svc:
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
.global cpu_arm7_spsr_banks_fiq_abt
.type cpu_arm7_spsr_banks_fiq_abt, %function
cpu_arm7_spsr_banks_fiq_abt:
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
.global cpu_arm7_spsr_banks_fiq_und
.type cpu_arm7_spsr_banks_fiq_und, %function
cpu_arm7_spsr_banks_fiq_und:
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
.global cpu_arm7_spsr_banks_irq_fiq
.type cpu_arm7_spsr_banks_irq_fiq, %function
cpu_arm7_spsr_banks_irq_fiq:
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
.global cpu_arm7_spsr_banks_irq_svc
.type cpu_arm7_spsr_banks_irq_svc, %function
cpu_arm7_spsr_banks_irq_svc:
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
.global cpu_arm7_spsr_banks_irq_abt
.type cpu_arm7_spsr_banks_irq_abt, %function
cpu_arm7_spsr_banks_irq_abt:
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
.global cpu_arm7_spsr_banks_irq_und
.type cpu_arm7_spsr_banks_irq_und, %function
cpu_arm7_spsr_banks_irq_und:
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
.global cpu_arm7_spsr_banks_svc_fiq
.type cpu_arm7_spsr_banks_svc_fiq, %function
cpu_arm7_spsr_banks_svc_fiq:
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
.global cpu_arm7_spsr_banks_svc_irq
.type cpu_arm7_spsr_banks_svc_irq, %function
cpu_arm7_spsr_banks_svc_irq:
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
.global cpu_arm7_spsr_banks_svc_abt
.type cpu_arm7_spsr_banks_svc_abt, %function
cpu_arm7_spsr_banks_svc_abt:
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
.global cpu_arm7_spsr_banks_svc_und
.type cpu_arm7_spsr_banks_svc_und, %function
cpu_arm7_spsr_banks_svc_und:
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
.global cpu_arm7_spsr_banks_abt_fiq
.type cpu_arm7_spsr_banks_abt_fiq, %function
cpu_arm7_spsr_banks_abt_fiq:
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
.global cpu_arm7_spsr_banks_abt_irq
.type cpu_arm7_spsr_banks_abt_irq, %function
cpu_arm7_spsr_banks_abt_irq:
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
.global cpu_arm7_spsr_banks_abt_svc
.type cpu_arm7_spsr_banks_abt_svc, %function
cpu_arm7_spsr_banks_abt_svc:
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
.global cpu_arm7_spsr_banks_abt_und
.type cpu_arm7_spsr_banks_abt_und, %function
cpu_arm7_spsr_banks_abt_und:
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
.global cpu_arm7_spsr_banks_und_fiq
.type cpu_arm7_spsr_banks_und_fiq, %function
cpu_arm7_spsr_banks_und_fiq:
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
.global cpu_arm7_spsr_banks_und_irq
.type cpu_arm7_spsr_banks_und_irq, %function
cpu_arm7_spsr_banks_und_irq:
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
.global cpu_arm7_spsr_banks_und_svc
.type cpu_arm7_spsr_banks_und_svc, %function
cpu_arm7_spsr_banks_und_svc:
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
.global cpu_arm7_spsr_banks_und_abt
.type cpu_arm7_spsr_banks_und_abt, %function
cpu_arm7_spsr_banks_und_abt:
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
.global cpu_arm7_spsr_field_f_fiq
.type cpu_arm7_spsr_field_f_fiq, %function
cpu_arm7_spsr_field_f_fiq:
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
.global cpu_arm7_spsr_field_c_fiq
.type cpu_arm7_spsr_field_c_fiq, %function
cpu_arm7_spsr_field_c_fiq:
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
.global cpu_arm7_spsr_field_f_irq
.type cpu_arm7_spsr_field_f_irq, %function
cpu_arm7_spsr_field_f_irq:
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
.global cpu_arm7_spsr_field_c_irq
.type cpu_arm7_spsr_field_c_irq, %function
cpu_arm7_spsr_field_c_irq:
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
.global cpu_arm7_spsr_field_f_svc
.type cpu_arm7_spsr_field_f_svc, %function
cpu_arm7_spsr_field_f_svc:
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
.global cpu_arm7_spsr_field_c_svc
.type cpu_arm7_spsr_field_c_svc, %function
cpu_arm7_spsr_field_c_svc:
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
.global cpu_arm7_spsr_field_f_abt
.type cpu_arm7_spsr_field_f_abt, %function
cpu_arm7_spsr_field_f_abt:
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
.global cpu_arm7_spsr_field_c_abt
.type cpu_arm7_spsr_field_c_abt, %function
cpu_arm7_spsr_field_c_abt:
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
.global cpu_arm7_spsr_field_f_und
.type cpu_arm7_spsr_field_f_und, %function
cpu_arm7_spsr_field_f_und:
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
.global cpu_arm7_spsr_field_c_und
.type cpu_arm7_spsr_field_c_und, %function
cpu_arm7_spsr_field_c_und:
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
.global cpu_arm7_cpsr_mode_sys
.type cpu_arm7_cpsr_mode_sys, %function
cpu_arm7_cpsr_mode_sys:
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
.global cpu_arm7_cpsr_mode_fiq
.type cpu_arm7_cpsr_mode_fiq, %function
cpu_arm7_cpsr_mode_fiq:
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
.global cpu_arm7_cpsr_mode_irq
.type cpu_arm7_cpsr_mode_irq, %function
cpu_arm7_cpsr_mode_irq:
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
.global cpu_arm7_cpsr_mode_svc
.type cpu_arm7_cpsr_mode_svc, %function
cpu_arm7_cpsr_mode_svc:
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
.global cpu_arm7_cpsr_mode_abt
.type cpu_arm7_cpsr_mode_abt, %function
cpu_arm7_cpsr_mode_abt:
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
.global cpu_arm7_cpsr_mode_und
.type cpu_arm7_cpsr_mode_und, %function
cpu_arm7_cpsr_mode_und:
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
.global cpu_arm7_return_movs_fiq_arm
.type cpu_arm7_return_movs_fiq_arm, %function
cpu_arm7_return_movs_fiq_arm:
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
.global cpu_arm7_return_movs_fiq_thumb
.type cpu_arm7_return_movs_fiq_thumb, %function
cpu_arm7_return_movs_fiq_thumb:
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
.global cpu_arm7_return_subs_fiq_arm
.type cpu_arm7_return_subs_fiq_arm, %function
cpu_arm7_return_subs_fiq_arm:
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
.global cpu_arm7_return_subs_fiq_thumb
.type cpu_arm7_return_subs_fiq_thumb, %function
cpu_arm7_return_subs_fiq_thumb:
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
.global cpu_arm7_return_ldm_fiq_arm
.type cpu_arm7_return_ldm_fiq_arm, %function
cpu_arm7_return_ldm_fiq_arm:
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
.global cpu_arm7_return_ldm_fiq_thumb
.type cpu_arm7_return_ldm_fiq_thumb, %function
cpu_arm7_return_ldm_fiq_thumb:
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
.global cpu_arm7_return_movs_irq_arm
.type cpu_arm7_return_movs_irq_arm, %function
cpu_arm7_return_movs_irq_arm:
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
.global cpu_arm7_return_movs_irq_thumb
.type cpu_arm7_return_movs_irq_thumb, %function
cpu_arm7_return_movs_irq_thumb:
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
.global cpu_arm7_return_subs_irq_arm
.type cpu_arm7_return_subs_irq_arm, %function
cpu_arm7_return_subs_irq_arm:
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
.global cpu_arm7_return_subs_irq_thumb
.type cpu_arm7_return_subs_irq_thumb, %function
cpu_arm7_return_subs_irq_thumb:
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
.global cpu_arm7_return_ldm_irq_arm
.type cpu_arm7_return_ldm_irq_arm, %function
cpu_arm7_return_ldm_irq_arm:
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
.global cpu_arm7_return_ldm_irq_thumb
.type cpu_arm7_return_ldm_irq_thumb, %function
cpu_arm7_return_ldm_irq_thumb:
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
.global cpu_arm7_return_movs_svc_arm
.type cpu_arm7_return_movs_svc_arm, %function
cpu_arm7_return_movs_svc_arm:
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
.global cpu_arm7_return_movs_svc_thumb
.type cpu_arm7_return_movs_svc_thumb, %function
cpu_arm7_return_movs_svc_thumb:
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
.global cpu_arm7_return_subs_svc_arm
.type cpu_arm7_return_subs_svc_arm, %function
cpu_arm7_return_subs_svc_arm:
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
.global cpu_arm7_return_subs_svc_thumb
.type cpu_arm7_return_subs_svc_thumb, %function
cpu_arm7_return_subs_svc_thumb:
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
.global cpu_arm7_return_ldm_svc_arm
.type cpu_arm7_return_ldm_svc_arm, %function
cpu_arm7_return_ldm_svc_arm:
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
.global cpu_arm7_return_ldm_svc_thumb
.type cpu_arm7_return_ldm_svc_thumb, %function
cpu_arm7_return_ldm_svc_thumb:
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
.global cpu_arm7_return_movs_abt_arm
.type cpu_arm7_return_movs_abt_arm, %function
cpu_arm7_return_movs_abt_arm:
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
.global cpu_arm7_return_movs_abt_thumb
.type cpu_arm7_return_movs_abt_thumb, %function
cpu_arm7_return_movs_abt_thumb:
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
.global cpu_arm7_return_subs_abt_arm
.type cpu_arm7_return_subs_abt_arm, %function
cpu_arm7_return_subs_abt_arm:
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
.global cpu_arm7_return_subs_abt_thumb
.type cpu_arm7_return_subs_abt_thumb, %function
cpu_arm7_return_subs_abt_thumb:
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
.global cpu_arm7_return_ldm_abt_arm
.type cpu_arm7_return_ldm_abt_arm, %function
cpu_arm7_return_ldm_abt_arm:
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
.global cpu_arm7_return_ldm_abt_thumb
.type cpu_arm7_return_ldm_abt_thumb, %function
cpu_arm7_return_ldm_abt_thumb:
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
.global cpu_arm7_return_movs_und_arm
.type cpu_arm7_return_movs_und_arm, %function
cpu_arm7_return_movs_und_arm:
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
.global cpu_arm7_return_movs_und_thumb
.type cpu_arm7_return_movs_und_thumb, %function
cpu_arm7_return_movs_und_thumb:
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
.global cpu_arm7_return_subs_und_arm
.type cpu_arm7_return_subs_und_arm, %function
cpu_arm7_return_subs_und_arm:
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
.global cpu_arm7_return_subs_und_thumb
.type cpu_arm7_return_subs_und_thumb, %function
cpu_arm7_return_subs_und_thumb:
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
.global cpu_arm7_return_ldm_und_arm
.type cpu_arm7_return_ldm_und_arm, %function
cpu_arm7_return_ldm_und_arm:
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
.global cpu_arm7_return_ldm_und_thumb
.type cpu_arm7_return_ldm_und_thumb, %function
cpu_arm7_return_ldm_und_thumb:
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
.global cpu_arm7_irq_entry_arm
.type cpu_arm7_irq_entry_arm, %function
cpu_arm7_irq_entry_arm:
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
ldr r3,=0x0380fffc
ldr r2,[r3]
str r2,[r7,#8]
ldr r2,=cpu_arm7_timer_irq_handler
str r2,[r3]
ldr r3,=0x0400010c
ldr r2,[r3]
str r2,[r7,#12]
str r1,[r3]
ldr r8,=cpu_arm7_irq_capture
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
ldr r3,=0x0380fffc
ldr r2,[r7,#8]
str r2,[r3]
ldr r2,[r7,#4]
str r2,[r5,#16]
ldr r2,[r7]
str r2,[r5,#8]
ldr r3,=cpu_arm7_irq_capture
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
.global cpu_arm7_irq_entry_thumb
.type cpu_arm7_irq_entry_thumb, %function
cpu_arm7_irq_entry_thumb:
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
ldr r3,=0x0380fffc
ldr r2,[r3]
str r2,[r7,#8]
ldr r2,=cpu_arm7_timer_irq_handler
str r2,[r3]
ldr r3,=0x0400010c
ldr r2,[r3]
str r2,[r7,#12]
str r1,[r3]
ldr r8,=cpu_arm7_irq_capture
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
ldr r3,=0x0380fffc
ldr r2,[r7,#8]
str r2,[r3]
ldr r2,[r7,#4]
str r2,[r5,#16]
ldr r2,[r7]
str r2,[r5,#8]
ldr r3,=cpu_arm7_irq_capture
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
.global cpu_arm7_irq_entry_user
.type cpu_arm7_irq_entry_user, %function
cpu_arm7_irq_entry_user:
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
ldr r3,=0x0380fffc
ldr r2,[r3]
str r2,[r7,#8]
ldr r2,=cpu_arm7_timer_irq_handler
str r2,[r3]
ldr r3,=0x0400010c
ldr r2,[r3]
str r2,[r7,#12]
str r1,[r3]
ldr r8,=cpu_arm7_irq_capture
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
ldr r3,=0x0380fffc
ldr r2,[r7,#8]
str r2,[r3]
ldr r2,[r7,#4]
str r2,[r5,#16]
ldr r2,[r7]
str r2,[r5,#8]
ldr r3,=cpu_arm7_irq_capture
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
.global cpu_arm7_irq_entry_masked_i
.type cpu_arm7_irq_entry_masked_i, %function
cpu_arm7_irq_entry_masked_i:
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
ldr r3,=0x0380fffc
ldr r2,[r3]
str r2,[r7,#8]
ldr r2,=cpu_arm7_timer_irq_handler
str r2,[r3]
ldr r3,=0x0400010c
ldr r2,[r3]
str r2,[r7,#12]
str r1,[r3]
ldr r8,=cpu_arm7_irq_capture
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
ldr r0,=cpu_arm7_irq_capture
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
ldr r3,=0x0380fffc
ldr r2,[r7,#8]
str r2,[r3]
ldr r2,[r7,#4]
str r2,[r5,#16]
ldr r2,[r7]
str r2,[r5,#8]
ldr r3,=cpu_arm7_irq_capture
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
.global cpu_arm7_irq_entry_masked_ime
.type cpu_arm7_irq_entry_masked_ime, %function
cpu_arm7_irq_entry_masked_ime:
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
ldr r3,=0x0380fffc
ldr r2,[r3]
str r2,[r7,#8]
ldr r2,=cpu_arm7_timer_irq_handler
str r2,[r3]
ldr r3,=0x0400010c
ldr r2,[r3]
str r2,[r7,#12]
str r1,[r3]
ldr r8,=cpu_arm7_irq_capture
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
ldr r0,=cpu_arm7_irq_capture
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
ldr r3,=0x0380fffc
ldr r2,[r7,#8]
str r2,[r3]
ldr r2,[r7,#4]
str r2,[r5,#16]
ldr r2,[r7]
str r2,[r5,#8]
ldr r3,=cpu_arm7_irq_capture
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
.global cpu_arm7_irq_entry_masked_ie
.type cpu_arm7_irq_entry_masked_ie, %function
cpu_arm7_irq_entry_masked_ie:
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
ldr r3,=0x0380fffc
ldr r2,[r3]
str r2,[r7,#8]
ldr r2,=cpu_arm7_timer_irq_handler
str r2,[r3]
ldr r3,=0x0400010c
ldr r2,[r3]
str r2,[r7,#12]
str r1,[r3]
ldr r8,=cpu_arm7_irq_capture
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
ldr r0,=cpu_arm7_irq_capture
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
ldr r3,=0x0380fffc
ldr r2,[r7,#8]
str r2,[r3]
ldr r2,[r7,#4]
str r2,[r5,#16]
ldr r2,[r7]
str r2,[r5,#8]
ldr r3,=cpu_arm7_irq_capture
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
.global cpu_arm7_timer_irq_handler
cpu_arm7_timer_irq_handler:
ldr r0,=0x0400010c
mov r1,#0
strh r1,[r0,#2]
ldr r0,=0x04000214
mov r1,#64
str r1,[r0]
ldr r0,=cpu_arm7_irq_capture
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
cpu_arm7_irq_capture:
.space 12
.text
.arm
.balign 4
.global cpu_arm7_irq_nested_arm
.type cpu_arm7_irq_nested_arm, %function
cpu_arm7_irq_nested_arm:
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
ldr r3,=0x0380fffc
ldr r2,[r3]
str r2,[r7,#8]
ldr r2,=cpu_arm7_nested_handler
str r2,[r3]
ldr r3,=0x04000108
ldr r2,[r3]
str r2,[r7,#20]
str r1,[r3]
ldr r2,[r3,#4]
str r2,[r7,#12]
str r1,[r3,#4]
ldr r8,=cpu_arm7_nested_capture
str r1,[r8]
ldr r2,=0xbaadc0de
str r2,[r8,#4]
mov r2,#0x60
str r2,[r5,#16]
str r2,[r5,#20]
ldr r0,[r7,#16]
orr r0,r0,#0x5f
msr cpsr_fsxc,r0
mov r1,#1
str r1,[r5,#8]
ldr r3,=0x0400010c
ldr r2,=0x00c0ff80
ldr r8,=2f
b 9f
.ltorg
9:
str r2,[r3]
.rept 4096
nop
.endr
2:
mrs r8,cpsr
orr r0,r8,#0xc0
msr cpsr_c,r0
mov r1,#0
str r1,[r5,#8]
ldr r3,=0x04000108
str r1,[r3]
str r1,[r3,#4]
mov r1,#0x60
str r1,[r5,#20]
ldr r2,[r7,#20]
str r2,[r3]
ldr r2,[r7,#12]
str r2,[r3,#4]
ldr r3,=0x0380fffc
ldr r2,[r7,#8]
str r2,[r3]
ldr r2,[r7,#4]
str r2,[r5,#16]
ldr r2,[r7]
str r2,[r5,#8]
ldr r3,=cpu_arm7_nested_capture
ldr r0,[r3,#4]
ldr r2,=0xf00000ff
and r0,r0,r2
and r1,r8,r2
ldr r2,[r3]
cmp r2,#2
orrne r1,r1,#0x100
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
.global cpu_arm7_irq_nested_thumb
.type cpu_arm7_irq_nested_thumb, %function
cpu_arm7_irq_nested_thumb:
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
ldr r3,=0x0380fffc
ldr r2,[r3]
str r2,[r7,#8]
ldr r2,=cpu_arm7_nested_handler
str r2,[r3]
ldr r3,=0x04000108
ldr r2,[r3]
str r2,[r7,#20]
str r1,[r3]
ldr r2,[r3,#4]
str r2,[r7,#12]
str r1,[r3,#4]
ldr r8,=cpu_arm7_nested_capture
str r1,[r8]
ldr r2,=0xbaadc0de
str r2,[r8,#4]
mov r2,#0x60
str r2,[r5,#16]
str r2,[r5,#20]
ldr r0,[r7,#16]
orr r0,r0,#0x5f
msr cpsr_fsxc,r0
mov r1,#1
str r1,[r5,#8]
ldr r3,=0x0400010c
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
.rept 4096
nop
.endr
bx r8
.arm
.balign 4
2:
mrs r8,cpsr
orr r0,r8,#0xc0
msr cpsr_c,r0
mov r1,#0
str r1,[r5,#8]
ldr r3,=0x04000108
str r1,[r3]
str r1,[r3,#4]
mov r1,#0x60
str r1,[r5,#20]
ldr r2,[r7,#20]
str r2,[r3]
ldr r2,[r7,#12]
str r2,[r3,#4]
ldr r3,=0x0380fffc
ldr r2,[r7,#8]
str r2,[r3]
ldr r2,[r7,#4]
str r2,[r5,#16]
ldr r2,[r7]
str r2,[r5,#8]
ldr r3,=cpu_arm7_nested_capture
ldr r0,[r3,#4]
ldr r2,=0xf00000ff
and r0,r0,r2
and r1,r8,r2
ldr r2,[r3]
cmp r2,#2
orrne r1,r1,#0x100
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
.global cpu_arm7_nested_handler
cpu_arm7_nested_handler:
ldr r0,=cpu_arm7_nested_capture
ldr r1,[r0]
cmp r1,#0
bne 2f
push {r4-r11,lr}
mrs r4,spsr
mov r5,r0
mov r1,#1
str r1,[r5]
ldr r0,=0x0400010c
mov r1,#0
strh r1,[r0,#2]
ldr r0,=0x04000214
mov r1,#64
str r1,[r0]
and r0,r4,#0xf0000000
orr r0,r0,#0x52
msr cpsr_fsxc,r0
ldr r0,=0x04000108
ldr r1,=0x00c0ff80
b 9f
.ltorg
9:
str r1,[r0]
.rept 2048
nop
.endr
mrs r0,cpsr
orr r0,r0,#0x80
msr cpsr_c,r0
msr spsr_fsxc,r4
pop {r4-r11,lr}
bx lr
2:
mrs r1,spsr
str r1,[r0,#4]
mov r1,#2
str r1,[r0]
ldr r0,=0x04000108
mov r1,#0
strh r1,[r0,#2]
ldr r0,=0x04000214
mov r1,#32
str r1,[r0]
bx lr
.ltorg
.bss
.balign 4
cpu_arm7_nested_capture:
.space 8
.text
.arm
.balign 4
.global cpu_arm7_adc_register_operand2_lsl
.type cpu_arm7_adc_register_operand2_lsl, %function
cpu_arm7_adc_register_operand2_lsl:
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
.global cpu_arm7_adc_register_operand2_lsr
.type cpu_arm7_adc_register_operand2_lsr, %function
cpu_arm7_adc_register_operand2_lsr:
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
.global cpu_arm7_adc_register_operand2_asr
.type cpu_arm7_adc_register_operand2_asr, %function
cpu_arm7_adc_register_operand2_asr:
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
.global cpu_arm7_adc_register_operand2_ror
.type cpu_arm7_adc_register_operand2_ror, %function
cpu_arm7_adc_register_operand2_ror:
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
.global cpu_arm7_sbc_register_operand2_lsl
.type cpu_arm7_sbc_register_operand2_lsl, %function
cpu_arm7_sbc_register_operand2_lsl:
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
.global cpu_arm7_sbc_register_operand2_lsr
.type cpu_arm7_sbc_register_operand2_lsr, %function
cpu_arm7_sbc_register_operand2_lsr:
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
.global cpu_arm7_sbc_register_operand2_asr
.type cpu_arm7_sbc_register_operand2_asr, %function
cpu_arm7_sbc_register_operand2_asr:
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
.global cpu_arm7_sbc_register_operand2_ror
.type cpu_arm7_sbc_register_operand2_ror, %function
cpu_arm7_sbc_register_operand2_ror:
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
.global cpu_arm7_and_register_operand2_lsl
.type cpu_arm7_and_register_operand2_lsl, %function
cpu_arm7_and_register_operand2_lsl:
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
.global cpu_arm7_and_register_operand2_lsr
.type cpu_arm7_and_register_operand2_lsr, %function
cpu_arm7_and_register_operand2_lsr:
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
.global cpu_arm7_and_register_operand2_asr
.type cpu_arm7_and_register_operand2_asr, %function
cpu_arm7_and_register_operand2_asr:
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
.global cpu_arm7_and_register_operand2_ror
.type cpu_arm7_and_register_operand2_ror, %function
cpu_arm7_and_register_operand2_ror:
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
.global cpu_arm7_eor_register_operand2_lsl
.type cpu_arm7_eor_register_operand2_lsl, %function
cpu_arm7_eor_register_operand2_lsl:
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
.global cpu_arm7_eor_register_operand2_lsr
.type cpu_arm7_eor_register_operand2_lsr, %function
cpu_arm7_eor_register_operand2_lsr:
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
.global cpu_arm7_eor_register_operand2_asr
.type cpu_arm7_eor_register_operand2_asr, %function
cpu_arm7_eor_register_operand2_asr:
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
.global cpu_arm7_eor_register_operand2_ror
.type cpu_arm7_eor_register_operand2_ror, %function
cpu_arm7_eor_register_operand2_ror:
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
.global cpu_arm7_tst_register_operand2_lsl
.type cpu_arm7_tst_register_operand2_lsl, %function
cpu_arm7_tst_register_operand2_lsl:
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
.global cpu_arm7_tst_register_operand2_lsr
.type cpu_arm7_tst_register_operand2_lsr, %function
cpu_arm7_tst_register_operand2_lsr:
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
.global cpu_arm7_tst_register_operand2_asr
.type cpu_arm7_tst_register_operand2_asr, %function
cpu_arm7_tst_register_operand2_asr:
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
.global cpu_arm7_tst_register_operand2_ror
.type cpu_arm7_tst_register_operand2_ror, %function
cpu_arm7_tst_register_operand2_ror:
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
.global cpu_arm7_teq_register_operand2_lsl
.type cpu_arm7_teq_register_operand2_lsl, %function
cpu_arm7_teq_register_operand2_lsl:
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
.global cpu_arm7_teq_register_operand2_lsr
.type cpu_arm7_teq_register_operand2_lsr, %function
cpu_arm7_teq_register_operand2_lsr:
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
.global cpu_arm7_teq_register_operand2_asr
.type cpu_arm7_teq_register_operand2_asr, %function
cpu_arm7_teq_register_operand2_asr:
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
.global cpu_arm7_teq_register_operand2_ror
.type cpu_arm7_teq_register_operand2_ror, %function
cpu_arm7_teq_register_operand2_ror:
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
.global cpu_arm7_cmp_register_operand2_lsl
.type cpu_arm7_cmp_register_operand2_lsl, %function
cpu_arm7_cmp_register_operand2_lsl:
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
.global cpu_arm7_cmp_register_operand2_lsr
.type cpu_arm7_cmp_register_operand2_lsr, %function
cpu_arm7_cmp_register_operand2_lsr:
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
.global cpu_arm7_cmp_register_operand2_asr
.type cpu_arm7_cmp_register_operand2_asr, %function
cpu_arm7_cmp_register_operand2_asr:
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
.global cpu_arm7_cmp_register_operand2_ror
.type cpu_arm7_cmp_register_operand2_ror, %function
cpu_arm7_cmp_register_operand2_ror:
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
.global cpu_arm7_cmn_register_operand2_lsl
.type cpu_arm7_cmn_register_operand2_lsl, %function
cpu_arm7_cmn_register_operand2_lsl:
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
.global cpu_arm7_cmn_register_operand2_lsr
.type cpu_arm7_cmn_register_operand2_lsr, %function
cpu_arm7_cmn_register_operand2_lsr:
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
.global cpu_arm7_cmn_register_operand2_asr
.type cpu_arm7_cmn_register_operand2_asr, %function
cpu_arm7_cmn_register_operand2_asr:
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
.global cpu_arm7_cmn_register_operand2_ror
.type cpu_arm7_cmn_register_operand2_ror, %function
cpu_arm7_cmn_register_operand2_ror:
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
.global cpu_arm7_add_register_operand2_lsl
.type cpu_arm7_add_register_operand2_lsl, %function
cpu_arm7_add_register_operand2_lsl:
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
.global cpu_arm7_add_register_operand2_lsr
.type cpu_arm7_add_register_operand2_lsr, %function
cpu_arm7_add_register_operand2_lsr:
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
.global cpu_arm7_add_register_operand2_asr
.type cpu_arm7_add_register_operand2_asr, %function
cpu_arm7_add_register_operand2_asr:
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
.global cpu_arm7_add_register_operand2_ror
.type cpu_arm7_add_register_operand2_ror, %function
cpu_arm7_add_register_operand2_ror:
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
.global cpu_arm7_sub_register_operand2_lsl
.type cpu_arm7_sub_register_operand2_lsl, %function
cpu_arm7_sub_register_operand2_lsl:
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
.global cpu_arm7_sub_register_operand2_lsr
.type cpu_arm7_sub_register_operand2_lsr, %function
cpu_arm7_sub_register_operand2_lsr:
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
.global cpu_arm7_sub_register_operand2_asr
.type cpu_arm7_sub_register_operand2_asr, %function
cpu_arm7_sub_register_operand2_asr:
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
.global cpu_arm7_sub_register_operand2_ror
.type cpu_arm7_sub_register_operand2_ror, %function
cpu_arm7_sub_register_operand2_ror:
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
.global cpu_arm7_rsb_register_operand2_lsl
.type cpu_arm7_rsb_register_operand2_lsl, %function
cpu_arm7_rsb_register_operand2_lsl:
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
.global cpu_arm7_rsb_register_operand2_lsr
.type cpu_arm7_rsb_register_operand2_lsr, %function
cpu_arm7_rsb_register_operand2_lsr:
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
.global cpu_arm7_rsb_register_operand2_asr
.type cpu_arm7_rsb_register_operand2_asr, %function
cpu_arm7_rsb_register_operand2_asr:
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
.global cpu_arm7_rsb_register_operand2_ror
.type cpu_arm7_rsb_register_operand2_ror, %function
cpu_arm7_rsb_register_operand2_ror:
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
.global cpu_arm7_rsc_register_operand2_lsl
.type cpu_arm7_rsc_register_operand2_lsl, %function
cpu_arm7_rsc_register_operand2_lsl:
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
.global cpu_arm7_rsc_register_operand2_lsr
.type cpu_arm7_rsc_register_operand2_lsr, %function
cpu_arm7_rsc_register_operand2_lsr:
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
.global cpu_arm7_rsc_register_operand2_asr
.type cpu_arm7_rsc_register_operand2_asr, %function
cpu_arm7_rsc_register_operand2_asr:
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
.global cpu_arm7_rsc_register_operand2_ror
.type cpu_arm7_rsc_register_operand2_ror, %function
cpu_arm7_rsc_register_operand2_ror:
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
.global cpu_arm7_orr_register_operand2_lsl
.type cpu_arm7_orr_register_operand2_lsl, %function
cpu_arm7_orr_register_operand2_lsl:
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
.global cpu_arm7_orr_register_operand2_lsr
.type cpu_arm7_orr_register_operand2_lsr, %function
cpu_arm7_orr_register_operand2_lsr:
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
.global cpu_arm7_orr_register_operand2_asr
.type cpu_arm7_orr_register_operand2_asr, %function
cpu_arm7_orr_register_operand2_asr:
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
.global cpu_arm7_orr_register_operand2_ror
.type cpu_arm7_orr_register_operand2_ror, %function
cpu_arm7_orr_register_operand2_ror:
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
.global cpu_arm7_bic_register_operand2_lsl
.type cpu_arm7_bic_register_operand2_lsl, %function
cpu_arm7_bic_register_operand2_lsl:
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
.global cpu_arm7_bic_register_operand2_lsr
.type cpu_arm7_bic_register_operand2_lsr, %function
cpu_arm7_bic_register_operand2_lsr:
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
.global cpu_arm7_bic_register_operand2_asr
.type cpu_arm7_bic_register_operand2_asr, %function
cpu_arm7_bic_register_operand2_asr:
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
.global cpu_arm7_bic_register_operand2_ror
.type cpu_arm7_bic_register_operand2_ror, %function
cpu_arm7_bic_register_operand2_ror:
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
.global cpu_arm7_mov_register_operand2_lsl
.type cpu_arm7_mov_register_operand2_lsl, %function
cpu_arm7_mov_register_operand2_lsl:
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
.global cpu_arm7_mov_register_operand2_lsr
.type cpu_arm7_mov_register_operand2_lsr, %function
cpu_arm7_mov_register_operand2_lsr:
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
.global cpu_arm7_mov_register_operand2_asr
.type cpu_arm7_mov_register_operand2_asr, %function
cpu_arm7_mov_register_operand2_asr:
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
.global cpu_arm7_mov_register_operand2_ror
.type cpu_arm7_mov_register_operand2_ror, %function
cpu_arm7_mov_register_operand2_ror:
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
.global cpu_arm7_mvn_register_operand2_lsl
.type cpu_arm7_mvn_register_operand2_lsl, %function
cpu_arm7_mvn_register_operand2_lsl:
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
.global cpu_arm7_mvn_register_operand2_lsr
.type cpu_arm7_mvn_register_operand2_lsr, %function
cpu_arm7_mvn_register_operand2_lsr:
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
.global cpu_arm7_mvn_register_operand2_asr
.type cpu_arm7_mvn_register_operand2_asr, %function
cpu_arm7_mvn_register_operand2_asr:
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
.global cpu_arm7_mvn_register_operand2_ror
.type cpu_arm7_mvn_register_operand2_ror, %function
cpu_arm7_mvn_register_operand2_ror:
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
.global cpu_arm7_thumb_edges_lsl_distinct
.type cpu_arm7_thumb_edges_lsl_distinct, %function
cpu_arm7_thumb_edges_lsl_distinct:
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
.global cpu_arm7_thumb_edges_lsl_distinct_instruction
cpu_arm7_thumb_edges_lsl_distinct_instruction:
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
.global cpu_arm7_thumb_edges_lsl_alias
.type cpu_arm7_thumb_edges_lsl_alias, %function
cpu_arm7_thumb_edges_lsl_alias:
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
.global cpu_arm7_thumb_edges_lsl_alias_instruction
cpu_arm7_thumb_edges_lsl_alias_instruction:
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
.global cpu_arm7_thumb_edges_lsr_distinct
.type cpu_arm7_thumb_edges_lsr_distinct, %function
cpu_arm7_thumb_edges_lsr_distinct:
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
.global cpu_arm7_thumb_edges_lsr_distinct_instruction
cpu_arm7_thumb_edges_lsr_distinct_instruction:
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
.global cpu_arm7_thumb_edges_lsr_alias
.type cpu_arm7_thumb_edges_lsr_alias, %function
cpu_arm7_thumb_edges_lsr_alias:
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
.global cpu_arm7_thumb_edges_lsr_alias_instruction
cpu_arm7_thumb_edges_lsr_alias_instruction:
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
.global cpu_arm7_thumb_edges_asr_distinct
.type cpu_arm7_thumb_edges_asr_distinct, %function
cpu_arm7_thumb_edges_asr_distinct:
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
.global cpu_arm7_thumb_edges_asr_distinct_instruction
cpu_arm7_thumb_edges_asr_distinct_instruction:
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
.global cpu_arm7_thumb_edges_asr_alias
.type cpu_arm7_thumb_edges_asr_alias, %function
cpu_arm7_thumb_edges_asr_alias:
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
.global cpu_arm7_thumb_edges_asr_alias_instruction
cpu_arm7_thumb_edges_asr_alias_instruction:
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
.global cpu_arm7_thumb_edges_ror_distinct
.type cpu_arm7_thumb_edges_ror_distinct, %function
cpu_arm7_thumb_edges_ror_distinct:
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
.global cpu_arm7_thumb_edges_ror_distinct_instruction
cpu_arm7_thumb_edges_ror_distinct_instruction:
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
.global cpu_arm7_thumb_edges_ror_alias
.type cpu_arm7_thumb_edges_ror_alias, %function
cpu_arm7_thumb_edges_ror_alias:
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
.global cpu_arm7_thumb_edges_ror_alias_instruction
cpu_arm7_thumb_edges_ror_alias_instruction:
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
.global cpu_arm7_thumb_edges_adc_distinct
.type cpu_arm7_thumb_edges_adc_distinct, %function
cpu_arm7_thumb_edges_adc_distinct:
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
.global cpu_arm7_thumb_edges_adc_distinct_instruction
cpu_arm7_thumb_edges_adc_distinct_instruction:
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
.global cpu_arm7_thumb_edges_adc_alias
.type cpu_arm7_thumb_edges_adc_alias, %function
cpu_arm7_thumb_edges_adc_alias:
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
.global cpu_arm7_thumb_edges_adc_alias_instruction
cpu_arm7_thumb_edges_adc_alias_instruction:
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
.global cpu_arm7_thumb_edges_sbc_distinct
.type cpu_arm7_thumb_edges_sbc_distinct, %function
cpu_arm7_thumb_edges_sbc_distinct:
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
.global cpu_arm7_thumb_edges_sbc_distinct_instruction
cpu_arm7_thumb_edges_sbc_distinct_instruction:
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
.global cpu_arm7_thumb_edges_sbc_alias
.type cpu_arm7_thumb_edges_sbc_alias, %function
cpu_arm7_thumb_edges_sbc_alias:
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
.global cpu_arm7_thumb_edges_sbc_alias_instruction
cpu_arm7_thumb_edges_sbc_alias_instruction:
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
.global cpu_arm7_thumb_edges_neg_distinct
.type cpu_arm7_thumb_edges_neg_distinct, %function
cpu_arm7_thumb_edges_neg_distinct:
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
.global cpu_arm7_thumb_edges_neg_distinct_instruction
cpu_arm7_thumb_edges_neg_distinct_instruction:
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
.global cpu_arm7_thumb_edges_mul_distinct
.type cpu_arm7_thumb_edges_mul_distinct, %function
cpu_arm7_thumb_edges_mul_distinct:
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
.global cpu_arm7_thumb_edges_mul_distinct_instruction
cpu_arm7_thumb_edges_mul_distinct_instruction:
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
.global cpu_arm7_thumb_edges_mul_alias
.type cpu_arm7_thumb_edges_mul_alias, %function
cpu_arm7_thumb_edges_mul_alias:
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
.global cpu_arm7_thumb_edges_mul_alias_instruction
cpu_arm7_thumb_edges_mul_alias_instruction:
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
.arm
.balign 4
.global cpu_arm7_thumb_mem_ldr_immediate_0_r0
.type cpu_arm7_thumb_mem_ldr_immediate_0_r0, %function
cpu_arm7_thumb_mem_ldr_immediate_0_r0:
push {r4-r10,lr}
mov r4,r3
mrs r6,cpsr
mov r8,r2
mov r9,r0
mov r10,r1
sub sp,sp,#1152
mov r0,sp
mov r1,r10
mov r2,#288
9:
str r1,[r0],#4
subs r2,r2,#1
bne 9b
add sp,sp,#16
mov r7,sp
str r9,[r7,#0]
mvn r0,r9
mov r1,r7
ldr r2,=0
adr r5,2f
adr r3,1f
orr r3,r3,#1
msr cpsr_f,r8
bx r3
.thumb
.balign 4
1:
.global cpu_arm7_thumb_mem_ldr_immediate_0_r0_instruction
cpu_arm7_thumb_mem_ldr_immediate_0_r0_instruction:
ldr r0,[r1,#0]
nop
bx r5
nop
.arm
.balign 4
2:
mrs r3,cpsr
and r8,r3,#0xf0000000
mov r9,r0
cmp r1,r7
orrne r8,r8,#2
ldr r3,=0
cmp r2,r3
orrne r8,r8,#4
cmp sp,r7
orrne r8,r8,#8
ldr r0,[r7,#-4]
cmp r0,r10
orrne r8,r8,#1
ldr r0,[r7,#4]
cmp r0,r10
orrne r8,r8,#1
str r9,[r4]
str r8,[r4,#4]
mov sp,r7
sub sp,sp,#16
add sp,sp,#1152
msr cpsr_f,r6
pop {r4-r10,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_mem_ldr_immediate_4_r0
.type cpu_arm7_thumb_mem_ldr_immediate_4_r0, %function
cpu_arm7_thumb_mem_ldr_immediate_4_r0:
push {r4-r10,lr}
mov r4,r3
mrs r6,cpsr
mov r8,r2
mov r9,r0
mov r10,r1
sub sp,sp,#1152
mov r0,sp
mov r1,r10
mov r2,#288
9:
str r1,[r0],#4
subs r2,r2,#1
bne 9b
add sp,sp,#16
mov r7,sp
str r9,[r7,#4]
mvn r0,r9
mov r1,r7
ldr r2,=4
adr r5,2f
adr r3,1f
orr r3,r3,#1
msr cpsr_f,r8
bx r3
.thumb
.balign 4
1:
.global cpu_arm7_thumb_mem_ldr_immediate_4_r0_instruction
cpu_arm7_thumb_mem_ldr_immediate_4_r0_instruction:
ldr r0,[r1,#4]
nop
bx r5
nop
.arm
.balign 4
2:
mrs r3,cpsr
and r8,r3,#0xf0000000
mov r9,r0
cmp r1,r7
orrne r8,r8,#2
ldr r3,=4
cmp r2,r3
orrne r8,r8,#4
cmp sp,r7
orrne r8,r8,#8
ldr r0,[r7,#0]
cmp r0,r10
orrne r8,r8,#1
ldr r0,[r7,#8]
cmp r0,r10
orrne r8,r8,#1
str r9,[r4]
str r8,[r4,#4]
mov sp,r7
sub sp,sp,#16
add sp,sp,#1152
msr cpsr_f,r6
pop {r4-r10,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_mem_ldr_immediate_124_r0
.type cpu_arm7_thumb_mem_ldr_immediate_124_r0, %function
cpu_arm7_thumb_mem_ldr_immediate_124_r0:
push {r4-r10,lr}
mov r4,r3
mrs r6,cpsr
mov r8,r2
mov r9,r0
mov r10,r1
sub sp,sp,#1152
mov r0,sp
mov r1,r10
mov r2,#288
9:
str r1,[r0],#4
subs r2,r2,#1
bne 9b
add sp,sp,#16
mov r7,sp
str r9,[r7,#124]
mvn r0,r9
mov r1,r7
ldr r2,=124
adr r5,2f
adr r3,1f
orr r3,r3,#1
msr cpsr_f,r8
bx r3
.thumb
.balign 4
1:
.global cpu_arm7_thumb_mem_ldr_immediate_124_r0_instruction
cpu_arm7_thumb_mem_ldr_immediate_124_r0_instruction:
ldr r0,[r1,#124]
nop
bx r5
nop
.arm
.balign 4
2:
mrs r3,cpsr
and r8,r3,#0xf0000000
mov r9,r0
cmp r1,r7
orrne r8,r8,#2
ldr r3,=124
cmp r2,r3
orrne r8,r8,#4
cmp sp,r7
orrne r8,r8,#8
ldr r0,[r7,#120]
cmp r0,r10
orrne r8,r8,#1
ldr r0,[r7,#128]
cmp r0,r10
orrne r8,r8,#1
str r9,[r4]
str r8,[r4,#4]
mov sp,r7
sub sp,sp,#16
add sp,sp,#1152
msr cpsr_f,r6
pop {r4-r10,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_mem_ldrb_immediate_0_r0
.type cpu_arm7_thumb_mem_ldrb_immediate_0_r0, %function
cpu_arm7_thumb_mem_ldrb_immediate_0_r0:
push {r4-r10,lr}
mov r4,r3
mrs r6,cpsr
mov r8,r2
mov r9,r0
mov r10,r1
sub sp,sp,#1152
mov r0,sp
mov r1,r10
mov r2,#288
9:
str r1,[r0],#4
subs r2,r2,#1
bne 9b
add sp,sp,#16
mov r7,sp
str r9,[r7,#0]
mvn r0,r9
mov r1,r7
ldr r2,=0
adr r5,2f
adr r3,1f
orr r3,r3,#1
msr cpsr_f,r8
bx r3
.thumb
.balign 4
1:
.global cpu_arm7_thumb_mem_ldrb_immediate_0_r0_instruction
cpu_arm7_thumb_mem_ldrb_immediate_0_r0_instruction:
ldrb r0,[r1,#0]
nop
bx r5
nop
.arm
.balign 4
2:
mrs r3,cpsr
and r8,r3,#0xf0000000
mov r9,r0
cmp r1,r7
orrne r8,r8,#2
ldr r3,=0
cmp r2,r3
orrne r8,r8,#4
cmp sp,r7
orrne r8,r8,#8
ldr r0,[r7,#-4]
cmp r0,r10
orrne r8,r8,#1
ldr r0,[r7,#4]
cmp r0,r10
orrne r8,r8,#1
str r9,[r4]
str r8,[r4,#4]
mov sp,r7
sub sp,sp,#16
add sp,sp,#1152
msr cpsr_f,r6
pop {r4-r10,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_mem_ldrb_immediate_1_r0
.type cpu_arm7_thumb_mem_ldrb_immediate_1_r0, %function
cpu_arm7_thumb_mem_ldrb_immediate_1_r0:
push {r4-r10,lr}
mov r4,r3
mrs r6,cpsr
mov r8,r2
mov r9,r0
mov r10,r1
sub sp,sp,#1152
mov r0,sp
mov r1,r10
mov r2,#288
9:
str r1,[r0],#4
subs r2,r2,#1
bne 9b
add sp,sp,#16
mov r7,sp
str r9,[r7,#0]
mvn r0,r9
mov r1,r7
ldr r2,=1
adr r5,2f
adr r3,1f
orr r3,r3,#1
msr cpsr_f,r8
bx r3
.thumb
.balign 4
1:
.global cpu_arm7_thumb_mem_ldrb_immediate_1_r0_instruction
cpu_arm7_thumb_mem_ldrb_immediate_1_r0_instruction:
ldrb r0,[r1,#1]
nop
bx r5
nop
.arm
.balign 4
2:
mrs r3,cpsr
and r8,r3,#0xf0000000
mov r9,r0
cmp r1,r7
orrne r8,r8,#2
ldr r3,=1
cmp r2,r3
orrne r8,r8,#4
cmp sp,r7
orrne r8,r8,#8
ldr r0,[r7,#-4]
cmp r0,r10
orrne r8,r8,#1
ldr r0,[r7,#4]
cmp r0,r10
orrne r8,r8,#1
str r9,[r4]
str r8,[r4,#4]
mov sp,r7
sub sp,sp,#16
add sp,sp,#1152
msr cpsr_f,r6
pop {r4-r10,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_mem_ldrb_immediate_31_r0
.type cpu_arm7_thumb_mem_ldrb_immediate_31_r0, %function
cpu_arm7_thumb_mem_ldrb_immediate_31_r0:
push {r4-r10,lr}
mov r4,r3
mrs r6,cpsr
mov r8,r2
mov r9,r0
mov r10,r1
sub sp,sp,#1152
mov r0,sp
mov r1,r10
mov r2,#288
9:
str r1,[r0],#4
subs r2,r2,#1
bne 9b
add sp,sp,#16
mov r7,sp
str r9,[r7,#28]
mvn r0,r9
mov r1,r7
ldr r2,=31
adr r5,2f
adr r3,1f
orr r3,r3,#1
msr cpsr_f,r8
bx r3
.thumb
.balign 4
1:
.global cpu_arm7_thumb_mem_ldrb_immediate_31_r0_instruction
cpu_arm7_thumb_mem_ldrb_immediate_31_r0_instruction:
ldrb r0,[r1,#31]
nop
bx r5
nop
.arm
.balign 4
2:
mrs r3,cpsr
and r8,r3,#0xf0000000
mov r9,r0
cmp r1,r7
orrne r8,r8,#2
ldr r3,=31
cmp r2,r3
orrne r8,r8,#4
cmp sp,r7
orrne r8,r8,#8
ldr r0,[r7,#24]
cmp r0,r10
orrne r8,r8,#1
ldr r0,[r7,#32]
cmp r0,r10
orrne r8,r8,#1
str r9,[r4]
str r8,[r4,#4]
mov sp,r7
sub sp,sp,#16
add sp,sp,#1152
msr cpsr_f,r6
pop {r4-r10,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_mem_ldrh_immediate_0_r0
.type cpu_arm7_thumb_mem_ldrh_immediate_0_r0, %function
cpu_arm7_thumb_mem_ldrh_immediate_0_r0:
push {r4-r10,lr}
mov r4,r3
mrs r6,cpsr
mov r8,r2
mov r9,r0
mov r10,r1
sub sp,sp,#1152
mov r0,sp
mov r1,r10
mov r2,#288
9:
str r1,[r0],#4
subs r2,r2,#1
bne 9b
add sp,sp,#16
mov r7,sp
str r9,[r7,#0]
mvn r0,r9
mov r1,r7
ldr r2,=0
adr r5,2f
adr r3,1f
orr r3,r3,#1
msr cpsr_f,r8
bx r3
.thumb
.balign 4
1:
.global cpu_arm7_thumb_mem_ldrh_immediate_0_r0_instruction
cpu_arm7_thumb_mem_ldrh_immediate_0_r0_instruction:
ldrh r0,[r1,#0]
nop
bx r5
nop
.arm
.balign 4
2:
mrs r3,cpsr
and r8,r3,#0xf0000000
mov r9,r0
cmp r1,r7
orrne r8,r8,#2
ldr r3,=0
cmp r2,r3
orrne r8,r8,#4
cmp sp,r7
orrne r8,r8,#8
ldr r0,[r7,#-4]
cmp r0,r10
orrne r8,r8,#1
ldr r0,[r7,#4]
cmp r0,r10
orrne r8,r8,#1
str r9,[r4]
str r8,[r4,#4]
mov sp,r7
sub sp,sp,#16
add sp,sp,#1152
msr cpsr_f,r6
pop {r4-r10,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_mem_ldrh_immediate_2_r0
.type cpu_arm7_thumb_mem_ldrh_immediate_2_r0, %function
cpu_arm7_thumb_mem_ldrh_immediate_2_r0:
push {r4-r10,lr}
mov r4,r3
mrs r6,cpsr
mov r8,r2
mov r9,r0
mov r10,r1
sub sp,sp,#1152
mov r0,sp
mov r1,r10
mov r2,#288
9:
str r1,[r0],#4
subs r2,r2,#1
bne 9b
add sp,sp,#16
mov r7,sp
str r9,[r7,#0]
mvn r0,r9
mov r1,r7
ldr r2,=2
adr r5,2f
adr r3,1f
orr r3,r3,#1
msr cpsr_f,r8
bx r3
.thumb
.balign 4
1:
.global cpu_arm7_thumb_mem_ldrh_immediate_2_r0_instruction
cpu_arm7_thumb_mem_ldrh_immediate_2_r0_instruction:
ldrh r0,[r1,#2]
nop
bx r5
nop
.arm
.balign 4
2:
mrs r3,cpsr
and r8,r3,#0xf0000000
mov r9,r0
cmp r1,r7
orrne r8,r8,#2
ldr r3,=2
cmp r2,r3
orrne r8,r8,#4
cmp sp,r7
orrne r8,r8,#8
ldr r0,[r7,#-4]
cmp r0,r10
orrne r8,r8,#1
ldr r0,[r7,#4]
cmp r0,r10
orrne r8,r8,#1
str r9,[r4]
str r8,[r4,#4]
mov sp,r7
sub sp,sp,#16
add sp,sp,#1152
msr cpsr_f,r6
pop {r4-r10,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_mem_ldrh_immediate_62_r0
.type cpu_arm7_thumb_mem_ldrh_immediate_62_r0, %function
cpu_arm7_thumb_mem_ldrh_immediate_62_r0:
push {r4-r10,lr}
mov r4,r3
mrs r6,cpsr
mov r8,r2
mov r9,r0
mov r10,r1
sub sp,sp,#1152
mov r0,sp
mov r1,r10
mov r2,#288
9:
str r1,[r0],#4
subs r2,r2,#1
bne 9b
add sp,sp,#16
mov r7,sp
str r9,[r7,#60]
mvn r0,r9
mov r1,r7
ldr r2,=62
adr r5,2f
adr r3,1f
orr r3,r3,#1
msr cpsr_f,r8
bx r3
.thumb
.balign 4
1:
.global cpu_arm7_thumb_mem_ldrh_immediate_62_r0_instruction
cpu_arm7_thumb_mem_ldrh_immediate_62_r0_instruction:
ldrh r0,[r1,#62]
nop
bx r5
nop
.arm
.balign 4
2:
mrs r3,cpsr
and r8,r3,#0xf0000000
mov r9,r0
cmp r1,r7
orrne r8,r8,#2
ldr r3,=62
cmp r2,r3
orrne r8,r8,#4
cmp sp,r7
orrne r8,r8,#8
ldr r0,[r7,#56]
cmp r0,r10
orrne r8,r8,#1
ldr r0,[r7,#64]
cmp r0,r10
orrne r8,r8,#1
str r9,[r4]
str r8,[r4,#4]
mov sp,r7
sub sp,sp,#16
add sp,sp,#1152
msr cpsr_f,r6
pop {r4-r10,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_mem_str_immediate_0_r0
.type cpu_arm7_thumb_mem_str_immediate_0_r0, %function
cpu_arm7_thumb_mem_str_immediate_0_r0:
push {r4-r10,lr}
mov r4,r3
mrs r6,cpsr
mov r8,r2
mov r9,r0
mov r10,r1
sub sp,sp,#1152
mov r0,sp
mov r1,r10
mov r2,#288
9:
str r1,[r0],#4
subs r2,r2,#1
bne 9b
add sp,sp,#16
mov r7,sp
mov r0,r9
mov r1,r7
ldr r2,=0
adr r5,2f
adr r3,1f
orr r3,r3,#1
msr cpsr_f,r8
bx r3
.thumb
.balign 4
1:
.global cpu_arm7_thumb_mem_str_immediate_0_r0_instruction
cpu_arm7_thumb_mem_str_immediate_0_r0_instruction:
str r0,[r1,#0]
nop
bx r5
nop
.arm
.balign 4
2:
mrs r3,cpsr
and r8,r3,#0xf0000000
ldr r9,[r7,#0]
cmp r1,r7
orrne r8,r8,#2
ldr r3,=0
cmp r2,r3
orrne r8,r8,#4
cmp sp,r7
orrne r8,r8,#8
ldr r0,[r7,#-4]
cmp r0,r10
orrne r8,r8,#1
ldr r0,[r7,#4]
cmp r0,r10
orrne r8,r8,#1
str r9,[r4]
str r8,[r4,#4]
mov sp,r7
sub sp,sp,#16
add sp,sp,#1152
msr cpsr_f,r6
pop {r4-r10,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_mem_str_immediate_4_r0
.type cpu_arm7_thumb_mem_str_immediate_4_r0, %function
cpu_arm7_thumb_mem_str_immediate_4_r0:
push {r4-r10,lr}
mov r4,r3
mrs r6,cpsr
mov r8,r2
mov r9,r0
mov r10,r1
sub sp,sp,#1152
mov r0,sp
mov r1,r10
mov r2,#288
9:
str r1,[r0],#4
subs r2,r2,#1
bne 9b
add sp,sp,#16
mov r7,sp
mov r0,r9
mov r1,r7
ldr r2,=4
adr r5,2f
adr r3,1f
orr r3,r3,#1
msr cpsr_f,r8
bx r3
.thumb
.balign 4
1:
.global cpu_arm7_thumb_mem_str_immediate_4_r0_instruction
cpu_arm7_thumb_mem_str_immediate_4_r0_instruction:
str r0,[r1,#4]
nop
bx r5
nop
.arm
.balign 4
2:
mrs r3,cpsr
and r8,r3,#0xf0000000
ldr r9,[r7,#4]
cmp r1,r7
orrne r8,r8,#2
ldr r3,=4
cmp r2,r3
orrne r8,r8,#4
cmp sp,r7
orrne r8,r8,#8
ldr r0,[r7,#0]
cmp r0,r10
orrne r8,r8,#1
ldr r0,[r7,#8]
cmp r0,r10
orrne r8,r8,#1
str r9,[r4]
str r8,[r4,#4]
mov sp,r7
sub sp,sp,#16
add sp,sp,#1152
msr cpsr_f,r6
pop {r4-r10,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_mem_str_immediate_124_r0
.type cpu_arm7_thumb_mem_str_immediate_124_r0, %function
cpu_arm7_thumb_mem_str_immediate_124_r0:
push {r4-r10,lr}
mov r4,r3
mrs r6,cpsr
mov r8,r2
mov r9,r0
mov r10,r1
sub sp,sp,#1152
mov r0,sp
mov r1,r10
mov r2,#288
9:
str r1,[r0],#4
subs r2,r2,#1
bne 9b
add sp,sp,#16
mov r7,sp
mov r0,r9
mov r1,r7
ldr r2,=124
adr r5,2f
adr r3,1f
orr r3,r3,#1
msr cpsr_f,r8
bx r3
.thumb
.balign 4
1:
.global cpu_arm7_thumb_mem_str_immediate_124_r0_instruction
cpu_arm7_thumb_mem_str_immediate_124_r0_instruction:
str r0,[r1,#124]
nop
bx r5
nop
.arm
.balign 4
2:
mrs r3,cpsr
and r8,r3,#0xf0000000
ldr r9,[r7,#124]
cmp r1,r7
orrne r8,r8,#2
ldr r3,=124
cmp r2,r3
orrne r8,r8,#4
cmp sp,r7
orrne r8,r8,#8
ldr r0,[r7,#120]
cmp r0,r10
orrne r8,r8,#1
ldr r0,[r7,#128]
cmp r0,r10
orrne r8,r8,#1
str r9,[r4]
str r8,[r4,#4]
mov sp,r7
sub sp,sp,#16
add sp,sp,#1152
msr cpsr_f,r6
pop {r4-r10,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_mem_strb_immediate_0_r0
.type cpu_arm7_thumb_mem_strb_immediate_0_r0, %function
cpu_arm7_thumb_mem_strb_immediate_0_r0:
push {r4-r10,lr}
mov r4,r3
mrs r6,cpsr
mov r8,r2
mov r9,r0
mov r10,r1
sub sp,sp,#1152
mov r0,sp
mov r1,r10
mov r2,#288
9:
str r1,[r0],#4
subs r2,r2,#1
bne 9b
add sp,sp,#16
mov r7,sp
mov r0,r9
mov r1,r7
ldr r2,=0
adr r5,2f
adr r3,1f
orr r3,r3,#1
msr cpsr_f,r8
bx r3
.thumb
.balign 4
1:
.global cpu_arm7_thumb_mem_strb_immediate_0_r0_instruction
cpu_arm7_thumb_mem_strb_immediate_0_r0_instruction:
strb r0,[r1,#0]
nop
bx r5
nop
.arm
.balign 4
2:
mrs r3,cpsr
and r8,r3,#0xf0000000
ldr r9,[r7,#0]
cmp r1,r7
orrne r8,r8,#2
ldr r3,=0
cmp r2,r3
orrne r8,r8,#4
cmp sp,r7
orrne r8,r8,#8
ldr r0,[r7,#-4]
cmp r0,r10
orrne r8,r8,#1
ldr r0,[r7,#4]
cmp r0,r10
orrne r8,r8,#1
str r9,[r4]
str r8,[r4,#4]
mov sp,r7
sub sp,sp,#16
add sp,sp,#1152
msr cpsr_f,r6
pop {r4-r10,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_mem_strb_immediate_1_r0
.type cpu_arm7_thumb_mem_strb_immediate_1_r0, %function
cpu_arm7_thumb_mem_strb_immediate_1_r0:
push {r4-r10,lr}
mov r4,r3
mrs r6,cpsr
mov r8,r2
mov r9,r0
mov r10,r1
sub sp,sp,#1152
mov r0,sp
mov r1,r10
mov r2,#288
9:
str r1,[r0],#4
subs r2,r2,#1
bne 9b
add sp,sp,#16
mov r7,sp
mov r0,r9
mov r1,r7
ldr r2,=1
adr r5,2f
adr r3,1f
orr r3,r3,#1
msr cpsr_f,r8
bx r3
.thumb
.balign 4
1:
.global cpu_arm7_thumb_mem_strb_immediate_1_r0_instruction
cpu_arm7_thumb_mem_strb_immediate_1_r0_instruction:
strb r0,[r1,#1]
nop
bx r5
nop
.arm
.balign 4
2:
mrs r3,cpsr
and r8,r3,#0xf0000000
ldr r9,[r7,#0]
cmp r1,r7
orrne r8,r8,#2
ldr r3,=1
cmp r2,r3
orrne r8,r8,#4
cmp sp,r7
orrne r8,r8,#8
ldr r0,[r7,#-4]
cmp r0,r10
orrne r8,r8,#1
ldr r0,[r7,#4]
cmp r0,r10
orrne r8,r8,#1
str r9,[r4]
str r8,[r4,#4]
mov sp,r7
sub sp,sp,#16
add sp,sp,#1152
msr cpsr_f,r6
pop {r4-r10,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_mem_strb_immediate_31_r0
.type cpu_arm7_thumb_mem_strb_immediate_31_r0, %function
cpu_arm7_thumb_mem_strb_immediate_31_r0:
push {r4-r10,lr}
mov r4,r3
mrs r6,cpsr
mov r8,r2
mov r9,r0
mov r10,r1
sub sp,sp,#1152
mov r0,sp
mov r1,r10
mov r2,#288
9:
str r1,[r0],#4
subs r2,r2,#1
bne 9b
add sp,sp,#16
mov r7,sp
mov r0,r9
mov r1,r7
ldr r2,=31
adr r5,2f
adr r3,1f
orr r3,r3,#1
msr cpsr_f,r8
bx r3
.thumb
.balign 4
1:
.global cpu_arm7_thumb_mem_strb_immediate_31_r0_instruction
cpu_arm7_thumb_mem_strb_immediate_31_r0_instruction:
strb r0,[r1,#31]
nop
bx r5
nop
.arm
.balign 4
2:
mrs r3,cpsr
and r8,r3,#0xf0000000
ldr r9,[r7,#28]
cmp r1,r7
orrne r8,r8,#2
ldr r3,=31
cmp r2,r3
orrne r8,r8,#4
cmp sp,r7
orrne r8,r8,#8
ldr r0,[r7,#24]
cmp r0,r10
orrne r8,r8,#1
ldr r0,[r7,#32]
cmp r0,r10
orrne r8,r8,#1
str r9,[r4]
str r8,[r4,#4]
mov sp,r7
sub sp,sp,#16
add sp,sp,#1152
msr cpsr_f,r6
pop {r4-r10,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_mem_strh_immediate_0_r0
.type cpu_arm7_thumb_mem_strh_immediate_0_r0, %function
cpu_arm7_thumb_mem_strh_immediate_0_r0:
push {r4-r10,lr}
mov r4,r3
mrs r6,cpsr
mov r8,r2
mov r9,r0
mov r10,r1
sub sp,sp,#1152
mov r0,sp
mov r1,r10
mov r2,#288
9:
str r1,[r0],#4
subs r2,r2,#1
bne 9b
add sp,sp,#16
mov r7,sp
mov r0,r9
mov r1,r7
ldr r2,=0
adr r5,2f
adr r3,1f
orr r3,r3,#1
msr cpsr_f,r8
bx r3
.thumb
.balign 4
1:
.global cpu_arm7_thumb_mem_strh_immediate_0_r0_instruction
cpu_arm7_thumb_mem_strh_immediate_0_r0_instruction:
strh r0,[r1,#0]
nop
bx r5
nop
.arm
.balign 4
2:
mrs r3,cpsr
and r8,r3,#0xf0000000
ldr r9,[r7,#0]
cmp r1,r7
orrne r8,r8,#2
ldr r3,=0
cmp r2,r3
orrne r8,r8,#4
cmp sp,r7
orrne r8,r8,#8
ldr r0,[r7,#-4]
cmp r0,r10
orrne r8,r8,#1
ldr r0,[r7,#4]
cmp r0,r10
orrne r8,r8,#1
str r9,[r4]
str r8,[r4,#4]
mov sp,r7
sub sp,sp,#16
add sp,sp,#1152
msr cpsr_f,r6
pop {r4-r10,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_mem_strh_immediate_2_r0
.type cpu_arm7_thumb_mem_strh_immediate_2_r0, %function
cpu_arm7_thumb_mem_strh_immediate_2_r0:
push {r4-r10,lr}
mov r4,r3
mrs r6,cpsr
mov r8,r2
mov r9,r0
mov r10,r1
sub sp,sp,#1152
mov r0,sp
mov r1,r10
mov r2,#288
9:
str r1,[r0],#4
subs r2,r2,#1
bne 9b
add sp,sp,#16
mov r7,sp
mov r0,r9
mov r1,r7
ldr r2,=2
adr r5,2f
adr r3,1f
orr r3,r3,#1
msr cpsr_f,r8
bx r3
.thumb
.balign 4
1:
.global cpu_arm7_thumb_mem_strh_immediate_2_r0_instruction
cpu_arm7_thumb_mem_strh_immediate_2_r0_instruction:
strh r0,[r1,#2]
nop
bx r5
nop
.arm
.balign 4
2:
mrs r3,cpsr
and r8,r3,#0xf0000000
ldr r9,[r7,#0]
cmp r1,r7
orrne r8,r8,#2
ldr r3,=2
cmp r2,r3
orrne r8,r8,#4
cmp sp,r7
orrne r8,r8,#8
ldr r0,[r7,#-4]
cmp r0,r10
orrne r8,r8,#1
ldr r0,[r7,#4]
cmp r0,r10
orrne r8,r8,#1
str r9,[r4]
str r8,[r4,#4]
mov sp,r7
sub sp,sp,#16
add sp,sp,#1152
msr cpsr_f,r6
pop {r4-r10,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_mem_strh_immediate_62_r0
.type cpu_arm7_thumb_mem_strh_immediate_62_r0, %function
cpu_arm7_thumb_mem_strh_immediate_62_r0:
push {r4-r10,lr}
mov r4,r3
mrs r6,cpsr
mov r8,r2
mov r9,r0
mov r10,r1
sub sp,sp,#1152
mov r0,sp
mov r1,r10
mov r2,#288
9:
str r1,[r0],#4
subs r2,r2,#1
bne 9b
add sp,sp,#16
mov r7,sp
mov r0,r9
mov r1,r7
ldr r2,=62
adr r5,2f
adr r3,1f
orr r3,r3,#1
msr cpsr_f,r8
bx r3
.thumb
.balign 4
1:
.global cpu_arm7_thumb_mem_strh_immediate_62_r0_instruction
cpu_arm7_thumb_mem_strh_immediate_62_r0_instruction:
strh r0,[r1,#62]
nop
bx r5
nop
.arm
.balign 4
2:
mrs r3,cpsr
and r8,r3,#0xf0000000
ldr r9,[r7,#60]
cmp r1,r7
orrne r8,r8,#2
ldr r3,=62
cmp r2,r3
orrne r8,r8,#4
cmp sp,r7
orrne r8,r8,#8
ldr r0,[r7,#56]
cmp r0,r10
orrne r8,r8,#1
ldr r0,[r7,#64]
cmp r0,r10
orrne r8,r8,#1
str r9,[r4]
str r8,[r4,#4]
mov sp,r7
sub sp,sp,#16
add sp,sp,#1152
msr cpsr_f,r6
pop {r4-r10,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_mem_ldr_sp_0_r0
.type cpu_arm7_thumb_mem_ldr_sp_0_r0, %function
cpu_arm7_thumb_mem_ldr_sp_0_r0:
push {r4-r10,lr}
mov r4,r3
mrs r6,cpsr
mov r8,r2
mov r9,r0
mov r10,r1
sub sp,sp,#1152
mov r0,sp
mov r1,r10
mov r2,#288
9:
str r1,[r0],#4
subs r2,r2,#1
bne 9b
add sp,sp,#16
mov r7,sp
str r9,[r7,#0]
mvn r0,r9
mov r1,r7
ldr r2,=0
adr r5,2f
adr r3,1f
orr r3,r3,#1
msr cpsr_f,r8
bx r3
.thumb
.balign 4
1:
.global cpu_arm7_thumb_mem_ldr_sp_0_r0_instruction
cpu_arm7_thumb_mem_ldr_sp_0_r0_instruction:
ldr r0,[sp,#0]
nop
bx r5
nop
.arm
.balign 4
2:
mrs r3,cpsr
and r8,r3,#0xf0000000
mov r9,r0
cmp r1,r7
orrne r8,r8,#2
ldr r3,=0
cmp r2,r3
orrne r8,r8,#4
cmp sp,r7
orrne r8,r8,#8
ldr r0,[r7,#-4]
cmp r0,r10
orrne r8,r8,#1
ldr r0,[r7,#4]
cmp r0,r10
orrne r8,r8,#1
str r9,[r4]
str r8,[r4,#4]
mov sp,r7
sub sp,sp,#16
add sp,sp,#1152
msr cpsr_f,r6
pop {r4-r10,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_mem_ldr_sp_4_r0
.type cpu_arm7_thumb_mem_ldr_sp_4_r0, %function
cpu_arm7_thumb_mem_ldr_sp_4_r0:
push {r4-r10,lr}
mov r4,r3
mrs r6,cpsr
mov r8,r2
mov r9,r0
mov r10,r1
sub sp,sp,#1152
mov r0,sp
mov r1,r10
mov r2,#288
9:
str r1,[r0],#4
subs r2,r2,#1
bne 9b
add sp,sp,#16
mov r7,sp
str r9,[r7,#4]
mvn r0,r9
mov r1,r7
ldr r2,=4
adr r5,2f
adr r3,1f
orr r3,r3,#1
msr cpsr_f,r8
bx r3
.thumb
.balign 4
1:
.global cpu_arm7_thumb_mem_ldr_sp_4_r0_instruction
cpu_arm7_thumb_mem_ldr_sp_4_r0_instruction:
ldr r0,[sp,#4]
nop
bx r5
nop
.arm
.balign 4
2:
mrs r3,cpsr
and r8,r3,#0xf0000000
mov r9,r0
cmp r1,r7
orrne r8,r8,#2
ldr r3,=4
cmp r2,r3
orrne r8,r8,#4
cmp sp,r7
orrne r8,r8,#8
ldr r0,[r7,#0]
cmp r0,r10
orrne r8,r8,#1
ldr r0,[r7,#8]
cmp r0,r10
orrne r8,r8,#1
str r9,[r4]
str r8,[r4,#4]
mov sp,r7
sub sp,sp,#16
add sp,sp,#1152
msr cpsr_f,r6
pop {r4-r10,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_mem_ldr_sp_1020_r0
.type cpu_arm7_thumb_mem_ldr_sp_1020_r0, %function
cpu_arm7_thumb_mem_ldr_sp_1020_r0:
push {r4-r10,lr}
mov r4,r3
mrs r6,cpsr
mov r8,r2
mov r9,r0
mov r10,r1
sub sp,sp,#1152
mov r0,sp
mov r1,r10
mov r2,#288
9:
str r1,[r0],#4
subs r2,r2,#1
bne 9b
add sp,sp,#16
mov r7,sp
str r9,[r7,#1020]
mvn r0,r9
mov r1,r7
ldr r2,=1020
adr r5,2f
adr r3,1f
orr r3,r3,#1
msr cpsr_f,r8
bx r3
.thumb
.balign 4
1:
.global cpu_arm7_thumb_mem_ldr_sp_1020_r0_instruction
cpu_arm7_thumb_mem_ldr_sp_1020_r0_instruction:
ldr r0,[sp,#1020]
nop
bx r5
nop
.arm
.balign 4
2:
mrs r3,cpsr
and r8,r3,#0xf0000000
mov r9,r0
cmp r1,r7
orrne r8,r8,#2
ldr r3,=1020
cmp r2,r3
orrne r8,r8,#4
cmp sp,r7
orrne r8,r8,#8
ldr r0,[r7,#1016]
cmp r0,r10
orrne r8,r8,#1
ldr r0,[r7,#1024]
cmp r0,r10
orrne r8,r8,#1
str r9,[r4]
str r8,[r4,#4]
mov sp,r7
sub sp,sp,#16
add sp,sp,#1152
msr cpsr_f,r6
pop {r4-r10,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_mem_str_sp_0_r0
.type cpu_arm7_thumb_mem_str_sp_0_r0, %function
cpu_arm7_thumb_mem_str_sp_0_r0:
push {r4-r10,lr}
mov r4,r3
mrs r6,cpsr
mov r8,r2
mov r9,r0
mov r10,r1
sub sp,sp,#1152
mov r0,sp
mov r1,r10
mov r2,#288
9:
str r1,[r0],#4
subs r2,r2,#1
bne 9b
add sp,sp,#16
mov r7,sp
mov r0,r9
mov r1,r7
ldr r2,=0
adr r5,2f
adr r3,1f
orr r3,r3,#1
msr cpsr_f,r8
bx r3
.thumb
.balign 4
1:
.global cpu_arm7_thumb_mem_str_sp_0_r0_instruction
cpu_arm7_thumb_mem_str_sp_0_r0_instruction:
str r0,[sp,#0]
nop
bx r5
nop
.arm
.balign 4
2:
mrs r3,cpsr
and r8,r3,#0xf0000000
ldr r9,[r7,#0]
cmp r1,r7
orrne r8,r8,#2
ldr r3,=0
cmp r2,r3
orrne r8,r8,#4
cmp sp,r7
orrne r8,r8,#8
ldr r0,[r7,#-4]
cmp r0,r10
orrne r8,r8,#1
ldr r0,[r7,#4]
cmp r0,r10
orrne r8,r8,#1
str r9,[r4]
str r8,[r4,#4]
mov sp,r7
sub sp,sp,#16
add sp,sp,#1152
msr cpsr_f,r6
pop {r4-r10,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_mem_str_sp_4_r0
.type cpu_arm7_thumb_mem_str_sp_4_r0, %function
cpu_arm7_thumb_mem_str_sp_4_r0:
push {r4-r10,lr}
mov r4,r3
mrs r6,cpsr
mov r8,r2
mov r9,r0
mov r10,r1
sub sp,sp,#1152
mov r0,sp
mov r1,r10
mov r2,#288
9:
str r1,[r0],#4
subs r2,r2,#1
bne 9b
add sp,sp,#16
mov r7,sp
mov r0,r9
mov r1,r7
ldr r2,=4
adr r5,2f
adr r3,1f
orr r3,r3,#1
msr cpsr_f,r8
bx r3
.thumb
.balign 4
1:
.global cpu_arm7_thumb_mem_str_sp_4_r0_instruction
cpu_arm7_thumb_mem_str_sp_4_r0_instruction:
str r0,[sp,#4]
nop
bx r5
nop
.arm
.balign 4
2:
mrs r3,cpsr
and r8,r3,#0xf0000000
ldr r9,[r7,#4]
cmp r1,r7
orrne r8,r8,#2
ldr r3,=4
cmp r2,r3
orrne r8,r8,#4
cmp sp,r7
orrne r8,r8,#8
ldr r0,[r7,#0]
cmp r0,r10
orrne r8,r8,#1
ldr r0,[r7,#8]
cmp r0,r10
orrne r8,r8,#1
str r9,[r4]
str r8,[r4,#4]
mov sp,r7
sub sp,sp,#16
add sp,sp,#1152
msr cpsr_f,r6
pop {r4-r10,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_mem_str_sp_1020_r0
.type cpu_arm7_thumb_mem_str_sp_1020_r0, %function
cpu_arm7_thumb_mem_str_sp_1020_r0:
push {r4-r10,lr}
mov r4,r3
mrs r6,cpsr
mov r8,r2
mov r9,r0
mov r10,r1
sub sp,sp,#1152
mov r0,sp
mov r1,r10
mov r2,#288
9:
str r1,[r0],#4
subs r2,r2,#1
bne 9b
add sp,sp,#16
mov r7,sp
mov r0,r9
mov r1,r7
ldr r2,=1020
adr r5,2f
adr r3,1f
orr r3,r3,#1
msr cpsr_f,r8
bx r3
.thumb
.balign 4
1:
.global cpu_arm7_thumb_mem_str_sp_1020_r0_instruction
cpu_arm7_thumb_mem_str_sp_1020_r0_instruction:
str r0,[sp,#1020]
nop
bx r5
nop
.arm
.balign 4
2:
mrs r3,cpsr
and r8,r3,#0xf0000000
ldr r9,[r7,#1020]
cmp r1,r7
orrne r8,r8,#2
ldr r3,=1020
cmp r2,r3
orrne r8,r8,#4
cmp sp,r7
orrne r8,r8,#8
ldr r0,[r7,#1016]
cmp r0,r10
orrne r8,r8,#1
ldr r0,[r7,#1024]
cmp r0,r10
orrne r8,r8,#1
str r9,[r4]
str r8,[r4,#4]
mov sp,r7
sub sp,sp,#16
add sp,sp,#1152
msr cpsr_f,r6
pop {r4-r10,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_mem_ldr_register_0_r0
.type cpu_arm7_thumb_mem_ldr_register_0_r0, %function
cpu_arm7_thumb_mem_ldr_register_0_r0:
push {r4-r10,lr}
mov r4,r3
mrs r6,cpsr
mov r8,r2
mov r9,r0
mov r10,r1
sub sp,sp,#1152
mov r0,sp
mov r1,r10
mov r2,#288
9:
str r1,[r0],#4
subs r2,r2,#1
bne 9b
add sp,sp,#16
mov r7,sp
str r9,[r7,#0]
mvn r0,r9
mov r1,r7
ldr r2,=0
adr r5,2f
adr r3,1f
orr r3,r3,#1
msr cpsr_f,r8
bx r3
.thumb
.balign 4
1:
.global cpu_arm7_thumb_mem_ldr_register_0_r0_instruction
cpu_arm7_thumb_mem_ldr_register_0_r0_instruction:
ldr r0,[r1,r2]
nop
bx r5
nop
.arm
.balign 4
2:
mrs r3,cpsr
and r8,r3,#0xf0000000
mov r9,r0
cmp r1,r7
orrne r8,r8,#2
ldr r3,=0
cmp r2,r3
orrne r8,r8,#4
cmp sp,r7
orrne r8,r8,#8
ldr r0,[r7,#-4]
cmp r0,r10
orrne r8,r8,#1
ldr r0,[r7,#4]
cmp r0,r10
orrne r8,r8,#1
str r9,[r4]
str r8,[r4,#4]
mov sp,r7
sub sp,sp,#16
add sp,sp,#1152
msr cpsr_f,r6
pop {r4-r10,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_mem_ldr_register_0_r1
.type cpu_arm7_thumb_mem_ldr_register_0_r1, %function
cpu_arm7_thumb_mem_ldr_register_0_r1:
push {r4-r10,lr}
mov r4,r3
mrs r6,cpsr
mov r8,r2
mov r9,r0
mov r10,r1
sub sp,sp,#1152
mov r0,sp
mov r1,r10
mov r2,#288
9:
str r1,[r0],#4
subs r2,r2,#1
bne 9b
add sp,sp,#16
mov r7,sp
str r9,[r7,#0]
mov r0,r9
mov r1,r7
ldr r2,=0
adr r5,2f
adr r3,1f
orr r3,r3,#1
msr cpsr_f,r8
bx r3
.thumb
.balign 4
1:
.global cpu_arm7_thumb_mem_ldr_register_0_r1_instruction
cpu_arm7_thumb_mem_ldr_register_0_r1_instruction:
ldr r1,[r1,r2]
nop
bx r5
nop
.arm
.balign 4
2:
mrs r3,cpsr
and r8,r3,#0xf0000000
mov r9,r1
ldr r3,=0
cmp r2,r3
orrne r8,r8,#4
cmp sp,r7
orrne r8,r8,#8
ldr r0,[r7,#-4]
cmp r0,r10
orrne r8,r8,#1
ldr r0,[r7,#4]
cmp r0,r10
orrne r8,r8,#1
str r9,[r4]
str r8,[r4,#4]
mov sp,r7
sub sp,sp,#16
add sp,sp,#1152
msr cpsr_f,r6
pop {r4-r10,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_mem_ldr_register_0_r2
.type cpu_arm7_thumb_mem_ldr_register_0_r2, %function
cpu_arm7_thumb_mem_ldr_register_0_r2:
push {r4-r10,lr}
mov r4,r3
mrs r6,cpsr
mov r8,r2
mov r9,r0
mov r10,r1
sub sp,sp,#1152
mov r0,sp
mov r1,r10
mov r2,#288
9:
str r1,[r0],#4
subs r2,r2,#1
bne 9b
add sp,sp,#16
mov r7,sp
str r9,[r7,#0]
mov r0,r9
mov r1,r7
ldr r2,=0
adr r5,2f
adr r3,1f
orr r3,r3,#1
msr cpsr_f,r8
bx r3
.thumb
.balign 4
1:
.global cpu_arm7_thumb_mem_ldr_register_0_r2_instruction
cpu_arm7_thumb_mem_ldr_register_0_r2_instruction:
ldr r2,[r1,r2]
nop
bx r5
nop
.arm
.balign 4
2:
mrs r3,cpsr
and r8,r3,#0xf0000000
mov r9,r2
cmp r1,r7
orrne r8,r8,#2
cmp sp,r7
orrne r8,r8,#8
ldr r0,[r7,#-4]
cmp r0,r10
orrne r8,r8,#1
ldr r0,[r7,#4]
cmp r0,r10
orrne r8,r8,#1
str r9,[r4]
str r8,[r4,#4]
mov sp,r7
sub sp,sp,#16
add sp,sp,#1152
msr cpsr_f,r6
pop {r4-r10,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_mem_ldr_register_4_r0
.type cpu_arm7_thumb_mem_ldr_register_4_r0, %function
cpu_arm7_thumb_mem_ldr_register_4_r0:
push {r4-r10,lr}
mov r4,r3
mrs r6,cpsr
mov r8,r2
mov r9,r0
mov r10,r1
sub sp,sp,#1152
mov r0,sp
mov r1,r10
mov r2,#288
9:
str r1,[r0],#4
subs r2,r2,#1
bne 9b
add sp,sp,#16
mov r7,sp
str r9,[r7,#4]
mvn r0,r9
mov r1,r7
ldr r2,=4
adr r5,2f
adr r3,1f
orr r3,r3,#1
msr cpsr_f,r8
bx r3
.thumb
.balign 4
1:
.global cpu_arm7_thumb_mem_ldr_register_4_r0_instruction
cpu_arm7_thumb_mem_ldr_register_4_r0_instruction:
ldr r0,[r1,r2]
nop
bx r5
nop
.arm
.balign 4
2:
mrs r3,cpsr
and r8,r3,#0xf0000000
mov r9,r0
cmp r1,r7
orrne r8,r8,#2
ldr r3,=4
cmp r2,r3
orrne r8,r8,#4
cmp sp,r7
orrne r8,r8,#8
ldr r0,[r7,#0]
cmp r0,r10
orrne r8,r8,#1
ldr r0,[r7,#8]
cmp r0,r10
orrne r8,r8,#1
str r9,[r4]
str r8,[r4,#4]
mov sp,r7
sub sp,sp,#16
add sp,sp,#1152
msr cpsr_f,r6
pop {r4-r10,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_mem_ldr_register_4_r1
.type cpu_arm7_thumb_mem_ldr_register_4_r1, %function
cpu_arm7_thumb_mem_ldr_register_4_r1:
push {r4-r10,lr}
mov r4,r3
mrs r6,cpsr
mov r8,r2
mov r9,r0
mov r10,r1
sub sp,sp,#1152
mov r0,sp
mov r1,r10
mov r2,#288
9:
str r1,[r0],#4
subs r2,r2,#1
bne 9b
add sp,sp,#16
mov r7,sp
str r9,[r7,#4]
mov r0,r9
mov r1,r7
ldr r2,=4
adr r5,2f
adr r3,1f
orr r3,r3,#1
msr cpsr_f,r8
bx r3
.thumb
.balign 4
1:
.global cpu_arm7_thumb_mem_ldr_register_4_r1_instruction
cpu_arm7_thumb_mem_ldr_register_4_r1_instruction:
ldr r1,[r1,r2]
nop
bx r5
nop
.arm
.balign 4
2:
mrs r3,cpsr
and r8,r3,#0xf0000000
mov r9,r1
ldr r3,=4
cmp r2,r3
orrne r8,r8,#4
cmp sp,r7
orrne r8,r8,#8
ldr r0,[r7,#0]
cmp r0,r10
orrne r8,r8,#1
ldr r0,[r7,#8]
cmp r0,r10
orrne r8,r8,#1
str r9,[r4]
str r8,[r4,#4]
mov sp,r7
sub sp,sp,#16
add sp,sp,#1152
msr cpsr_f,r6
pop {r4-r10,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_mem_ldr_register_4_r2
.type cpu_arm7_thumb_mem_ldr_register_4_r2, %function
cpu_arm7_thumb_mem_ldr_register_4_r2:
push {r4-r10,lr}
mov r4,r3
mrs r6,cpsr
mov r8,r2
mov r9,r0
mov r10,r1
sub sp,sp,#1152
mov r0,sp
mov r1,r10
mov r2,#288
9:
str r1,[r0],#4
subs r2,r2,#1
bne 9b
add sp,sp,#16
mov r7,sp
str r9,[r7,#4]
mov r0,r9
mov r1,r7
ldr r2,=4
adr r5,2f
adr r3,1f
orr r3,r3,#1
msr cpsr_f,r8
bx r3
.thumb
.balign 4
1:
.global cpu_arm7_thumb_mem_ldr_register_4_r2_instruction
cpu_arm7_thumb_mem_ldr_register_4_r2_instruction:
ldr r2,[r1,r2]
nop
bx r5
nop
.arm
.balign 4
2:
mrs r3,cpsr
and r8,r3,#0xf0000000
mov r9,r2
cmp r1,r7
orrne r8,r8,#2
cmp sp,r7
orrne r8,r8,#8
ldr r0,[r7,#0]
cmp r0,r10
orrne r8,r8,#1
ldr r0,[r7,#8]
cmp r0,r10
orrne r8,r8,#1
str r9,[r4]
str r8,[r4,#4]
mov sp,r7
sub sp,sp,#16
add sp,sp,#1152
msr cpsr_f,r6
pop {r4-r10,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_mem_ldr_register_128_r0
.type cpu_arm7_thumb_mem_ldr_register_128_r0, %function
cpu_arm7_thumb_mem_ldr_register_128_r0:
push {r4-r10,lr}
mov r4,r3
mrs r6,cpsr
mov r8,r2
mov r9,r0
mov r10,r1
sub sp,sp,#1152
mov r0,sp
mov r1,r10
mov r2,#288
9:
str r1,[r0],#4
subs r2,r2,#1
bne 9b
add sp,sp,#16
mov r7,sp
str r9,[r7,#128]
mvn r0,r9
mov r1,r7
ldr r2,=128
adr r5,2f
adr r3,1f
orr r3,r3,#1
msr cpsr_f,r8
bx r3
.thumb
.balign 4
1:
.global cpu_arm7_thumb_mem_ldr_register_128_r0_instruction
cpu_arm7_thumb_mem_ldr_register_128_r0_instruction:
ldr r0,[r1,r2]
nop
bx r5
nop
.arm
.balign 4
2:
mrs r3,cpsr
and r8,r3,#0xf0000000
mov r9,r0
cmp r1,r7
orrne r8,r8,#2
ldr r3,=128
cmp r2,r3
orrne r8,r8,#4
cmp sp,r7
orrne r8,r8,#8
ldr r0,[r7,#124]
cmp r0,r10
orrne r8,r8,#1
ldr r0,[r7,#132]
cmp r0,r10
orrne r8,r8,#1
str r9,[r4]
str r8,[r4,#4]
mov sp,r7
sub sp,sp,#16
add sp,sp,#1152
msr cpsr_f,r6
pop {r4-r10,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_mem_ldr_register_128_r1
.type cpu_arm7_thumb_mem_ldr_register_128_r1, %function
cpu_arm7_thumb_mem_ldr_register_128_r1:
push {r4-r10,lr}
mov r4,r3
mrs r6,cpsr
mov r8,r2
mov r9,r0
mov r10,r1
sub sp,sp,#1152
mov r0,sp
mov r1,r10
mov r2,#288
9:
str r1,[r0],#4
subs r2,r2,#1
bne 9b
add sp,sp,#16
mov r7,sp
str r9,[r7,#128]
mov r0,r9
mov r1,r7
ldr r2,=128
adr r5,2f
adr r3,1f
orr r3,r3,#1
msr cpsr_f,r8
bx r3
.thumb
.balign 4
1:
.global cpu_arm7_thumb_mem_ldr_register_128_r1_instruction
cpu_arm7_thumb_mem_ldr_register_128_r1_instruction:
ldr r1,[r1,r2]
nop
bx r5
nop
.arm
.balign 4
2:
mrs r3,cpsr
and r8,r3,#0xf0000000
mov r9,r1
ldr r3,=128
cmp r2,r3
orrne r8,r8,#4
cmp sp,r7
orrne r8,r8,#8
ldr r0,[r7,#124]
cmp r0,r10
orrne r8,r8,#1
ldr r0,[r7,#132]
cmp r0,r10
orrne r8,r8,#1
str r9,[r4]
str r8,[r4,#4]
mov sp,r7
sub sp,sp,#16
add sp,sp,#1152
msr cpsr_f,r6
pop {r4-r10,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_mem_ldr_register_128_r2
.type cpu_arm7_thumb_mem_ldr_register_128_r2, %function
cpu_arm7_thumb_mem_ldr_register_128_r2:
push {r4-r10,lr}
mov r4,r3
mrs r6,cpsr
mov r8,r2
mov r9,r0
mov r10,r1
sub sp,sp,#1152
mov r0,sp
mov r1,r10
mov r2,#288
9:
str r1,[r0],#4
subs r2,r2,#1
bne 9b
add sp,sp,#16
mov r7,sp
str r9,[r7,#128]
mov r0,r9
mov r1,r7
ldr r2,=128
adr r5,2f
adr r3,1f
orr r3,r3,#1
msr cpsr_f,r8
bx r3
.thumb
.balign 4
1:
.global cpu_arm7_thumb_mem_ldr_register_128_r2_instruction
cpu_arm7_thumb_mem_ldr_register_128_r2_instruction:
ldr r2,[r1,r2]
nop
bx r5
nop
.arm
.balign 4
2:
mrs r3,cpsr
and r8,r3,#0xf0000000
mov r9,r2
cmp r1,r7
orrne r8,r8,#2
cmp sp,r7
orrne r8,r8,#8
ldr r0,[r7,#124]
cmp r0,r10
orrne r8,r8,#1
ldr r0,[r7,#132]
cmp r0,r10
orrne r8,r8,#1
str r9,[r4]
str r8,[r4,#4]
mov sp,r7
sub sp,sp,#16
add sp,sp,#1152
msr cpsr_f,r6
pop {r4-r10,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_mem_ldrb_register_0_r0
.type cpu_arm7_thumb_mem_ldrb_register_0_r0, %function
cpu_arm7_thumb_mem_ldrb_register_0_r0:
push {r4-r10,lr}
mov r4,r3
mrs r6,cpsr
mov r8,r2
mov r9,r0
mov r10,r1
sub sp,sp,#1152
mov r0,sp
mov r1,r10
mov r2,#288
9:
str r1,[r0],#4
subs r2,r2,#1
bne 9b
add sp,sp,#16
mov r7,sp
str r9,[r7,#0]
mvn r0,r9
mov r1,r7
ldr r2,=0
adr r5,2f
adr r3,1f
orr r3,r3,#1
msr cpsr_f,r8
bx r3
.thumb
.balign 4
1:
.global cpu_arm7_thumb_mem_ldrb_register_0_r0_instruction
cpu_arm7_thumb_mem_ldrb_register_0_r0_instruction:
ldrb r0,[r1,r2]
nop
bx r5
nop
.arm
.balign 4
2:
mrs r3,cpsr
and r8,r3,#0xf0000000
mov r9,r0
cmp r1,r7
orrne r8,r8,#2
ldr r3,=0
cmp r2,r3
orrne r8,r8,#4
cmp sp,r7
orrne r8,r8,#8
ldr r0,[r7,#-4]
cmp r0,r10
orrne r8,r8,#1
ldr r0,[r7,#4]
cmp r0,r10
orrne r8,r8,#1
str r9,[r4]
str r8,[r4,#4]
mov sp,r7
sub sp,sp,#16
add sp,sp,#1152
msr cpsr_f,r6
pop {r4-r10,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_mem_ldrb_register_0_r1
.type cpu_arm7_thumb_mem_ldrb_register_0_r1, %function
cpu_arm7_thumb_mem_ldrb_register_0_r1:
push {r4-r10,lr}
mov r4,r3
mrs r6,cpsr
mov r8,r2
mov r9,r0
mov r10,r1
sub sp,sp,#1152
mov r0,sp
mov r1,r10
mov r2,#288
9:
str r1,[r0],#4
subs r2,r2,#1
bne 9b
add sp,sp,#16
mov r7,sp
str r9,[r7,#0]
mov r0,r9
mov r1,r7
ldr r2,=0
adr r5,2f
adr r3,1f
orr r3,r3,#1
msr cpsr_f,r8
bx r3
.thumb
.balign 4
1:
.global cpu_arm7_thumb_mem_ldrb_register_0_r1_instruction
cpu_arm7_thumb_mem_ldrb_register_0_r1_instruction:
ldrb r1,[r1,r2]
nop
bx r5
nop
.arm
.balign 4
2:
mrs r3,cpsr
and r8,r3,#0xf0000000
mov r9,r1
ldr r3,=0
cmp r2,r3
orrne r8,r8,#4
cmp sp,r7
orrne r8,r8,#8
ldr r0,[r7,#-4]
cmp r0,r10
orrne r8,r8,#1
ldr r0,[r7,#4]
cmp r0,r10
orrne r8,r8,#1
str r9,[r4]
str r8,[r4,#4]
mov sp,r7
sub sp,sp,#16
add sp,sp,#1152
msr cpsr_f,r6
pop {r4-r10,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_mem_ldrb_register_0_r2
.type cpu_arm7_thumb_mem_ldrb_register_0_r2, %function
cpu_arm7_thumb_mem_ldrb_register_0_r2:
push {r4-r10,lr}
mov r4,r3
mrs r6,cpsr
mov r8,r2
mov r9,r0
mov r10,r1
sub sp,sp,#1152
mov r0,sp
mov r1,r10
mov r2,#288
9:
str r1,[r0],#4
subs r2,r2,#1
bne 9b
add sp,sp,#16
mov r7,sp
str r9,[r7,#0]
mov r0,r9
mov r1,r7
ldr r2,=0
adr r5,2f
adr r3,1f
orr r3,r3,#1
msr cpsr_f,r8
bx r3
.thumb
.balign 4
1:
.global cpu_arm7_thumb_mem_ldrb_register_0_r2_instruction
cpu_arm7_thumb_mem_ldrb_register_0_r2_instruction:
ldrb r2,[r1,r2]
nop
bx r5
nop
.arm
.balign 4
2:
mrs r3,cpsr
and r8,r3,#0xf0000000
mov r9,r2
cmp r1,r7
orrne r8,r8,#2
cmp sp,r7
orrne r8,r8,#8
ldr r0,[r7,#-4]
cmp r0,r10
orrne r8,r8,#1
ldr r0,[r7,#4]
cmp r0,r10
orrne r8,r8,#1
str r9,[r4]
str r8,[r4,#4]
mov sp,r7
sub sp,sp,#16
add sp,sp,#1152
msr cpsr_f,r6
pop {r4-r10,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_mem_ldrb_register_1_r0
.type cpu_arm7_thumb_mem_ldrb_register_1_r0, %function
cpu_arm7_thumb_mem_ldrb_register_1_r0:
push {r4-r10,lr}
mov r4,r3
mrs r6,cpsr
mov r8,r2
mov r9,r0
mov r10,r1
sub sp,sp,#1152
mov r0,sp
mov r1,r10
mov r2,#288
9:
str r1,[r0],#4
subs r2,r2,#1
bne 9b
add sp,sp,#16
mov r7,sp
str r9,[r7,#0]
mvn r0,r9
mov r1,r7
ldr r2,=1
adr r5,2f
adr r3,1f
orr r3,r3,#1
msr cpsr_f,r8
bx r3
.thumb
.balign 4
1:
.global cpu_arm7_thumb_mem_ldrb_register_1_r0_instruction
cpu_arm7_thumb_mem_ldrb_register_1_r0_instruction:
ldrb r0,[r1,r2]
nop
bx r5
nop
.arm
.balign 4
2:
mrs r3,cpsr
and r8,r3,#0xf0000000
mov r9,r0
cmp r1,r7
orrne r8,r8,#2
ldr r3,=1
cmp r2,r3
orrne r8,r8,#4
cmp sp,r7
orrne r8,r8,#8
ldr r0,[r7,#-4]
cmp r0,r10
orrne r8,r8,#1
ldr r0,[r7,#4]
cmp r0,r10
orrne r8,r8,#1
str r9,[r4]
str r8,[r4,#4]
mov sp,r7
sub sp,sp,#16
add sp,sp,#1152
msr cpsr_f,r6
pop {r4-r10,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_mem_ldrb_register_1_r1
.type cpu_arm7_thumb_mem_ldrb_register_1_r1, %function
cpu_arm7_thumb_mem_ldrb_register_1_r1:
push {r4-r10,lr}
mov r4,r3
mrs r6,cpsr
mov r8,r2
mov r9,r0
mov r10,r1
sub sp,sp,#1152
mov r0,sp
mov r1,r10
mov r2,#288
9:
str r1,[r0],#4
subs r2,r2,#1
bne 9b
add sp,sp,#16
mov r7,sp
str r9,[r7,#0]
mov r0,r9
mov r1,r7
ldr r2,=1
adr r5,2f
adr r3,1f
orr r3,r3,#1
msr cpsr_f,r8
bx r3
.thumb
.balign 4
1:
.global cpu_arm7_thumb_mem_ldrb_register_1_r1_instruction
cpu_arm7_thumb_mem_ldrb_register_1_r1_instruction:
ldrb r1,[r1,r2]
nop
bx r5
nop
.arm
.balign 4
2:
mrs r3,cpsr
and r8,r3,#0xf0000000
mov r9,r1
ldr r3,=1
cmp r2,r3
orrne r8,r8,#4
cmp sp,r7
orrne r8,r8,#8
ldr r0,[r7,#-4]
cmp r0,r10
orrne r8,r8,#1
ldr r0,[r7,#4]
cmp r0,r10
orrne r8,r8,#1
str r9,[r4]
str r8,[r4,#4]
mov sp,r7
sub sp,sp,#16
add sp,sp,#1152
msr cpsr_f,r6
pop {r4-r10,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_mem_ldrb_register_1_r2
.type cpu_arm7_thumb_mem_ldrb_register_1_r2, %function
cpu_arm7_thumb_mem_ldrb_register_1_r2:
push {r4-r10,lr}
mov r4,r3
mrs r6,cpsr
mov r8,r2
mov r9,r0
mov r10,r1
sub sp,sp,#1152
mov r0,sp
mov r1,r10
mov r2,#288
9:
str r1,[r0],#4
subs r2,r2,#1
bne 9b
add sp,sp,#16
mov r7,sp
str r9,[r7,#0]
mov r0,r9
mov r1,r7
ldr r2,=1
adr r5,2f
adr r3,1f
orr r3,r3,#1
msr cpsr_f,r8
bx r3
.thumb
.balign 4
1:
.global cpu_arm7_thumb_mem_ldrb_register_1_r2_instruction
cpu_arm7_thumb_mem_ldrb_register_1_r2_instruction:
ldrb r2,[r1,r2]
nop
bx r5
nop
.arm
.balign 4
2:
mrs r3,cpsr
and r8,r3,#0xf0000000
mov r9,r2
cmp r1,r7
orrne r8,r8,#2
cmp sp,r7
orrne r8,r8,#8
ldr r0,[r7,#-4]
cmp r0,r10
orrne r8,r8,#1
ldr r0,[r7,#4]
cmp r0,r10
orrne r8,r8,#1
str r9,[r4]
str r8,[r4,#4]
mov sp,r7
sub sp,sp,#16
add sp,sp,#1152
msr cpsr_f,r6
pop {r4-r10,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_mem_ldrb_register_3_r0
.type cpu_arm7_thumb_mem_ldrb_register_3_r0, %function
cpu_arm7_thumb_mem_ldrb_register_3_r0:
push {r4-r10,lr}
mov r4,r3
mrs r6,cpsr
mov r8,r2
mov r9,r0
mov r10,r1
sub sp,sp,#1152
mov r0,sp
mov r1,r10
mov r2,#288
9:
str r1,[r0],#4
subs r2,r2,#1
bne 9b
add sp,sp,#16
mov r7,sp
str r9,[r7,#0]
mvn r0,r9
mov r1,r7
ldr r2,=3
adr r5,2f
adr r3,1f
orr r3,r3,#1
msr cpsr_f,r8
bx r3
.thumb
.balign 4
1:
.global cpu_arm7_thumb_mem_ldrb_register_3_r0_instruction
cpu_arm7_thumb_mem_ldrb_register_3_r0_instruction:
ldrb r0,[r1,r2]
nop
bx r5
nop
.arm
.balign 4
2:
mrs r3,cpsr
and r8,r3,#0xf0000000
mov r9,r0
cmp r1,r7
orrne r8,r8,#2
ldr r3,=3
cmp r2,r3
orrne r8,r8,#4
cmp sp,r7
orrne r8,r8,#8
ldr r0,[r7,#-4]
cmp r0,r10
orrne r8,r8,#1
ldr r0,[r7,#4]
cmp r0,r10
orrne r8,r8,#1
str r9,[r4]
str r8,[r4,#4]
mov sp,r7
sub sp,sp,#16
add sp,sp,#1152
msr cpsr_f,r6
pop {r4-r10,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_mem_ldrb_register_3_r1
.type cpu_arm7_thumb_mem_ldrb_register_3_r1, %function
cpu_arm7_thumb_mem_ldrb_register_3_r1:
push {r4-r10,lr}
mov r4,r3
mrs r6,cpsr
mov r8,r2
mov r9,r0
mov r10,r1
sub sp,sp,#1152
mov r0,sp
mov r1,r10
mov r2,#288
9:
str r1,[r0],#4
subs r2,r2,#1
bne 9b
add sp,sp,#16
mov r7,sp
str r9,[r7,#0]
mov r0,r9
mov r1,r7
ldr r2,=3
adr r5,2f
adr r3,1f
orr r3,r3,#1
msr cpsr_f,r8
bx r3
.thumb
.balign 4
1:
.global cpu_arm7_thumb_mem_ldrb_register_3_r1_instruction
cpu_arm7_thumb_mem_ldrb_register_3_r1_instruction:
ldrb r1,[r1,r2]
nop
bx r5
nop
.arm
.balign 4
2:
mrs r3,cpsr
and r8,r3,#0xf0000000
mov r9,r1
ldr r3,=3
cmp r2,r3
orrne r8,r8,#4
cmp sp,r7
orrne r8,r8,#8
ldr r0,[r7,#-4]
cmp r0,r10
orrne r8,r8,#1
ldr r0,[r7,#4]
cmp r0,r10
orrne r8,r8,#1
str r9,[r4]
str r8,[r4,#4]
mov sp,r7
sub sp,sp,#16
add sp,sp,#1152
msr cpsr_f,r6
pop {r4-r10,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_mem_ldrb_register_3_r2
.type cpu_arm7_thumb_mem_ldrb_register_3_r2, %function
cpu_arm7_thumb_mem_ldrb_register_3_r2:
push {r4-r10,lr}
mov r4,r3
mrs r6,cpsr
mov r8,r2
mov r9,r0
mov r10,r1
sub sp,sp,#1152
mov r0,sp
mov r1,r10
mov r2,#288
9:
str r1,[r0],#4
subs r2,r2,#1
bne 9b
add sp,sp,#16
mov r7,sp
str r9,[r7,#0]
mov r0,r9
mov r1,r7
ldr r2,=3
adr r5,2f
adr r3,1f
orr r3,r3,#1
msr cpsr_f,r8
bx r3
.thumb
.balign 4
1:
.global cpu_arm7_thumb_mem_ldrb_register_3_r2_instruction
cpu_arm7_thumb_mem_ldrb_register_3_r2_instruction:
ldrb r2,[r1,r2]
nop
bx r5
nop
.arm
.balign 4
2:
mrs r3,cpsr
and r8,r3,#0xf0000000
mov r9,r2
cmp r1,r7
orrne r8,r8,#2
cmp sp,r7
orrne r8,r8,#8
ldr r0,[r7,#-4]
cmp r0,r10
orrne r8,r8,#1
ldr r0,[r7,#4]
cmp r0,r10
orrne r8,r8,#1
str r9,[r4]
str r8,[r4,#4]
mov sp,r7
sub sp,sp,#16
add sp,sp,#1152
msr cpsr_f,r6
pop {r4-r10,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_mem_ldrh_register_0_r0
.type cpu_arm7_thumb_mem_ldrh_register_0_r0, %function
cpu_arm7_thumb_mem_ldrh_register_0_r0:
push {r4-r10,lr}
mov r4,r3
mrs r6,cpsr
mov r8,r2
mov r9,r0
mov r10,r1
sub sp,sp,#1152
mov r0,sp
mov r1,r10
mov r2,#288
9:
str r1,[r0],#4
subs r2,r2,#1
bne 9b
add sp,sp,#16
mov r7,sp
str r9,[r7,#0]
mvn r0,r9
mov r1,r7
ldr r2,=0
adr r5,2f
adr r3,1f
orr r3,r3,#1
msr cpsr_f,r8
bx r3
.thumb
.balign 4
1:
.global cpu_arm7_thumb_mem_ldrh_register_0_r0_instruction
cpu_arm7_thumb_mem_ldrh_register_0_r0_instruction:
ldrh r0,[r1,r2]
nop
bx r5
nop
.arm
.balign 4
2:
mrs r3,cpsr
and r8,r3,#0xf0000000
mov r9,r0
cmp r1,r7
orrne r8,r8,#2
ldr r3,=0
cmp r2,r3
orrne r8,r8,#4
cmp sp,r7
orrne r8,r8,#8
ldr r0,[r7,#-4]
cmp r0,r10
orrne r8,r8,#1
ldr r0,[r7,#4]
cmp r0,r10
orrne r8,r8,#1
str r9,[r4]
str r8,[r4,#4]
mov sp,r7
sub sp,sp,#16
add sp,sp,#1152
msr cpsr_f,r6
pop {r4-r10,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_mem_ldrh_register_0_r1
.type cpu_arm7_thumb_mem_ldrh_register_0_r1, %function
cpu_arm7_thumb_mem_ldrh_register_0_r1:
push {r4-r10,lr}
mov r4,r3
mrs r6,cpsr
mov r8,r2
mov r9,r0
mov r10,r1
sub sp,sp,#1152
mov r0,sp
mov r1,r10
mov r2,#288
9:
str r1,[r0],#4
subs r2,r2,#1
bne 9b
add sp,sp,#16
mov r7,sp
str r9,[r7,#0]
mov r0,r9
mov r1,r7
ldr r2,=0
adr r5,2f
adr r3,1f
orr r3,r3,#1
msr cpsr_f,r8
bx r3
.thumb
.balign 4
1:
.global cpu_arm7_thumb_mem_ldrh_register_0_r1_instruction
cpu_arm7_thumb_mem_ldrh_register_0_r1_instruction:
ldrh r1,[r1,r2]
nop
bx r5
nop
.arm
.balign 4
2:
mrs r3,cpsr
and r8,r3,#0xf0000000
mov r9,r1
ldr r3,=0
cmp r2,r3
orrne r8,r8,#4
cmp sp,r7
orrne r8,r8,#8
ldr r0,[r7,#-4]
cmp r0,r10
orrne r8,r8,#1
ldr r0,[r7,#4]
cmp r0,r10
orrne r8,r8,#1
str r9,[r4]
str r8,[r4,#4]
mov sp,r7
sub sp,sp,#16
add sp,sp,#1152
msr cpsr_f,r6
pop {r4-r10,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_mem_ldrh_register_0_r2
.type cpu_arm7_thumb_mem_ldrh_register_0_r2, %function
cpu_arm7_thumb_mem_ldrh_register_0_r2:
push {r4-r10,lr}
mov r4,r3
mrs r6,cpsr
mov r8,r2
mov r9,r0
mov r10,r1
sub sp,sp,#1152
mov r0,sp
mov r1,r10
mov r2,#288
9:
str r1,[r0],#4
subs r2,r2,#1
bne 9b
add sp,sp,#16
mov r7,sp
str r9,[r7,#0]
mov r0,r9
mov r1,r7
ldr r2,=0
adr r5,2f
adr r3,1f
orr r3,r3,#1
msr cpsr_f,r8
bx r3
.thumb
.balign 4
1:
.global cpu_arm7_thumb_mem_ldrh_register_0_r2_instruction
cpu_arm7_thumb_mem_ldrh_register_0_r2_instruction:
ldrh r2,[r1,r2]
nop
bx r5
nop
.arm
.balign 4
2:
mrs r3,cpsr
and r8,r3,#0xf0000000
mov r9,r2
cmp r1,r7
orrne r8,r8,#2
cmp sp,r7
orrne r8,r8,#8
ldr r0,[r7,#-4]
cmp r0,r10
orrne r8,r8,#1
ldr r0,[r7,#4]
cmp r0,r10
orrne r8,r8,#1
str r9,[r4]
str r8,[r4,#4]
mov sp,r7
sub sp,sp,#16
add sp,sp,#1152
msr cpsr_f,r6
pop {r4-r10,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_mem_ldrh_register_2_r0
.type cpu_arm7_thumb_mem_ldrh_register_2_r0, %function
cpu_arm7_thumb_mem_ldrh_register_2_r0:
push {r4-r10,lr}
mov r4,r3
mrs r6,cpsr
mov r8,r2
mov r9,r0
mov r10,r1
sub sp,sp,#1152
mov r0,sp
mov r1,r10
mov r2,#288
9:
str r1,[r0],#4
subs r2,r2,#1
bne 9b
add sp,sp,#16
mov r7,sp
str r9,[r7,#0]
mvn r0,r9
mov r1,r7
ldr r2,=2
adr r5,2f
adr r3,1f
orr r3,r3,#1
msr cpsr_f,r8
bx r3
.thumb
.balign 4
1:
.global cpu_arm7_thumb_mem_ldrh_register_2_r0_instruction
cpu_arm7_thumb_mem_ldrh_register_2_r0_instruction:
ldrh r0,[r1,r2]
nop
bx r5
nop
.arm
.balign 4
2:
mrs r3,cpsr
and r8,r3,#0xf0000000
mov r9,r0
cmp r1,r7
orrne r8,r8,#2
ldr r3,=2
cmp r2,r3
orrne r8,r8,#4
cmp sp,r7
orrne r8,r8,#8
ldr r0,[r7,#-4]
cmp r0,r10
orrne r8,r8,#1
ldr r0,[r7,#4]
cmp r0,r10
orrne r8,r8,#1
str r9,[r4]
str r8,[r4,#4]
mov sp,r7
sub sp,sp,#16
add sp,sp,#1152
msr cpsr_f,r6
pop {r4-r10,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_mem_ldrh_register_2_r1
.type cpu_arm7_thumb_mem_ldrh_register_2_r1, %function
cpu_arm7_thumb_mem_ldrh_register_2_r1:
push {r4-r10,lr}
mov r4,r3
mrs r6,cpsr
mov r8,r2
mov r9,r0
mov r10,r1
sub sp,sp,#1152
mov r0,sp
mov r1,r10
mov r2,#288
9:
str r1,[r0],#4
subs r2,r2,#1
bne 9b
add sp,sp,#16
mov r7,sp
str r9,[r7,#0]
mov r0,r9
mov r1,r7
ldr r2,=2
adr r5,2f
adr r3,1f
orr r3,r3,#1
msr cpsr_f,r8
bx r3
.thumb
.balign 4
1:
.global cpu_arm7_thumb_mem_ldrh_register_2_r1_instruction
cpu_arm7_thumb_mem_ldrh_register_2_r1_instruction:
ldrh r1,[r1,r2]
nop
bx r5
nop
.arm
.balign 4
2:
mrs r3,cpsr
and r8,r3,#0xf0000000
mov r9,r1
ldr r3,=2
cmp r2,r3
orrne r8,r8,#4
cmp sp,r7
orrne r8,r8,#8
ldr r0,[r7,#-4]
cmp r0,r10
orrne r8,r8,#1
ldr r0,[r7,#4]
cmp r0,r10
orrne r8,r8,#1
str r9,[r4]
str r8,[r4,#4]
mov sp,r7
sub sp,sp,#16
add sp,sp,#1152
msr cpsr_f,r6
pop {r4-r10,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_mem_ldrh_register_2_r2
.type cpu_arm7_thumb_mem_ldrh_register_2_r2, %function
cpu_arm7_thumb_mem_ldrh_register_2_r2:
push {r4-r10,lr}
mov r4,r3
mrs r6,cpsr
mov r8,r2
mov r9,r0
mov r10,r1
sub sp,sp,#1152
mov r0,sp
mov r1,r10
mov r2,#288
9:
str r1,[r0],#4
subs r2,r2,#1
bne 9b
add sp,sp,#16
mov r7,sp
str r9,[r7,#0]
mov r0,r9
mov r1,r7
ldr r2,=2
adr r5,2f
adr r3,1f
orr r3,r3,#1
msr cpsr_f,r8
bx r3
.thumb
.balign 4
1:
.global cpu_arm7_thumb_mem_ldrh_register_2_r2_instruction
cpu_arm7_thumb_mem_ldrh_register_2_r2_instruction:
ldrh r2,[r1,r2]
nop
bx r5
nop
.arm
.balign 4
2:
mrs r3,cpsr
and r8,r3,#0xf0000000
mov r9,r2
cmp r1,r7
orrne r8,r8,#2
cmp sp,r7
orrne r8,r8,#8
ldr r0,[r7,#-4]
cmp r0,r10
orrne r8,r8,#1
ldr r0,[r7,#4]
cmp r0,r10
orrne r8,r8,#1
str r9,[r4]
str r8,[r4,#4]
mov sp,r7
sub sp,sp,#16
add sp,sp,#1152
msr cpsr_f,r6
pop {r4-r10,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_mem_ldrh_register_62_r0
.type cpu_arm7_thumb_mem_ldrh_register_62_r0, %function
cpu_arm7_thumb_mem_ldrh_register_62_r0:
push {r4-r10,lr}
mov r4,r3
mrs r6,cpsr
mov r8,r2
mov r9,r0
mov r10,r1
sub sp,sp,#1152
mov r0,sp
mov r1,r10
mov r2,#288
9:
str r1,[r0],#4
subs r2,r2,#1
bne 9b
add sp,sp,#16
mov r7,sp
str r9,[r7,#60]
mvn r0,r9
mov r1,r7
ldr r2,=62
adr r5,2f
adr r3,1f
orr r3,r3,#1
msr cpsr_f,r8
bx r3
.thumb
.balign 4
1:
.global cpu_arm7_thumb_mem_ldrh_register_62_r0_instruction
cpu_arm7_thumb_mem_ldrh_register_62_r0_instruction:
ldrh r0,[r1,r2]
nop
bx r5
nop
.arm
.balign 4
2:
mrs r3,cpsr
and r8,r3,#0xf0000000
mov r9,r0
cmp r1,r7
orrne r8,r8,#2
ldr r3,=62
cmp r2,r3
orrne r8,r8,#4
cmp sp,r7
orrne r8,r8,#8
ldr r0,[r7,#56]
cmp r0,r10
orrne r8,r8,#1
ldr r0,[r7,#64]
cmp r0,r10
orrne r8,r8,#1
str r9,[r4]
str r8,[r4,#4]
mov sp,r7
sub sp,sp,#16
add sp,sp,#1152
msr cpsr_f,r6
pop {r4-r10,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_mem_ldrh_register_62_r1
.type cpu_arm7_thumb_mem_ldrh_register_62_r1, %function
cpu_arm7_thumb_mem_ldrh_register_62_r1:
push {r4-r10,lr}
mov r4,r3
mrs r6,cpsr
mov r8,r2
mov r9,r0
mov r10,r1
sub sp,sp,#1152
mov r0,sp
mov r1,r10
mov r2,#288
9:
str r1,[r0],#4
subs r2,r2,#1
bne 9b
add sp,sp,#16
mov r7,sp
str r9,[r7,#60]
mov r0,r9
mov r1,r7
ldr r2,=62
adr r5,2f
adr r3,1f
orr r3,r3,#1
msr cpsr_f,r8
bx r3
.thumb
.balign 4
1:
.global cpu_arm7_thumb_mem_ldrh_register_62_r1_instruction
cpu_arm7_thumb_mem_ldrh_register_62_r1_instruction:
ldrh r1,[r1,r2]
nop
bx r5
nop
.arm
.balign 4
2:
mrs r3,cpsr
and r8,r3,#0xf0000000
mov r9,r1
ldr r3,=62
cmp r2,r3
orrne r8,r8,#4
cmp sp,r7
orrne r8,r8,#8
ldr r0,[r7,#56]
cmp r0,r10
orrne r8,r8,#1
ldr r0,[r7,#64]
cmp r0,r10
orrne r8,r8,#1
str r9,[r4]
str r8,[r4,#4]
mov sp,r7
sub sp,sp,#16
add sp,sp,#1152
msr cpsr_f,r6
pop {r4-r10,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_mem_ldrh_register_62_r2
.type cpu_arm7_thumb_mem_ldrh_register_62_r2, %function
cpu_arm7_thumb_mem_ldrh_register_62_r2:
push {r4-r10,lr}
mov r4,r3
mrs r6,cpsr
mov r8,r2
mov r9,r0
mov r10,r1
sub sp,sp,#1152
mov r0,sp
mov r1,r10
mov r2,#288
9:
str r1,[r0],#4
subs r2,r2,#1
bne 9b
add sp,sp,#16
mov r7,sp
str r9,[r7,#60]
mov r0,r9
mov r1,r7
ldr r2,=62
adr r5,2f
adr r3,1f
orr r3,r3,#1
msr cpsr_f,r8
bx r3
.thumb
.balign 4
1:
.global cpu_arm7_thumb_mem_ldrh_register_62_r2_instruction
cpu_arm7_thumb_mem_ldrh_register_62_r2_instruction:
ldrh r2,[r1,r2]
nop
bx r5
nop
.arm
.balign 4
2:
mrs r3,cpsr
and r8,r3,#0xf0000000
mov r9,r2
cmp r1,r7
orrne r8,r8,#2
cmp sp,r7
orrne r8,r8,#8
ldr r0,[r7,#56]
cmp r0,r10
orrne r8,r8,#1
ldr r0,[r7,#64]
cmp r0,r10
orrne r8,r8,#1
str r9,[r4]
str r8,[r4,#4]
mov sp,r7
sub sp,sp,#16
add sp,sp,#1152
msr cpsr_f,r6
pop {r4-r10,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_mem_ldrsb_register_0_r0
.type cpu_arm7_thumb_mem_ldrsb_register_0_r0, %function
cpu_arm7_thumb_mem_ldrsb_register_0_r0:
push {r4-r10,lr}
mov r4,r3
mrs r6,cpsr
mov r8,r2
mov r9,r0
mov r10,r1
sub sp,sp,#1152
mov r0,sp
mov r1,r10
mov r2,#288
9:
str r1,[r0],#4
subs r2,r2,#1
bne 9b
add sp,sp,#16
mov r7,sp
str r9,[r7,#0]
mvn r0,r9
mov r1,r7
ldr r2,=0
adr r5,2f
adr r3,1f
orr r3,r3,#1
msr cpsr_f,r8
bx r3
.thumb
.balign 4
1:
.global cpu_arm7_thumb_mem_ldrsb_register_0_r0_instruction
cpu_arm7_thumb_mem_ldrsb_register_0_r0_instruction:
ldrsb r0,[r1,r2]
nop
bx r5
nop
.arm
.balign 4
2:
mrs r3,cpsr
and r8,r3,#0xf0000000
mov r9,r0
cmp r1,r7
orrne r8,r8,#2
ldr r3,=0
cmp r2,r3
orrne r8,r8,#4
cmp sp,r7
orrne r8,r8,#8
ldr r0,[r7,#-4]
cmp r0,r10
orrne r8,r8,#1
ldr r0,[r7,#4]
cmp r0,r10
orrne r8,r8,#1
str r9,[r4]
str r8,[r4,#4]
mov sp,r7
sub sp,sp,#16
add sp,sp,#1152
msr cpsr_f,r6
pop {r4-r10,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_mem_ldrsb_register_0_r1
.type cpu_arm7_thumb_mem_ldrsb_register_0_r1, %function
cpu_arm7_thumb_mem_ldrsb_register_0_r1:
push {r4-r10,lr}
mov r4,r3
mrs r6,cpsr
mov r8,r2
mov r9,r0
mov r10,r1
sub sp,sp,#1152
mov r0,sp
mov r1,r10
mov r2,#288
9:
str r1,[r0],#4
subs r2,r2,#1
bne 9b
add sp,sp,#16
mov r7,sp
str r9,[r7,#0]
mov r0,r9
mov r1,r7
ldr r2,=0
adr r5,2f
adr r3,1f
orr r3,r3,#1
msr cpsr_f,r8
bx r3
.thumb
.balign 4
1:
.global cpu_arm7_thumb_mem_ldrsb_register_0_r1_instruction
cpu_arm7_thumb_mem_ldrsb_register_0_r1_instruction:
ldrsb r1,[r1,r2]
nop
bx r5
nop
.arm
.balign 4
2:
mrs r3,cpsr
and r8,r3,#0xf0000000
mov r9,r1
ldr r3,=0
cmp r2,r3
orrne r8,r8,#4
cmp sp,r7
orrne r8,r8,#8
ldr r0,[r7,#-4]
cmp r0,r10
orrne r8,r8,#1
ldr r0,[r7,#4]
cmp r0,r10
orrne r8,r8,#1
str r9,[r4]
str r8,[r4,#4]
mov sp,r7
sub sp,sp,#16
add sp,sp,#1152
msr cpsr_f,r6
pop {r4-r10,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_mem_ldrsb_register_0_r2
.type cpu_arm7_thumb_mem_ldrsb_register_0_r2, %function
cpu_arm7_thumb_mem_ldrsb_register_0_r2:
push {r4-r10,lr}
mov r4,r3
mrs r6,cpsr
mov r8,r2
mov r9,r0
mov r10,r1
sub sp,sp,#1152
mov r0,sp
mov r1,r10
mov r2,#288
9:
str r1,[r0],#4
subs r2,r2,#1
bne 9b
add sp,sp,#16
mov r7,sp
str r9,[r7,#0]
mov r0,r9
mov r1,r7
ldr r2,=0
adr r5,2f
adr r3,1f
orr r3,r3,#1
msr cpsr_f,r8
bx r3
.thumb
.balign 4
1:
.global cpu_arm7_thumb_mem_ldrsb_register_0_r2_instruction
cpu_arm7_thumb_mem_ldrsb_register_0_r2_instruction:
ldrsb r2,[r1,r2]
nop
bx r5
nop
.arm
.balign 4
2:
mrs r3,cpsr
and r8,r3,#0xf0000000
mov r9,r2
cmp r1,r7
orrne r8,r8,#2
cmp sp,r7
orrne r8,r8,#8
ldr r0,[r7,#-4]
cmp r0,r10
orrne r8,r8,#1
ldr r0,[r7,#4]
cmp r0,r10
orrne r8,r8,#1
str r9,[r4]
str r8,[r4,#4]
mov sp,r7
sub sp,sp,#16
add sp,sp,#1152
msr cpsr_f,r6
pop {r4-r10,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_mem_ldrsb_register_1_r0
.type cpu_arm7_thumb_mem_ldrsb_register_1_r0, %function
cpu_arm7_thumb_mem_ldrsb_register_1_r0:
push {r4-r10,lr}
mov r4,r3
mrs r6,cpsr
mov r8,r2
mov r9,r0
mov r10,r1
sub sp,sp,#1152
mov r0,sp
mov r1,r10
mov r2,#288
9:
str r1,[r0],#4
subs r2,r2,#1
bne 9b
add sp,sp,#16
mov r7,sp
str r9,[r7,#0]
mvn r0,r9
mov r1,r7
ldr r2,=1
adr r5,2f
adr r3,1f
orr r3,r3,#1
msr cpsr_f,r8
bx r3
.thumb
.balign 4
1:
.global cpu_arm7_thumb_mem_ldrsb_register_1_r0_instruction
cpu_arm7_thumb_mem_ldrsb_register_1_r0_instruction:
ldrsb r0,[r1,r2]
nop
bx r5
nop
.arm
.balign 4
2:
mrs r3,cpsr
and r8,r3,#0xf0000000
mov r9,r0
cmp r1,r7
orrne r8,r8,#2
ldr r3,=1
cmp r2,r3
orrne r8,r8,#4
cmp sp,r7
orrne r8,r8,#8
ldr r0,[r7,#-4]
cmp r0,r10
orrne r8,r8,#1
ldr r0,[r7,#4]
cmp r0,r10
orrne r8,r8,#1
str r9,[r4]
str r8,[r4,#4]
mov sp,r7
sub sp,sp,#16
add sp,sp,#1152
msr cpsr_f,r6
pop {r4-r10,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_mem_ldrsb_register_1_r1
.type cpu_arm7_thumb_mem_ldrsb_register_1_r1, %function
cpu_arm7_thumb_mem_ldrsb_register_1_r1:
push {r4-r10,lr}
mov r4,r3
mrs r6,cpsr
mov r8,r2
mov r9,r0
mov r10,r1
sub sp,sp,#1152
mov r0,sp
mov r1,r10
mov r2,#288
9:
str r1,[r0],#4
subs r2,r2,#1
bne 9b
add sp,sp,#16
mov r7,sp
str r9,[r7,#0]
mov r0,r9
mov r1,r7
ldr r2,=1
adr r5,2f
adr r3,1f
orr r3,r3,#1
msr cpsr_f,r8
bx r3
.thumb
.balign 4
1:
.global cpu_arm7_thumb_mem_ldrsb_register_1_r1_instruction
cpu_arm7_thumb_mem_ldrsb_register_1_r1_instruction:
ldrsb r1,[r1,r2]
nop
bx r5
nop
.arm
.balign 4
2:
mrs r3,cpsr
and r8,r3,#0xf0000000
mov r9,r1
ldr r3,=1
cmp r2,r3
orrne r8,r8,#4
cmp sp,r7
orrne r8,r8,#8
ldr r0,[r7,#-4]
cmp r0,r10
orrne r8,r8,#1
ldr r0,[r7,#4]
cmp r0,r10
orrne r8,r8,#1
str r9,[r4]
str r8,[r4,#4]
mov sp,r7
sub sp,sp,#16
add sp,sp,#1152
msr cpsr_f,r6
pop {r4-r10,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_mem_ldrsb_register_1_r2
.type cpu_arm7_thumb_mem_ldrsb_register_1_r2, %function
cpu_arm7_thumb_mem_ldrsb_register_1_r2:
push {r4-r10,lr}
mov r4,r3
mrs r6,cpsr
mov r8,r2
mov r9,r0
mov r10,r1
sub sp,sp,#1152
mov r0,sp
mov r1,r10
mov r2,#288
9:
str r1,[r0],#4
subs r2,r2,#1
bne 9b
add sp,sp,#16
mov r7,sp
str r9,[r7,#0]
mov r0,r9
mov r1,r7
ldr r2,=1
adr r5,2f
adr r3,1f
orr r3,r3,#1
msr cpsr_f,r8
bx r3
.thumb
.balign 4
1:
.global cpu_arm7_thumb_mem_ldrsb_register_1_r2_instruction
cpu_arm7_thumb_mem_ldrsb_register_1_r2_instruction:
ldrsb r2,[r1,r2]
nop
bx r5
nop
.arm
.balign 4
2:
mrs r3,cpsr
and r8,r3,#0xf0000000
mov r9,r2
cmp r1,r7
orrne r8,r8,#2
cmp sp,r7
orrne r8,r8,#8
ldr r0,[r7,#-4]
cmp r0,r10
orrne r8,r8,#1
ldr r0,[r7,#4]
cmp r0,r10
orrne r8,r8,#1
str r9,[r4]
str r8,[r4,#4]
mov sp,r7
sub sp,sp,#16
add sp,sp,#1152
msr cpsr_f,r6
pop {r4-r10,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_mem_ldrsb_register_3_r0
.type cpu_arm7_thumb_mem_ldrsb_register_3_r0, %function
cpu_arm7_thumb_mem_ldrsb_register_3_r0:
push {r4-r10,lr}
mov r4,r3
mrs r6,cpsr
mov r8,r2
mov r9,r0
mov r10,r1
sub sp,sp,#1152
mov r0,sp
mov r1,r10
mov r2,#288
9:
str r1,[r0],#4
subs r2,r2,#1
bne 9b
add sp,sp,#16
mov r7,sp
str r9,[r7,#0]
mvn r0,r9
mov r1,r7
ldr r2,=3
adr r5,2f
adr r3,1f
orr r3,r3,#1
msr cpsr_f,r8
bx r3
.thumb
.balign 4
1:
.global cpu_arm7_thumb_mem_ldrsb_register_3_r0_instruction
cpu_arm7_thumb_mem_ldrsb_register_3_r0_instruction:
ldrsb r0,[r1,r2]
nop
bx r5
nop
.arm
.balign 4
2:
mrs r3,cpsr
and r8,r3,#0xf0000000
mov r9,r0
cmp r1,r7
orrne r8,r8,#2
ldr r3,=3
cmp r2,r3
orrne r8,r8,#4
cmp sp,r7
orrne r8,r8,#8
ldr r0,[r7,#-4]
cmp r0,r10
orrne r8,r8,#1
ldr r0,[r7,#4]
cmp r0,r10
orrne r8,r8,#1
str r9,[r4]
str r8,[r4,#4]
mov sp,r7
sub sp,sp,#16
add sp,sp,#1152
msr cpsr_f,r6
pop {r4-r10,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_mem_ldrsb_register_3_r1
.type cpu_arm7_thumb_mem_ldrsb_register_3_r1, %function
cpu_arm7_thumb_mem_ldrsb_register_3_r1:
push {r4-r10,lr}
mov r4,r3
mrs r6,cpsr
mov r8,r2
mov r9,r0
mov r10,r1
sub sp,sp,#1152
mov r0,sp
mov r1,r10
mov r2,#288
9:
str r1,[r0],#4
subs r2,r2,#1
bne 9b
add sp,sp,#16
mov r7,sp
str r9,[r7,#0]
mov r0,r9
mov r1,r7
ldr r2,=3
adr r5,2f
adr r3,1f
orr r3,r3,#1
msr cpsr_f,r8
bx r3
.thumb
.balign 4
1:
.global cpu_arm7_thumb_mem_ldrsb_register_3_r1_instruction
cpu_arm7_thumb_mem_ldrsb_register_3_r1_instruction:
ldrsb r1,[r1,r2]
nop
bx r5
nop
.arm
.balign 4
2:
mrs r3,cpsr
and r8,r3,#0xf0000000
mov r9,r1
ldr r3,=3
cmp r2,r3
orrne r8,r8,#4
cmp sp,r7
orrne r8,r8,#8
ldr r0,[r7,#-4]
cmp r0,r10
orrne r8,r8,#1
ldr r0,[r7,#4]
cmp r0,r10
orrne r8,r8,#1
str r9,[r4]
str r8,[r4,#4]
mov sp,r7
sub sp,sp,#16
add sp,sp,#1152
msr cpsr_f,r6
pop {r4-r10,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_mem_ldrsb_register_3_r2
.type cpu_arm7_thumb_mem_ldrsb_register_3_r2, %function
cpu_arm7_thumb_mem_ldrsb_register_3_r2:
push {r4-r10,lr}
mov r4,r3
mrs r6,cpsr
mov r8,r2
mov r9,r0
mov r10,r1
sub sp,sp,#1152
mov r0,sp
mov r1,r10
mov r2,#288
9:
str r1,[r0],#4
subs r2,r2,#1
bne 9b
add sp,sp,#16
mov r7,sp
str r9,[r7,#0]
mov r0,r9
mov r1,r7
ldr r2,=3
adr r5,2f
adr r3,1f
orr r3,r3,#1
msr cpsr_f,r8
bx r3
.thumb
.balign 4
1:
.global cpu_arm7_thumb_mem_ldrsb_register_3_r2_instruction
cpu_arm7_thumb_mem_ldrsb_register_3_r2_instruction:
ldrsb r2,[r1,r2]
nop
bx r5
nop
.arm
.balign 4
2:
mrs r3,cpsr
and r8,r3,#0xf0000000
mov r9,r2
cmp r1,r7
orrne r8,r8,#2
cmp sp,r7
orrne r8,r8,#8
ldr r0,[r7,#-4]
cmp r0,r10
orrne r8,r8,#1
ldr r0,[r7,#4]
cmp r0,r10
orrne r8,r8,#1
str r9,[r4]
str r8,[r4,#4]
mov sp,r7
sub sp,sp,#16
add sp,sp,#1152
msr cpsr_f,r6
pop {r4-r10,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_mem_ldrsh_register_0_r0
.type cpu_arm7_thumb_mem_ldrsh_register_0_r0, %function
cpu_arm7_thumb_mem_ldrsh_register_0_r0:
push {r4-r10,lr}
mov r4,r3
mrs r6,cpsr
mov r8,r2
mov r9,r0
mov r10,r1
sub sp,sp,#1152
mov r0,sp
mov r1,r10
mov r2,#288
9:
str r1,[r0],#4
subs r2,r2,#1
bne 9b
add sp,sp,#16
mov r7,sp
str r9,[r7,#0]
mvn r0,r9
mov r1,r7
ldr r2,=0
adr r5,2f
adr r3,1f
orr r3,r3,#1
msr cpsr_f,r8
bx r3
.thumb
.balign 4
1:
.global cpu_arm7_thumb_mem_ldrsh_register_0_r0_instruction
cpu_arm7_thumb_mem_ldrsh_register_0_r0_instruction:
ldrsh r0,[r1,r2]
nop
bx r5
nop
.arm
.balign 4
2:
mrs r3,cpsr
and r8,r3,#0xf0000000
mov r9,r0
cmp r1,r7
orrne r8,r8,#2
ldr r3,=0
cmp r2,r3
orrne r8,r8,#4
cmp sp,r7
orrne r8,r8,#8
ldr r0,[r7,#-4]
cmp r0,r10
orrne r8,r8,#1
ldr r0,[r7,#4]
cmp r0,r10
orrne r8,r8,#1
str r9,[r4]
str r8,[r4,#4]
mov sp,r7
sub sp,sp,#16
add sp,sp,#1152
msr cpsr_f,r6
pop {r4-r10,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_mem_ldrsh_register_0_r1
.type cpu_arm7_thumb_mem_ldrsh_register_0_r1, %function
cpu_arm7_thumb_mem_ldrsh_register_0_r1:
push {r4-r10,lr}
mov r4,r3
mrs r6,cpsr
mov r8,r2
mov r9,r0
mov r10,r1
sub sp,sp,#1152
mov r0,sp
mov r1,r10
mov r2,#288
9:
str r1,[r0],#4
subs r2,r2,#1
bne 9b
add sp,sp,#16
mov r7,sp
str r9,[r7,#0]
mov r0,r9
mov r1,r7
ldr r2,=0
adr r5,2f
adr r3,1f
orr r3,r3,#1
msr cpsr_f,r8
bx r3
.thumb
.balign 4
1:
.global cpu_arm7_thumb_mem_ldrsh_register_0_r1_instruction
cpu_arm7_thumb_mem_ldrsh_register_0_r1_instruction:
ldrsh r1,[r1,r2]
nop
bx r5
nop
.arm
.balign 4
2:
mrs r3,cpsr
and r8,r3,#0xf0000000
mov r9,r1
ldr r3,=0
cmp r2,r3
orrne r8,r8,#4
cmp sp,r7
orrne r8,r8,#8
ldr r0,[r7,#-4]
cmp r0,r10
orrne r8,r8,#1
ldr r0,[r7,#4]
cmp r0,r10
orrne r8,r8,#1
str r9,[r4]
str r8,[r4,#4]
mov sp,r7
sub sp,sp,#16
add sp,sp,#1152
msr cpsr_f,r6
pop {r4-r10,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_mem_ldrsh_register_0_r2
.type cpu_arm7_thumb_mem_ldrsh_register_0_r2, %function
cpu_arm7_thumb_mem_ldrsh_register_0_r2:
push {r4-r10,lr}
mov r4,r3
mrs r6,cpsr
mov r8,r2
mov r9,r0
mov r10,r1
sub sp,sp,#1152
mov r0,sp
mov r1,r10
mov r2,#288
9:
str r1,[r0],#4
subs r2,r2,#1
bne 9b
add sp,sp,#16
mov r7,sp
str r9,[r7,#0]
mov r0,r9
mov r1,r7
ldr r2,=0
adr r5,2f
adr r3,1f
orr r3,r3,#1
msr cpsr_f,r8
bx r3
.thumb
.balign 4
1:
.global cpu_arm7_thumb_mem_ldrsh_register_0_r2_instruction
cpu_arm7_thumb_mem_ldrsh_register_0_r2_instruction:
ldrsh r2,[r1,r2]
nop
bx r5
nop
.arm
.balign 4
2:
mrs r3,cpsr
and r8,r3,#0xf0000000
mov r9,r2
cmp r1,r7
orrne r8,r8,#2
cmp sp,r7
orrne r8,r8,#8
ldr r0,[r7,#-4]
cmp r0,r10
orrne r8,r8,#1
ldr r0,[r7,#4]
cmp r0,r10
orrne r8,r8,#1
str r9,[r4]
str r8,[r4,#4]
mov sp,r7
sub sp,sp,#16
add sp,sp,#1152
msr cpsr_f,r6
pop {r4-r10,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_mem_ldrsh_register_2_r0
.type cpu_arm7_thumb_mem_ldrsh_register_2_r0, %function
cpu_arm7_thumb_mem_ldrsh_register_2_r0:
push {r4-r10,lr}
mov r4,r3
mrs r6,cpsr
mov r8,r2
mov r9,r0
mov r10,r1
sub sp,sp,#1152
mov r0,sp
mov r1,r10
mov r2,#288
9:
str r1,[r0],#4
subs r2,r2,#1
bne 9b
add sp,sp,#16
mov r7,sp
str r9,[r7,#0]
mvn r0,r9
mov r1,r7
ldr r2,=2
adr r5,2f
adr r3,1f
orr r3,r3,#1
msr cpsr_f,r8
bx r3
.thumb
.balign 4
1:
.global cpu_arm7_thumb_mem_ldrsh_register_2_r0_instruction
cpu_arm7_thumb_mem_ldrsh_register_2_r0_instruction:
ldrsh r0,[r1,r2]
nop
bx r5
nop
.arm
.balign 4
2:
mrs r3,cpsr
and r8,r3,#0xf0000000
mov r9,r0
cmp r1,r7
orrne r8,r8,#2
ldr r3,=2
cmp r2,r3
orrne r8,r8,#4
cmp sp,r7
orrne r8,r8,#8
ldr r0,[r7,#-4]
cmp r0,r10
orrne r8,r8,#1
ldr r0,[r7,#4]
cmp r0,r10
orrne r8,r8,#1
str r9,[r4]
str r8,[r4,#4]
mov sp,r7
sub sp,sp,#16
add sp,sp,#1152
msr cpsr_f,r6
pop {r4-r10,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_mem_ldrsh_register_2_r1
.type cpu_arm7_thumb_mem_ldrsh_register_2_r1, %function
cpu_arm7_thumb_mem_ldrsh_register_2_r1:
push {r4-r10,lr}
mov r4,r3
mrs r6,cpsr
mov r8,r2
mov r9,r0
mov r10,r1
sub sp,sp,#1152
mov r0,sp
mov r1,r10
mov r2,#288
9:
str r1,[r0],#4
subs r2,r2,#1
bne 9b
add sp,sp,#16
mov r7,sp
str r9,[r7,#0]
mov r0,r9
mov r1,r7
ldr r2,=2
adr r5,2f
adr r3,1f
orr r3,r3,#1
msr cpsr_f,r8
bx r3
.thumb
.balign 4
1:
.global cpu_arm7_thumb_mem_ldrsh_register_2_r1_instruction
cpu_arm7_thumb_mem_ldrsh_register_2_r1_instruction:
ldrsh r1,[r1,r2]
nop
bx r5
nop
.arm
.balign 4
2:
mrs r3,cpsr
and r8,r3,#0xf0000000
mov r9,r1
ldr r3,=2
cmp r2,r3
orrne r8,r8,#4
cmp sp,r7
orrne r8,r8,#8
ldr r0,[r7,#-4]
cmp r0,r10
orrne r8,r8,#1
ldr r0,[r7,#4]
cmp r0,r10
orrne r8,r8,#1
str r9,[r4]
str r8,[r4,#4]
mov sp,r7
sub sp,sp,#16
add sp,sp,#1152
msr cpsr_f,r6
pop {r4-r10,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_mem_ldrsh_register_2_r2
.type cpu_arm7_thumb_mem_ldrsh_register_2_r2, %function
cpu_arm7_thumb_mem_ldrsh_register_2_r2:
push {r4-r10,lr}
mov r4,r3
mrs r6,cpsr
mov r8,r2
mov r9,r0
mov r10,r1
sub sp,sp,#1152
mov r0,sp
mov r1,r10
mov r2,#288
9:
str r1,[r0],#4
subs r2,r2,#1
bne 9b
add sp,sp,#16
mov r7,sp
str r9,[r7,#0]
mov r0,r9
mov r1,r7
ldr r2,=2
adr r5,2f
adr r3,1f
orr r3,r3,#1
msr cpsr_f,r8
bx r3
.thumb
.balign 4
1:
.global cpu_arm7_thumb_mem_ldrsh_register_2_r2_instruction
cpu_arm7_thumb_mem_ldrsh_register_2_r2_instruction:
ldrsh r2,[r1,r2]
nop
bx r5
nop
.arm
.balign 4
2:
mrs r3,cpsr
and r8,r3,#0xf0000000
mov r9,r2
cmp r1,r7
orrne r8,r8,#2
cmp sp,r7
orrne r8,r8,#8
ldr r0,[r7,#-4]
cmp r0,r10
orrne r8,r8,#1
ldr r0,[r7,#4]
cmp r0,r10
orrne r8,r8,#1
str r9,[r4]
str r8,[r4,#4]
mov sp,r7
sub sp,sp,#16
add sp,sp,#1152
msr cpsr_f,r6
pop {r4-r10,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_mem_ldrsh_register_62_r0
.type cpu_arm7_thumb_mem_ldrsh_register_62_r0, %function
cpu_arm7_thumb_mem_ldrsh_register_62_r0:
push {r4-r10,lr}
mov r4,r3
mrs r6,cpsr
mov r8,r2
mov r9,r0
mov r10,r1
sub sp,sp,#1152
mov r0,sp
mov r1,r10
mov r2,#288
9:
str r1,[r0],#4
subs r2,r2,#1
bne 9b
add sp,sp,#16
mov r7,sp
str r9,[r7,#60]
mvn r0,r9
mov r1,r7
ldr r2,=62
adr r5,2f
adr r3,1f
orr r3,r3,#1
msr cpsr_f,r8
bx r3
.thumb
.balign 4
1:
.global cpu_arm7_thumb_mem_ldrsh_register_62_r0_instruction
cpu_arm7_thumb_mem_ldrsh_register_62_r0_instruction:
ldrsh r0,[r1,r2]
nop
bx r5
nop
.arm
.balign 4
2:
mrs r3,cpsr
and r8,r3,#0xf0000000
mov r9,r0
cmp r1,r7
orrne r8,r8,#2
ldr r3,=62
cmp r2,r3
orrne r8,r8,#4
cmp sp,r7
orrne r8,r8,#8
ldr r0,[r7,#56]
cmp r0,r10
orrne r8,r8,#1
ldr r0,[r7,#64]
cmp r0,r10
orrne r8,r8,#1
str r9,[r4]
str r8,[r4,#4]
mov sp,r7
sub sp,sp,#16
add sp,sp,#1152
msr cpsr_f,r6
pop {r4-r10,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_mem_ldrsh_register_62_r1
.type cpu_arm7_thumb_mem_ldrsh_register_62_r1, %function
cpu_arm7_thumb_mem_ldrsh_register_62_r1:
push {r4-r10,lr}
mov r4,r3
mrs r6,cpsr
mov r8,r2
mov r9,r0
mov r10,r1
sub sp,sp,#1152
mov r0,sp
mov r1,r10
mov r2,#288
9:
str r1,[r0],#4
subs r2,r2,#1
bne 9b
add sp,sp,#16
mov r7,sp
str r9,[r7,#60]
mov r0,r9
mov r1,r7
ldr r2,=62
adr r5,2f
adr r3,1f
orr r3,r3,#1
msr cpsr_f,r8
bx r3
.thumb
.balign 4
1:
.global cpu_arm7_thumb_mem_ldrsh_register_62_r1_instruction
cpu_arm7_thumb_mem_ldrsh_register_62_r1_instruction:
ldrsh r1,[r1,r2]
nop
bx r5
nop
.arm
.balign 4
2:
mrs r3,cpsr
and r8,r3,#0xf0000000
mov r9,r1
ldr r3,=62
cmp r2,r3
orrne r8,r8,#4
cmp sp,r7
orrne r8,r8,#8
ldr r0,[r7,#56]
cmp r0,r10
orrne r8,r8,#1
ldr r0,[r7,#64]
cmp r0,r10
orrne r8,r8,#1
str r9,[r4]
str r8,[r4,#4]
mov sp,r7
sub sp,sp,#16
add sp,sp,#1152
msr cpsr_f,r6
pop {r4-r10,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_mem_ldrsh_register_62_r2
.type cpu_arm7_thumb_mem_ldrsh_register_62_r2, %function
cpu_arm7_thumb_mem_ldrsh_register_62_r2:
push {r4-r10,lr}
mov r4,r3
mrs r6,cpsr
mov r8,r2
mov r9,r0
mov r10,r1
sub sp,sp,#1152
mov r0,sp
mov r1,r10
mov r2,#288
9:
str r1,[r0],#4
subs r2,r2,#1
bne 9b
add sp,sp,#16
mov r7,sp
str r9,[r7,#60]
mov r0,r9
mov r1,r7
ldr r2,=62
adr r5,2f
adr r3,1f
orr r3,r3,#1
msr cpsr_f,r8
bx r3
.thumb
.balign 4
1:
.global cpu_arm7_thumb_mem_ldrsh_register_62_r2_instruction
cpu_arm7_thumb_mem_ldrsh_register_62_r2_instruction:
ldrsh r2,[r1,r2]
nop
bx r5
nop
.arm
.balign 4
2:
mrs r3,cpsr
and r8,r3,#0xf0000000
mov r9,r2
cmp r1,r7
orrne r8,r8,#2
cmp sp,r7
orrne r8,r8,#8
ldr r0,[r7,#56]
cmp r0,r10
orrne r8,r8,#1
ldr r0,[r7,#64]
cmp r0,r10
orrne r8,r8,#1
str r9,[r4]
str r8,[r4,#4]
mov sp,r7
sub sp,sp,#16
add sp,sp,#1152
msr cpsr_f,r6
pop {r4-r10,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_mem_str_register_0_r0
.type cpu_arm7_thumb_mem_str_register_0_r0, %function
cpu_arm7_thumb_mem_str_register_0_r0:
push {r4-r10,lr}
mov r4,r3
mrs r6,cpsr
mov r8,r2
mov r9,r0
mov r10,r1
sub sp,sp,#1152
mov r0,sp
mov r1,r10
mov r2,#288
9:
str r1,[r0],#4
subs r2,r2,#1
bne 9b
add sp,sp,#16
mov r7,sp
mov r0,r9
mov r1,r7
ldr r2,=0
adr r5,2f
adr r3,1f
orr r3,r3,#1
msr cpsr_f,r8
bx r3
.thumb
.balign 4
1:
.global cpu_arm7_thumb_mem_str_register_0_r0_instruction
cpu_arm7_thumb_mem_str_register_0_r0_instruction:
str r0,[r1,r2]
nop
bx r5
nop
.arm
.balign 4
2:
mrs r3,cpsr
and r8,r3,#0xf0000000
ldr r9,[r7,#0]
cmp r1,r7
orrne r8,r8,#2
ldr r3,=0
cmp r2,r3
orrne r8,r8,#4
cmp sp,r7
orrne r8,r8,#8
ldr r0,[r7,#-4]
cmp r0,r10
orrne r8,r8,#1
ldr r0,[r7,#4]
cmp r0,r10
orrne r8,r8,#1
str r9,[r4]
str r8,[r4,#4]
mov sp,r7
sub sp,sp,#16
add sp,sp,#1152
msr cpsr_f,r6
pop {r4-r10,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_mem_str_register_4_r0
.type cpu_arm7_thumb_mem_str_register_4_r0, %function
cpu_arm7_thumb_mem_str_register_4_r0:
push {r4-r10,lr}
mov r4,r3
mrs r6,cpsr
mov r8,r2
mov r9,r0
mov r10,r1
sub sp,sp,#1152
mov r0,sp
mov r1,r10
mov r2,#288
9:
str r1,[r0],#4
subs r2,r2,#1
bne 9b
add sp,sp,#16
mov r7,sp
mov r0,r9
mov r1,r7
ldr r2,=4
adr r5,2f
adr r3,1f
orr r3,r3,#1
msr cpsr_f,r8
bx r3
.thumb
.balign 4
1:
.global cpu_arm7_thumb_mem_str_register_4_r0_instruction
cpu_arm7_thumb_mem_str_register_4_r0_instruction:
str r0,[r1,r2]
nop
bx r5
nop
.arm
.balign 4
2:
mrs r3,cpsr
and r8,r3,#0xf0000000
ldr r9,[r7,#4]
cmp r1,r7
orrne r8,r8,#2
ldr r3,=4
cmp r2,r3
orrne r8,r8,#4
cmp sp,r7
orrne r8,r8,#8
ldr r0,[r7,#0]
cmp r0,r10
orrne r8,r8,#1
ldr r0,[r7,#8]
cmp r0,r10
orrne r8,r8,#1
str r9,[r4]
str r8,[r4,#4]
mov sp,r7
sub sp,sp,#16
add sp,sp,#1152
msr cpsr_f,r6
pop {r4-r10,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_mem_str_register_128_r0
.type cpu_arm7_thumb_mem_str_register_128_r0, %function
cpu_arm7_thumb_mem_str_register_128_r0:
push {r4-r10,lr}
mov r4,r3
mrs r6,cpsr
mov r8,r2
mov r9,r0
mov r10,r1
sub sp,sp,#1152
mov r0,sp
mov r1,r10
mov r2,#288
9:
str r1,[r0],#4
subs r2,r2,#1
bne 9b
add sp,sp,#16
mov r7,sp
mov r0,r9
mov r1,r7
ldr r2,=128
adr r5,2f
adr r3,1f
orr r3,r3,#1
msr cpsr_f,r8
bx r3
.thumb
.balign 4
1:
.global cpu_arm7_thumb_mem_str_register_128_r0_instruction
cpu_arm7_thumb_mem_str_register_128_r0_instruction:
str r0,[r1,r2]
nop
bx r5
nop
.arm
.balign 4
2:
mrs r3,cpsr
and r8,r3,#0xf0000000
ldr r9,[r7,#128]
cmp r1,r7
orrne r8,r8,#2
ldr r3,=128
cmp r2,r3
orrne r8,r8,#4
cmp sp,r7
orrne r8,r8,#8
ldr r0,[r7,#124]
cmp r0,r10
orrne r8,r8,#1
ldr r0,[r7,#132]
cmp r0,r10
orrne r8,r8,#1
str r9,[r4]
str r8,[r4,#4]
mov sp,r7
sub sp,sp,#16
add sp,sp,#1152
msr cpsr_f,r6
pop {r4-r10,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_mem_strb_register_0_r0
.type cpu_arm7_thumb_mem_strb_register_0_r0, %function
cpu_arm7_thumb_mem_strb_register_0_r0:
push {r4-r10,lr}
mov r4,r3
mrs r6,cpsr
mov r8,r2
mov r9,r0
mov r10,r1
sub sp,sp,#1152
mov r0,sp
mov r1,r10
mov r2,#288
9:
str r1,[r0],#4
subs r2,r2,#1
bne 9b
add sp,sp,#16
mov r7,sp
mov r0,r9
mov r1,r7
ldr r2,=0
adr r5,2f
adr r3,1f
orr r3,r3,#1
msr cpsr_f,r8
bx r3
.thumb
.balign 4
1:
.global cpu_arm7_thumb_mem_strb_register_0_r0_instruction
cpu_arm7_thumb_mem_strb_register_0_r0_instruction:
strb r0,[r1,r2]
nop
bx r5
nop
.arm
.balign 4
2:
mrs r3,cpsr
and r8,r3,#0xf0000000
ldr r9,[r7,#0]
cmp r1,r7
orrne r8,r8,#2
ldr r3,=0
cmp r2,r3
orrne r8,r8,#4
cmp sp,r7
orrne r8,r8,#8
ldr r0,[r7,#-4]
cmp r0,r10
orrne r8,r8,#1
ldr r0,[r7,#4]
cmp r0,r10
orrne r8,r8,#1
str r9,[r4]
str r8,[r4,#4]
mov sp,r7
sub sp,sp,#16
add sp,sp,#1152
msr cpsr_f,r6
pop {r4-r10,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_mem_strb_register_1_r0
.type cpu_arm7_thumb_mem_strb_register_1_r0, %function
cpu_arm7_thumb_mem_strb_register_1_r0:
push {r4-r10,lr}
mov r4,r3
mrs r6,cpsr
mov r8,r2
mov r9,r0
mov r10,r1
sub sp,sp,#1152
mov r0,sp
mov r1,r10
mov r2,#288
9:
str r1,[r0],#4
subs r2,r2,#1
bne 9b
add sp,sp,#16
mov r7,sp
mov r0,r9
mov r1,r7
ldr r2,=1
adr r5,2f
adr r3,1f
orr r3,r3,#1
msr cpsr_f,r8
bx r3
.thumb
.balign 4
1:
.global cpu_arm7_thumb_mem_strb_register_1_r0_instruction
cpu_arm7_thumb_mem_strb_register_1_r0_instruction:
strb r0,[r1,r2]
nop
bx r5
nop
.arm
.balign 4
2:
mrs r3,cpsr
and r8,r3,#0xf0000000
ldr r9,[r7,#0]
cmp r1,r7
orrne r8,r8,#2
ldr r3,=1
cmp r2,r3
orrne r8,r8,#4
cmp sp,r7
orrne r8,r8,#8
ldr r0,[r7,#-4]
cmp r0,r10
orrne r8,r8,#1
ldr r0,[r7,#4]
cmp r0,r10
orrne r8,r8,#1
str r9,[r4]
str r8,[r4,#4]
mov sp,r7
sub sp,sp,#16
add sp,sp,#1152
msr cpsr_f,r6
pop {r4-r10,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_mem_strb_register_3_r0
.type cpu_arm7_thumb_mem_strb_register_3_r0, %function
cpu_arm7_thumb_mem_strb_register_3_r0:
push {r4-r10,lr}
mov r4,r3
mrs r6,cpsr
mov r8,r2
mov r9,r0
mov r10,r1
sub sp,sp,#1152
mov r0,sp
mov r1,r10
mov r2,#288
9:
str r1,[r0],#4
subs r2,r2,#1
bne 9b
add sp,sp,#16
mov r7,sp
mov r0,r9
mov r1,r7
ldr r2,=3
adr r5,2f
adr r3,1f
orr r3,r3,#1
msr cpsr_f,r8
bx r3
.thumb
.balign 4
1:
.global cpu_arm7_thumb_mem_strb_register_3_r0_instruction
cpu_arm7_thumb_mem_strb_register_3_r0_instruction:
strb r0,[r1,r2]
nop
bx r5
nop
.arm
.balign 4
2:
mrs r3,cpsr
and r8,r3,#0xf0000000
ldr r9,[r7,#0]
cmp r1,r7
orrne r8,r8,#2
ldr r3,=3
cmp r2,r3
orrne r8,r8,#4
cmp sp,r7
orrne r8,r8,#8
ldr r0,[r7,#-4]
cmp r0,r10
orrne r8,r8,#1
ldr r0,[r7,#4]
cmp r0,r10
orrne r8,r8,#1
str r9,[r4]
str r8,[r4,#4]
mov sp,r7
sub sp,sp,#16
add sp,sp,#1152
msr cpsr_f,r6
pop {r4-r10,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_mem_strh_register_0_r0
.type cpu_arm7_thumb_mem_strh_register_0_r0, %function
cpu_arm7_thumb_mem_strh_register_0_r0:
push {r4-r10,lr}
mov r4,r3
mrs r6,cpsr
mov r8,r2
mov r9,r0
mov r10,r1
sub sp,sp,#1152
mov r0,sp
mov r1,r10
mov r2,#288
9:
str r1,[r0],#4
subs r2,r2,#1
bne 9b
add sp,sp,#16
mov r7,sp
mov r0,r9
mov r1,r7
ldr r2,=0
adr r5,2f
adr r3,1f
orr r3,r3,#1
msr cpsr_f,r8
bx r3
.thumb
.balign 4
1:
.global cpu_arm7_thumb_mem_strh_register_0_r0_instruction
cpu_arm7_thumb_mem_strh_register_0_r0_instruction:
strh r0,[r1,r2]
nop
bx r5
nop
.arm
.balign 4
2:
mrs r3,cpsr
and r8,r3,#0xf0000000
ldr r9,[r7,#0]
cmp r1,r7
orrne r8,r8,#2
ldr r3,=0
cmp r2,r3
orrne r8,r8,#4
cmp sp,r7
orrne r8,r8,#8
ldr r0,[r7,#-4]
cmp r0,r10
orrne r8,r8,#1
ldr r0,[r7,#4]
cmp r0,r10
orrne r8,r8,#1
str r9,[r4]
str r8,[r4,#4]
mov sp,r7
sub sp,sp,#16
add sp,sp,#1152
msr cpsr_f,r6
pop {r4-r10,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_mem_strh_register_2_r0
.type cpu_arm7_thumb_mem_strh_register_2_r0, %function
cpu_arm7_thumb_mem_strh_register_2_r0:
push {r4-r10,lr}
mov r4,r3
mrs r6,cpsr
mov r8,r2
mov r9,r0
mov r10,r1
sub sp,sp,#1152
mov r0,sp
mov r1,r10
mov r2,#288
9:
str r1,[r0],#4
subs r2,r2,#1
bne 9b
add sp,sp,#16
mov r7,sp
mov r0,r9
mov r1,r7
ldr r2,=2
adr r5,2f
adr r3,1f
orr r3,r3,#1
msr cpsr_f,r8
bx r3
.thumb
.balign 4
1:
.global cpu_arm7_thumb_mem_strh_register_2_r0_instruction
cpu_arm7_thumb_mem_strh_register_2_r0_instruction:
strh r0,[r1,r2]
nop
bx r5
nop
.arm
.balign 4
2:
mrs r3,cpsr
and r8,r3,#0xf0000000
ldr r9,[r7,#0]
cmp r1,r7
orrne r8,r8,#2
ldr r3,=2
cmp r2,r3
orrne r8,r8,#4
cmp sp,r7
orrne r8,r8,#8
ldr r0,[r7,#-4]
cmp r0,r10
orrne r8,r8,#1
ldr r0,[r7,#4]
cmp r0,r10
orrne r8,r8,#1
str r9,[r4]
str r8,[r4,#4]
mov sp,r7
sub sp,sp,#16
add sp,sp,#1152
msr cpsr_f,r6
pop {r4-r10,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_mem_strh_register_62_r0
.type cpu_arm7_thumb_mem_strh_register_62_r0, %function
cpu_arm7_thumb_mem_strh_register_62_r0:
push {r4-r10,lr}
mov r4,r3
mrs r6,cpsr
mov r8,r2
mov r9,r0
mov r10,r1
sub sp,sp,#1152
mov r0,sp
mov r1,r10
mov r2,#288
9:
str r1,[r0],#4
subs r2,r2,#1
bne 9b
add sp,sp,#16
mov r7,sp
mov r0,r9
mov r1,r7
ldr r2,=62
adr r5,2f
adr r3,1f
orr r3,r3,#1
msr cpsr_f,r8
bx r3
.thumb
.balign 4
1:
.global cpu_arm7_thumb_mem_strh_register_62_r0_instruction
cpu_arm7_thumb_mem_strh_register_62_r0_instruction:
strh r0,[r1,r2]
nop
bx r5
nop
.arm
.balign 4
2:
mrs r3,cpsr
and r8,r3,#0xf0000000
ldr r9,[r7,#60]
cmp r1,r7
orrne r8,r8,#2
ldr r3,=62
cmp r2,r3
orrne r8,r8,#4
cmp sp,r7
orrne r8,r8,#8
ldr r0,[r7,#56]
cmp r0,r10
orrne r8,r8,#1
ldr r0,[r7,#64]
cmp r0,r10
orrne r8,r8,#1
str r9,[r4]
str r8,[r4,#4]
mov sp,r7
sub sp,sp,#16
add sp,sp,#1152
msr cpsr_f,r6
pop {r4-r10,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_sp_add_sp_0_r13
.type cpu_arm7_thumb_sp_add_sp_0_r13, %function
cpu_arm7_thumb_sp_add_sp_0_r13:
push {r4-r8,lr}
mov r4,r3
mrs r6,cpsr
mov r7,sp
sub sp,sp,#256
add sp,sp,r0
mov r8,sp
mvn r0,#0
mvn r1,#0
adr r5,2f
adr r3,1f
orr r3,r3,#1
msr cpsr_f,r2
bx r3
.thumb
.balign 4
1:
.global cpu_arm7_thumb_sp_add_sp_0_r13_instruction
cpu_arm7_thumb_sp_add_sp_0_r13_instruction:
add sp,#0
nop
bx r5
nop
.arm
.balign 4
2:
mrs r2,cpsr
and r2,r2,#0xf0000000
sub r0,sp,r8
str r0,[r4]
str r2,[r4,#4]
mov sp,r7
msr cpsr_f,r6
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_sp_add_sp_4_r13
.type cpu_arm7_thumb_sp_add_sp_4_r13, %function
cpu_arm7_thumb_sp_add_sp_4_r13:
push {r4-r8,lr}
mov r4,r3
mrs r6,cpsr
mov r7,sp
sub sp,sp,#256
add sp,sp,r0
mov r8,sp
mvn r0,#0
mvn r1,#0
adr r5,2f
adr r3,1f
orr r3,r3,#1
msr cpsr_f,r2
bx r3
.thumb
.balign 4
1:
.global cpu_arm7_thumb_sp_add_sp_4_r13_instruction
cpu_arm7_thumb_sp_add_sp_4_r13_instruction:
add sp,#4
nop
bx r5
nop
.arm
.balign 4
2:
mrs r2,cpsr
and r2,r2,#0xf0000000
sub r0,sp,r8
str r0,[r4]
str r2,[r4,#4]
mov sp,r7
msr cpsr_f,r6
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_sp_add_sp_508_r13
.type cpu_arm7_thumb_sp_add_sp_508_r13, %function
cpu_arm7_thumb_sp_add_sp_508_r13:
push {r4-r8,lr}
mov r4,r3
mrs r6,cpsr
mov r7,sp
sub sp,sp,#256
add sp,sp,r0
mov r8,sp
mvn r0,#0
mvn r1,#0
adr r5,2f
adr r3,1f
orr r3,r3,#1
msr cpsr_f,r2
bx r3
.thumb
.balign 4
1:
.global cpu_arm7_thumb_sp_add_sp_508_r13_instruction
cpu_arm7_thumb_sp_add_sp_508_r13_instruction:
add sp,#508
nop
bx r5
nop
.arm
.balign 4
2:
mrs r2,cpsr
and r2,r2,#0xf0000000
sub r0,sp,r8
str r0,[r4]
str r2,[r4,#4]
mov sp,r7
msr cpsr_f,r6
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_sp_sub_sp_0_r13
.type cpu_arm7_thumb_sp_sub_sp_0_r13, %function
cpu_arm7_thumb_sp_sub_sp_0_r13:
push {r4-r8,lr}
mov r4,r3
mrs r6,cpsr
mov r7,sp
sub sp,sp,#256
add sp,sp,r0
mov r8,sp
mvn r0,#0
mvn r1,#0
adr r5,2f
adr r3,1f
orr r3,r3,#1
msr cpsr_f,r2
bx r3
.thumb
.balign 4
1:
.global cpu_arm7_thumb_sp_sub_sp_0_r13_instruction
cpu_arm7_thumb_sp_sub_sp_0_r13_instruction:
sub sp,#0
nop
bx r5
nop
.arm
.balign 4
2:
mrs r2,cpsr
and r2,r2,#0xf0000000
sub r0,sp,r8
str r0,[r4]
str r2,[r4,#4]
mov sp,r7
msr cpsr_f,r6
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_sp_sub_sp_4_r13
.type cpu_arm7_thumb_sp_sub_sp_4_r13, %function
cpu_arm7_thumb_sp_sub_sp_4_r13:
push {r4-r8,lr}
mov r4,r3
mrs r6,cpsr
mov r7,sp
sub sp,sp,#256
add sp,sp,r0
mov r8,sp
mvn r0,#0
mvn r1,#0
adr r5,2f
adr r3,1f
orr r3,r3,#1
msr cpsr_f,r2
bx r3
.thumb
.balign 4
1:
.global cpu_arm7_thumb_sp_sub_sp_4_r13_instruction
cpu_arm7_thumb_sp_sub_sp_4_r13_instruction:
sub sp,#4
nop
bx r5
nop
.arm
.balign 4
2:
mrs r2,cpsr
and r2,r2,#0xf0000000
sub r0,sp,r8
str r0,[r4]
str r2,[r4,#4]
mov sp,r7
msr cpsr_f,r6
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_sp_sub_sp_508_r13
.type cpu_arm7_thumb_sp_sub_sp_508_r13, %function
cpu_arm7_thumb_sp_sub_sp_508_r13:
push {r4-r8,lr}
mov r4,r3
mrs r6,cpsr
mov r7,sp
sub sp,sp,#256
add sp,sp,r0
mov r8,sp
mvn r0,#0
mvn r1,#0
adr r5,2f
adr r3,1f
orr r3,r3,#1
msr cpsr_f,r2
bx r3
.thumb
.balign 4
1:
.global cpu_arm7_thumb_sp_sub_sp_508_r13_instruction
cpu_arm7_thumb_sp_sub_sp_508_r13_instruction:
sub sp,#508
nop
bx r5
nop
.arm
.balign 4
2:
mrs r2,cpsr
and r2,r2,#0xf0000000
sub r0,sp,r8
str r0,[r4]
str r2,[r4,#4]
mov sp,r7
msr cpsr_f,r6
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_sp_address_sp_0_r0
.type cpu_arm7_thumb_sp_address_sp_0_r0, %function
cpu_arm7_thumb_sp_address_sp_0_r0:
push {r4-r8,lr}
mov r4,r3
mrs r6,cpsr
mov r7,sp
sub sp,sp,#256
add sp,sp,r0
mov r8,sp
mvn r0,#0
mvn r1,#0
adr r5,2f
adr r3,1f
orr r3,r3,#1
msr cpsr_f,r2
bx r3
.thumb
.balign 4
1:
.global cpu_arm7_thumb_sp_address_sp_0_r0_instruction
cpu_arm7_thumb_sp_address_sp_0_r0_instruction:
add r0,sp,#0
nop
bx r5
nop
.arm
.balign 4
2:
mrs r2,cpsr
and r2,r2,#0xf0000000
sub r0,r0,r8
cmp sp,r8
orrne r2,r2,#1
str r0,[r4]
str r2,[r4,#4]
mov sp,r7
msr cpsr_f,r6
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_sp_address_sp_4_r0
.type cpu_arm7_thumb_sp_address_sp_4_r0, %function
cpu_arm7_thumb_sp_address_sp_4_r0:
push {r4-r8,lr}
mov r4,r3
mrs r6,cpsr
mov r7,sp
sub sp,sp,#256
add sp,sp,r0
mov r8,sp
mvn r0,#0
mvn r1,#0
adr r5,2f
adr r3,1f
orr r3,r3,#1
msr cpsr_f,r2
bx r3
.thumb
.balign 4
1:
.global cpu_arm7_thumb_sp_address_sp_4_r0_instruction
cpu_arm7_thumb_sp_address_sp_4_r0_instruction:
add r0,sp,#4
nop
bx r5
nop
.arm
.balign 4
2:
mrs r2,cpsr
and r2,r2,#0xf0000000
sub r0,r0,r8
cmp sp,r8
orrne r2,r2,#1
str r0,[r4]
str r2,[r4,#4]
mov sp,r7
msr cpsr_f,r6
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_sp_address_sp_1020_r0
.type cpu_arm7_thumb_sp_address_sp_1020_r0, %function
cpu_arm7_thumb_sp_address_sp_1020_r0:
push {r4-r8,lr}
mov r4,r3
mrs r6,cpsr
mov r7,sp
sub sp,sp,#256
add sp,sp,r0
mov r8,sp
mvn r0,#0
mvn r1,#0
adr r5,2f
adr r3,1f
orr r3,r3,#1
msr cpsr_f,r2
bx r3
.thumb
.balign 4
1:
.global cpu_arm7_thumb_sp_address_sp_1020_r0_instruction
cpu_arm7_thumb_sp_address_sp_1020_r0_instruction:
add r0,sp,#1020
nop
bx r5
nop
.arm
.balign 4
2:
mrs r2,cpsr
and r2,r2,#0xf0000000
sub r0,r0,r8
cmp sp,r8
orrne r2,r2,#1
str r0,[r4]
str r2,[r4,#4]
mov sp,r7
msr cpsr_f,r6
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_sp_address_sp_0_r1
.type cpu_arm7_thumb_sp_address_sp_0_r1, %function
cpu_arm7_thumb_sp_address_sp_0_r1:
push {r4-r8,lr}
mov r4,r3
mrs r6,cpsr
mov r7,sp
sub sp,sp,#256
add sp,sp,r0
mov r8,sp
mvn r0,#0
mvn r1,#0
adr r5,2f
adr r3,1f
orr r3,r3,#1
msr cpsr_f,r2
bx r3
.thumb
.balign 4
1:
.global cpu_arm7_thumb_sp_address_sp_0_r1_instruction
cpu_arm7_thumb_sp_address_sp_0_r1_instruction:
add r1,sp,#0
nop
bx r5
nop
.arm
.balign 4
2:
mrs r2,cpsr
and r2,r2,#0xf0000000
sub r0,r1,r8
cmp sp,r8
orrne r2,r2,#1
str r0,[r4]
str r2,[r4,#4]
mov sp,r7
msr cpsr_f,r6
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_sp_address_sp_4_r1
.type cpu_arm7_thumb_sp_address_sp_4_r1, %function
cpu_arm7_thumb_sp_address_sp_4_r1:
push {r4-r8,lr}
mov r4,r3
mrs r6,cpsr
mov r7,sp
sub sp,sp,#256
add sp,sp,r0
mov r8,sp
mvn r0,#0
mvn r1,#0
adr r5,2f
adr r3,1f
orr r3,r3,#1
msr cpsr_f,r2
bx r3
.thumb
.balign 4
1:
.global cpu_arm7_thumb_sp_address_sp_4_r1_instruction
cpu_arm7_thumb_sp_address_sp_4_r1_instruction:
add r1,sp,#4
nop
bx r5
nop
.arm
.balign 4
2:
mrs r2,cpsr
and r2,r2,#0xf0000000
sub r0,r1,r8
cmp sp,r8
orrne r2,r2,#1
str r0,[r4]
str r2,[r4,#4]
mov sp,r7
msr cpsr_f,r6
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_sp_address_sp_1020_r1
.type cpu_arm7_thumb_sp_address_sp_1020_r1, %function
cpu_arm7_thumb_sp_address_sp_1020_r1:
push {r4-r8,lr}
mov r4,r3
mrs r6,cpsr
mov r7,sp
sub sp,sp,#256
add sp,sp,r0
mov r8,sp
mvn r0,#0
mvn r1,#0
adr r5,2f
adr r3,1f
orr r3,r3,#1
msr cpsr_f,r2
bx r3
.thumb
.balign 4
1:
.global cpu_arm7_thumb_sp_address_sp_1020_r1_instruction
cpu_arm7_thumb_sp_address_sp_1020_r1_instruction:
add r1,sp,#1020
nop
bx r5
nop
.arm
.balign 4
2:
mrs r2,cpsr
and r2,r2,#0xf0000000
sub r0,r1,r8
cmp sp,r8
orrne r2,r2,#1
str r0,[r4]
str r2,[r4,#4]
mov sp,r7
msr cpsr_f,r6
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_pc_address_pc_a0_o0_r0_v00000000
.type cpu_arm7_thumb_pc_address_pc_a0_o0_r0_v00000000, %function
cpu_arm7_thumb_pc_address_pc_a0_o0_r0_v00000000:
push {r4-r8,lr}
mov r4,r3
mrs r6,cpsr
mov r7,sp
ldr r0,=0xffffffff
mov r1,r0
ldr r8,=cpu_arm7_thumb_pc_address_pc_a0_o0_r0_v00000000_instruction
add r8,r8,#4
bic r8,r8,#3
ldr r5,=2f
ldr r3,=cpu_arm7_thumb_pc_address_pc_a0_o0_r0_v00000000_instruction+1
msr cpsr_f,r2
bx r3
.thumb
.balign 4
.global cpu_arm7_thumb_pc_address_pc_a0_o0_r0_v00000000_instruction
cpu_arm7_thumb_pc_address_pc_a0_o0_r0_v00000000_instruction:
.hword 0xa000
bx r5
.arm
.balign 4
2:
mrs r2,cpsr
and r2,r2,#0xf0000000
sub r0,r0,r8
cmp sp,r7
orrne r2,r2,#1
str r0,[r4]
str r2,[r4,#4]
msr cpsr_f,r6
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_pc_address_pc_a0_o0_r1_v00000000
.type cpu_arm7_thumb_pc_address_pc_a0_o0_r1_v00000000, %function
cpu_arm7_thumb_pc_address_pc_a0_o0_r1_v00000000:
push {r4-r8,lr}
mov r4,r3
mrs r6,cpsr
mov r7,sp
ldr r0,=0xffffffff
mov r1,r0
ldr r8,=cpu_arm7_thumb_pc_address_pc_a0_o0_r1_v00000000_instruction
add r8,r8,#4
bic r8,r8,#3
ldr r5,=2f
ldr r3,=cpu_arm7_thumb_pc_address_pc_a0_o0_r1_v00000000_instruction+1
msr cpsr_f,r2
bx r3
.thumb
.balign 4
.global cpu_arm7_thumb_pc_address_pc_a0_o0_r1_v00000000_instruction
cpu_arm7_thumb_pc_address_pc_a0_o0_r1_v00000000_instruction:
.hword 0xa100
bx r5
.arm
.balign 4
2:
mrs r2,cpsr
and r2,r2,#0xf0000000
sub r0,r1,r8
cmp sp,r7
orrne r2,r2,#1
str r0,[r4]
str r2,[r4,#4]
msr cpsr_f,r6
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_pc_address_pc_a0_o4_r0_v00000000
.type cpu_arm7_thumb_pc_address_pc_a0_o4_r0_v00000000, %function
cpu_arm7_thumb_pc_address_pc_a0_o4_r0_v00000000:
push {r4-r8,lr}
mov r4,r3
mrs r6,cpsr
mov r7,sp
ldr r0,=0xffffffff
mov r1,r0
ldr r8,=cpu_arm7_thumb_pc_address_pc_a0_o4_r0_v00000000_instruction
add r8,r8,#4
bic r8,r8,#3
ldr r5,=2f
ldr r3,=cpu_arm7_thumb_pc_address_pc_a0_o4_r0_v00000000_instruction+1
msr cpsr_f,r2
bx r3
.thumb
.balign 4
.global cpu_arm7_thumb_pc_address_pc_a0_o4_r0_v00000000_instruction
cpu_arm7_thumb_pc_address_pc_a0_o4_r0_v00000000_instruction:
.hword 0xa001
bx r5
.arm
.balign 4
2:
mrs r2,cpsr
and r2,r2,#0xf0000000
sub r0,r0,r8
cmp sp,r7
orrne r2,r2,#1
str r0,[r4]
str r2,[r4,#4]
msr cpsr_f,r6
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_pc_address_pc_a0_o4_r1_v00000000
.type cpu_arm7_thumb_pc_address_pc_a0_o4_r1_v00000000, %function
cpu_arm7_thumb_pc_address_pc_a0_o4_r1_v00000000:
push {r4-r8,lr}
mov r4,r3
mrs r6,cpsr
mov r7,sp
ldr r0,=0xffffffff
mov r1,r0
ldr r8,=cpu_arm7_thumb_pc_address_pc_a0_o4_r1_v00000000_instruction
add r8,r8,#4
bic r8,r8,#3
ldr r5,=2f
ldr r3,=cpu_arm7_thumb_pc_address_pc_a0_o4_r1_v00000000_instruction+1
msr cpsr_f,r2
bx r3
.thumb
.balign 4
.global cpu_arm7_thumb_pc_address_pc_a0_o4_r1_v00000000_instruction
cpu_arm7_thumb_pc_address_pc_a0_o4_r1_v00000000_instruction:
.hword 0xa101
bx r5
.arm
.balign 4
2:
mrs r2,cpsr
and r2,r2,#0xf0000000
sub r0,r1,r8
cmp sp,r7
orrne r2,r2,#1
str r0,[r4]
str r2,[r4,#4]
msr cpsr_f,r6
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_pc_address_pc_a0_o1020_r0_v00000000
.type cpu_arm7_thumb_pc_address_pc_a0_o1020_r0_v00000000, %function
cpu_arm7_thumb_pc_address_pc_a0_o1020_r0_v00000000:
push {r4-r8,lr}
mov r4,r3
mrs r6,cpsr
mov r7,sp
ldr r0,=0xffffffff
mov r1,r0
ldr r8,=cpu_arm7_thumb_pc_address_pc_a0_o1020_r0_v00000000_instruction
add r8,r8,#4
bic r8,r8,#3
ldr r5,=2f
ldr r3,=cpu_arm7_thumb_pc_address_pc_a0_o1020_r0_v00000000_instruction+1
msr cpsr_f,r2
bx r3
.thumb
.balign 4
.global cpu_arm7_thumb_pc_address_pc_a0_o1020_r0_v00000000_instruction
cpu_arm7_thumb_pc_address_pc_a0_o1020_r0_v00000000_instruction:
.hword 0xa0ff
bx r5
.arm
.balign 4
2:
mrs r2,cpsr
and r2,r2,#0xf0000000
sub r0,r0,r8
cmp sp,r7
orrne r2,r2,#1
str r0,[r4]
str r2,[r4,#4]
msr cpsr_f,r6
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_pc_address_pc_a0_o1020_r1_v00000000
.type cpu_arm7_thumb_pc_address_pc_a0_o1020_r1_v00000000, %function
cpu_arm7_thumb_pc_address_pc_a0_o1020_r1_v00000000:
push {r4-r8,lr}
mov r4,r3
mrs r6,cpsr
mov r7,sp
ldr r0,=0xffffffff
mov r1,r0
ldr r8,=cpu_arm7_thumb_pc_address_pc_a0_o1020_r1_v00000000_instruction
add r8,r8,#4
bic r8,r8,#3
ldr r5,=2f
ldr r3,=cpu_arm7_thumb_pc_address_pc_a0_o1020_r1_v00000000_instruction+1
msr cpsr_f,r2
bx r3
.thumb
.balign 4
.global cpu_arm7_thumb_pc_address_pc_a0_o1020_r1_v00000000_instruction
cpu_arm7_thumb_pc_address_pc_a0_o1020_r1_v00000000_instruction:
.hword 0xa1ff
bx r5
.arm
.balign 4
2:
mrs r2,cpsr
and r2,r2,#0xf0000000
sub r0,r1,r8
cmp sp,r7
orrne r2,r2,#1
str r0,[r4]
str r2,[r4,#4]
msr cpsr_f,r6
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_pc_address_pc_a2_o0_r0_v00000000
.type cpu_arm7_thumb_pc_address_pc_a2_o0_r0_v00000000, %function
cpu_arm7_thumb_pc_address_pc_a2_o0_r0_v00000000:
push {r4-r8,lr}
mov r4,r3
mrs r6,cpsr
mov r7,sp
ldr r0,=0xffffffff
mov r1,r0
ldr r8,=cpu_arm7_thumb_pc_address_pc_a2_o0_r0_v00000000_instruction
add r8,r8,#4
bic r8,r8,#3
ldr r5,=2f
ldr r3,=cpu_arm7_thumb_pc_address_pc_a2_o0_r0_v00000000_instruction+1
msr cpsr_f,r2
bx r3
.thumb
.balign 4
nop
.global cpu_arm7_thumb_pc_address_pc_a2_o0_r0_v00000000_instruction
cpu_arm7_thumb_pc_address_pc_a2_o0_r0_v00000000_instruction:
.hword 0xa000
bx r5
.arm
.balign 4
2:
mrs r2,cpsr
and r2,r2,#0xf0000000
sub r0,r0,r8
cmp sp,r7
orrne r2,r2,#1
str r0,[r4]
str r2,[r4,#4]
msr cpsr_f,r6
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_pc_address_pc_a2_o0_r1_v00000000
.type cpu_arm7_thumb_pc_address_pc_a2_o0_r1_v00000000, %function
cpu_arm7_thumb_pc_address_pc_a2_o0_r1_v00000000:
push {r4-r8,lr}
mov r4,r3
mrs r6,cpsr
mov r7,sp
ldr r0,=0xffffffff
mov r1,r0
ldr r8,=cpu_arm7_thumb_pc_address_pc_a2_o0_r1_v00000000_instruction
add r8,r8,#4
bic r8,r8,#3
ldr r5,=2f
ldr r3,=cpu_arm7_thumb_pc_address_pc_a2_o0_r1_v00000000_instruction+1
msr cpsr_f,r2
bx r3
.thumb
.balign 4
nop
.global cpu_arm7_thumb_pc_address_pc_a2_o0_r1_v00000000_instruction
cpu_arm7_thumb_pc_address_pc_a2_o0_r1_v00000000_instruction:
.hword 0xa100
bx r5
.arm
.balign 4
2:
mrs r2,cpsr
and r2,r2,#0xf0000000
sub r0,r1,r8
cmp sp,r7
orrne r2,r2,#1
str r0,[r4]
str r2,[r4,#4]
msr cpsr_f,r6
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_pc_address_pc_a2_o4_r0_v00000000
.type cpu_arm7_thumb_pc_address_pc_a2_o4_r0_v00000000, %function
cpu_arm7_thumb_pc_address_pc_a2_o4_r0_v00000000:
push {r4-r8,lr}
mov r4,r3
mrs r6,cpsr
mov r7,sp
ldr r0,=0xffffffff
mov r1,r0
ldr r8,=cpu_arm7_thumb_pc_address_pc_a2_o4_r0_v00000000_instruction
add r8,r8,#4
bic r8,r8,#3
ldr r5,=2f
ldr r3,=cpu_arm7_thumb_pc_address_pc_a2_o4_r0_v00000000_instruction+1
msr cpsr_f,r2
bx r3
.thumb
.balign 4
nop
.global cpu_arm7_thumb_pc_address_pc_a2_o4_r0_v00000000_instruction
cpu_arm7_thumb_pc_address_pc_a2_o4_r0_v00000000_instruction:
.hword 0xa001
bx r5
.arm
.balign 4
2:
mrs r2,cpsr
and r2,r2,#0xf0000000
sub r0,r0,r8
cmp sp,r7
orrne r2,r2,#1
str r0,[r4]
str r2,[r4,#4]
msr cpsr_f,r6
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_pc_address_pc_a2_o4_r1_v00000000
.type cpu_arm7_thumb_pc_address_pc_a2_o4_r1_v00000000, %function
cpu_arm7_thumb_pc_address_pc_a2_o4_r1_v00000000:
push {r4-r8,lr}
mov r4,r3
mrs r6,cpsr
mov r7,sp
ldr r0,=0xffffffff
mov r1,r0
ldr r8,=cpu_arm7_thumb_pc_address_pc_a2_o4_r1_v00000000_instruction
add r8,r8,#4
bic r8,r8,#3
ldr r5,=2f
ldr r3,=cpu_arm7_thumb_pc_address_pc_a2_o4_r1_v00000000_instruction+1
msr cpsr_f,r2
bx r3
.thumb
.balign 4
nop
.global cpu_arm7_thumb_pc_address_pc_a2_o4_r1_v00000000_instruction
cpu_arm7_thumb_pc_address_pc_a2_o4_r1_v00000000_instruction:
.hword 0xa101
bx r5
.arm
.balign 4
2:
mrs r2,cpsr
and r2,r2,#0xf0000000
sub r0,r1,r8
cmp sp,r7
orrne r2,r2,#1
str r0,[r4]
str r2,[r4,#4]
msr cpsr_f,r6
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_pc_address_pc_a2_o1020_r0_v00000000
.type cpu_arm7_thumb_pc_address_pc_a2_o1020_r0_v00000000, %function
cpu_arm7_thumb_pc_address_pc_a2_o1020_r0_v00000000:
push {r4-r8,lr}
mov r4,r3
mrs r6,cpsr
mov r7,sp
ldr r0,=0xffffffff
mov r1,r0
ldr r8,=cpu_arm7_thumb_pc_address_pc_a2_o1020_r0_v00000000_instruction
add r8,r8,#4
bic r8,r8,#3
ldr r5,=2f
ldr r3,=cpu_arm7_thumb_pc_address_pc_a2_o1020_r0_v00000000_instruction+1
msr cpsr_f,r2
bx r3
.thumb
.balign 4
nop
.global cpu_arm7_thumb_pc_address_pc_a2_o1020_r0_v00000000_instruction
cpu_arm7_thumb_pc_address_pc_a2_o1020_r0_v00000000_instruction:
.hword 0xa0ff
bx r5
.arm
.balign 4
2:
mrs r2,cpsr
and r2,r2,#0xf0000000
sub r0,r0,r8
cmp sp,r7
orrne r2,r2,#1
str r0,[r4]
str r2,[r4,#4]
msr cpsr_f,r6
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_pc_address_pc_a2_o1020_r1_v00000000
.type cpu_arm7_thumb_pc_address_pc_a2_o1020_r1_v00000000, %function
cpu_arm7_thumb_pc_address_pc_a2_o1020_r1_v00000000:
push {r4-r8,lr}
mov r4,r3
mrs r6,cpsr
mov r7,sp
ldr r0,=0xffffffff
mov r1,r0
ldr r8,=cpu_arm7_thumb_pc_address_pc_a2_o1020_r1_v00000000_instruction
add r8,r8,#4
bic r8,r8,#3
ldr r5,=2f
ldr r3,=cpu_arm7_thumb_pc_address_pc_a2_o1020_r1_v00000000_instruction+1
msr cpsr_f,r2
bx r3
.thumb
.balign 4
nop
.global cpu_arm7_thumb_pc_address_pc_a2_o1020_r1_v00000000_instruction
cpu_arm7_thumb_pc_address_pc_a2_o1020_r1_v00000000_instruction:
.hword 0xa1ff
bx r5
.arm
.balign 4
2:
mrs r2,cpsr
and r2,r2,#0xf0000000
sub r0,r1,r8
cmp sp,r7
orrne r2,r2,#1
str r0,[r4]
str r2,[r4,#4]
msr cpsr_f,r6
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_pc_literal_ldr_a0_o0_r0_v00000000
.type cpu_arm7_thumb_pc_literal_ldr_a0_o0_r0_v00000000, %function
cpu_arm7_thumb_pc_literal_ldr_a0_o0_r0_v00000000:
push {r4-r8,lr}
mov r4,r3
mrs r6,cpsr
mov r7,sp
ldr r0,=0xffffffff
mov r1,r0
ldr r8,=cpu_arm7_thumb_pc_literal_ldr_a0_o0_r0_v00000000_instruction
add r8,r8,#4
bic r8,r8,#3
ldr r5,=2f
ldr r3,=cpu_arm7_thumb_pc_literal_ldr_a0_o0_r0_v00000000_instruction+1
msr cpsr_f,r2
bx r3
.thumb
.balign 4
.global cpu_arm7_thumb_pc_literal_ldr_a0_o0_r0_v00000000_instruction
cpu_arm7_thumb_pc_literal_ldr_a0_o0_r0_v00000000_instruction:
.hword 0x4800
bx r5
.word 0x00000000
.arm
.balign 4
2:
mrs r2,cpsr
and r2,r2,#0xf0000000
mov r0,r0
cmp sp,r7
orrne r2,r2,#1
str r0,[r4]
str r2,[r4,#4]
msr cpsr_f,r6
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_pc_literal_ldr_a0_o0_r0_vffffffff
.type cpu_arm7_thumb_pc_literal_ldr_a0_o0_r0_vffffffff, %function
cpu_arm7_thumb_pc_literal_ldr_a0_o0_r0_vffffffff:
push {r4-r8,lr}
mov r4,r3
mrs r6,cpsr
mov r7,sp
ldr r0,=0x00000000
mov r1,r0
ldr r8,=cpu_arm7_thumb_pc_literal_ldr_a0_o0_r0_vffffffff_instruction
add r8,r8,#4
bic r8,r8,#3
ldr r5,=2f
ldr r3,=cpu_arm7_thumb_pc_literal_ldr_a0_o0_r0_vffffffff_instruction+1
msr cpsr_f,r2
bx r3
.thumb
.balign 4
.global cpu_arm7_thumb_pc_literal_ldr_a0_o0_r0_vffffffff_instruction
cpu_arm7_thumb_pc_literal_ldr_a0_o0_r0_vffffffff_instruction:
.hword 0x4800
bx r5
.word 0xffffffff
.arm
.balign 4
2:
mrs r2,cpsr
and r2,r2,#0xf0000000
mov r0,r0
cmp sp,r7
orrne r2,r2,#1
str r0,[r4]
str r2,[r4,#4]
msr cpsr_f,r6
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_pc_literal_ldr_a0_o0_r0_v80000001
.type cpu_arm7_thumb_pc_literal_ldr_a0_o0_r0_v80000001, %function
cpu_arm7_thumb_pc_literal_ldr_a0_o0_r0_v80000001:
push {r4-r8,lr}
mov r4,r3
mrs r6,cpsr
mov r7,sp
ldr r0,=0x7ffffffe
mov r1,r0
ldr r8,=cpu_arm7_thumb_pc_literal_ldr_a0_o0_r0_v80000001_instruction
add r8,r8,#4
bic r8,r8,#3
ldr r5,=2f
ldr r3,=cpu_arm7_thumb_pc_literal_ldr_a0_o0_r0_v80000001_instruction+1
msr cpsr_f,r2
bx r3
.thumb
.balign 4
.global cpu_arm7_thumb_pc_literal_ldr_a0_o0_r0_v80000001_instruction
cpu_arm7_thumb_pc_literal_ldr_a0_o0_r0_v80000001_instruction:
.hword 0x4800
bx r5
.word 0x80000001
.arm
.balign 4
2:
mrs r2,cpsr
and r2,r2,#0xf0000000
mov r0,r0
cmp sp,r7
orrne r2,r2,#1
str r0,[r4]
str r2,[r4,#4]
msr cpsr_f,r6
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_pc_literal_ldr_a0_o0_r0_v12345678
.type cpu_arm7_thumb_pc_literal_ldr_a0_o0_r0_v12345678, %function
cpu_arm7_thumb_pc_literal_ldr_a0_o0_r0_v12345678:
push {r4-r8,lr}
mov r4,r3
mrs r6,cpsr
mov r7,sp
ldr r0,=0xedcba987
mov r1,r0
ldr r8,=cpu_arm7_thumb_pc_literal_ldr_a0_o0_r0_v12345678_instruction
add r8,r8,#4
bic r8,r8,#3
ldr r5,=2f
ldr r3,=cpu_arm7_thumb_pc_literal_ldr_a0_o0_r0_v12345678_instruction+1
msr cpsr_f,r2
bx r3
.thumb
.balign 4
.global cpu_arm7_thumb_pc_literal_ldr_a0_o0_r0_v12345678_instruction
cpu_arm7_thumb_pc_literal_ldr_a0_o0_r0_v12345678_instruction:
.hword 0x4800
bx r5
.word 0x12345678
.arm
.balign 4
2:
mrs r2,cpsr
and r2,r2,#0xf0000000
mov r0,r0
cmp sp,r7
orrne r2,r2,#1
str r0,[r4]
str r2,[r4,#4]
msr cpsr_f,r6
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_pc_literal_ldr_a0_o0_r1_v00000000
.type cpu_arm7_thumb_pc_literal_ldr_a0_o0_r1_v00000000, %function
cpu_arm7_thumb_pc_literal_ldr_a0_o0_r1_v00000000:
push {r4-r8,lr}
mov r4,r3
mrs r6,cpsr
mov r7,sp
ldr r0,=0xffffffff
mov r1,r0
ldr r8,=cpu_arm7_thumb_pc_literal_ldr_a0_o0_r1_v00000000_instruction
add r8,r8,#4
bic r8,r8,#3
ldr r5,=2f
ldr r3,=cpu_arm7_thumb_pc_literal_ldr_a0_o0_r1_v00000000_instruction+1
msr cpsr_f,r2
bx r3
.thumb
.balign 4
.global cpu_arm7_thumb_pc_literal_ldr_a0_o0_r1_v00000000_instruction
cpu_arm7_thumb_pc_literal_ldr_a0_o0_r1_v00000000_instruction:
.hword 0x4900
bx r5
.word 0x00000000
.arm
.balign 4
2:
mrs r2,cpsr
and r2,r2,#0xf0000000
mov r0,r1
cmp sp,r7
orrne r2,r2,#1
str r0,[r4]
str r2,[r4,#4]
msr cpsr_f,r6
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_pc_literal_ldr_a0_o0_r1_vffffffff
.type cpu_arm7_thumb_pc_literal_ldr_a0_o0_r1_vffffffff, %function
cpu_arm7_thumb_pc_literal_ldr_a0_o0_r1_vffffffff:
push {r4-r8,lr}
mov r4,r3
mrs r6,cpsr
mov r7,sp
ldr r0,=0x00000000
mov r1,r0
ldr r8,=cpu_arm7_thumb_pc_literal_ldr_a0_o0_r1_vffffffff_instruction
add r8,r8,#4
bic r8,r8,#3
ldr r5,=2f
ldr r3,=cpu_arm7_thumb_pc_literal_ldr_a0_o0_r1_vffffffff_instruction+1
msr cpsr_f,r2
bx r3
.thumb
.balign 4
.global cpu_arm7_thumb_pc_literal_ldr_a0_o0_r1_vffffffff_instruction
cpu_arm7_thumb_pc_literal_ldr_a0_o0_r1_vffffffff_instruction:
.hword 0x4900
bx r5
.word 0xffffffff
.arm
.balign 4
2:
mrs r2,cpsr
and r2,r2,#0xf0000000
mov r0,r1
cmp sp,r7
orrne r2,r2,#1
str r0,[r4]
str r2,[r4,#4]
msr cpsr_f,r6
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_pc_literal_ldr_a0_o0_r1_v80000001
.type cpu_arm7_thumb_pc_literal_ldr_a0_o0_r1_v80000001, %function
cpu_arm7_thumb_pc_literal_ldr_a0_o0_r1_v80000001:
push {r4-r8,lr}
mov r4,r3
mrs r6,cpsr
mov r7,sp
ldr r0,=0x7ffffffe
mov r1,r0
ldr r8,=cpu_arm7_thumb_pc_literal_ldr_a0_o0_r1_v80000001_instruction
add r8,r8,#4
bic r8,r8,#3
ldr r5,=2f
ldr r3,=cpu_arm7_thumb_pc_literal_ldr_a0_o0_r1_v80000001_instruction+1
msr cpsr_f,r2
bx r3
.thumb
.balign 4
.global cpu_arm7_thumb_pc_literal_ldr_a0_o0_r1_v80000001_instruction
cpu_arm7_thumb_pc_literal_ldr_a0_o0_r1_v80000001_instruction:
.hword 0x4900
bx r5
.word 0x80000001
.arm
.balign 4
2:
mrs r2,cpsr
and r2,r2,#0xf0000000
mov r0,r1
cmp sp,r7
orrne r2,r2,#1
str r0,[r4]
str r2,[r4,#4]
msr cpsr_f,r6
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_pc_literal_ldr_a0_o0_r1_v12345678
.type cpu_arm7_thumb_pc_literal_ldr_a0_o0_r1_v12345678, %function
cpu_arm7_thumb_pc_literal_ldr_a0_o0_r1_v12345678:
push {r4-r8,lr}
mov r4,r3
mrs r6,cpsr
mov r7,sp
ldr r0,=0xedcba987
mov r1,r0
ldr r8,=cpu_arm7_thumb_pc_literal_ldr_a0_o0_r1_v12345678_instruction
add r8,r8,#4
bic r8,r8,#3
ldr r5,=2f
ldr r3,=cpu_arm7_thumb_pc_literal_ldr_a0_o0_r1_v12345678_instruction+1
msr cpsr_f,r2
bx r3
.thumb
.balign 4
.global cpu_arm7_thumb_pc_literal_ldr_a0_o0_r1_v12345678_instruction
cpu_arm7_thumb_pc_literal_ldr_a0_o0_r1_v12345678_instruction:
.hword 0x4900
bx r5
.word 0x12345678
.arm
.balign 4
2:
mrs r2,cpsr
and r2,r2,#0xf0000000
mov r0,r1
cmp sp,r7
orrne r2,r2,#1
str r0,[r4]
str r2,[r4,#4]
msr cpsr_f,r6
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_pc_literal_ldr_a0_o4_r0_v00000000
.type cpu_arm7_thumb_pc_literal_ldr_a0_o4_r0_v00000000, %function
cpu_arm7_thumb_pc_literal_ldr_a0_o4_r0_v00000000:
push {r4-r8,lr}
mov r4,r3
mrs r6,cpsr
mov r7,sp
ldr r0,=0xffffffff
mov r1,r0
ldr r8,=cpu_arm7_thumb_pc_literal_ldr_a0_o4_r0_v00000000_instruction
add r8,r8,#4
bic r8,r8,#3
ldr r5,=2f
ldr r3,=cpu_arm7_thumb_pc_literal_ldr_a0_o4_r0_v00000000_instruction+1
msr cpsr_f,r2
bx r3
.thumb
.balign 4
.global cpu_arm7_thumb_pc_literal_ldr_a0_o4_r0_v00000000_instruction
cpu_arm7_thumb_pc_literal_ldr_a0_o4_r0_v00000000_instruction:
.hword 0x4801
bx r5
.space 4,0
.word 0x00000000
.arm
.balign 4
2:
mrs r2,cpsr
and r2,r2,#0xf0000000
mov r0,r0
cmp sp,r7
orrne r2,r2,#1
str r0,[r4]
str r2,[r4,#4]
msr cpsr_f,r6
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_pc_literal_ldr_a0_o4_r0_vffffffff
.type cpu_arm7_thumb_pc_literal_ldr_a0_o4_r0_vffffffff, %function
cpu_arm7_thumb_pc_literal_ldr_a0_o4_r0_vffffffff:
push {r4-r8,lr}
mov r4,r3
mrs r6,cpsr
mov r7,sp
ldr r0,=0x00000000
mov r1,r0
ldr r8,=cpu_arm7_thumb_pc_literal_ldr_a0_o4_r0_vffffffff_instruction
add r8,r8,#4
bic r8,r8,#3
ldr r5,=2f
ldr r3,=cpu_arm7_thumb_pc_literal_ldr_a0_o4_r0_vffffffff_instruction+1
msr cpsr_f,r2
bx r3
.thumb
.balign 4
.global cpu_arm7_thumb_pc_literal_ldr_a0_o4_r0_vffffffff_instruction
cpu_arm7_thumb_pc_literal_ldr_a0_o4_r0_vffffffff_instruction:
.hword 0x4801
bx r5
.space 4,0
.word 0xffffffff
.arm
.balign 4
2:
mrs r2,cpsr
and r2,r2,#0xf0000000
mov r0,r0
cmp sp,r7
orrne r2,r2,#1
str r0,[r4]
str r2,[r4,#4]
msr cpsr_f,r6
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_pc_literal_ldr_a0_o4_r0_v80000001
.type cpu_arm7_thumb_pc_literal_ldr_a0_o4_r0_v80000001, %function
cpu_arm7_thumb_pc_literal_ldr_a0_o4_r0_v80000001:
push {r4-r8,lr}
mov r4,r3
mrs r6,cpsr
mov r7,sp
ldr r0,=0x7ffffffe
mov r1,r0
ldr r8,=cpu_arm7_thumb_pc_literal_ldr_a0_o4_r0_v80000001_instruction
add r8,r8,#4
bic r8,r8,#3
ldr r5,=2f
ldr r3,=cpu_arm7_thumb_pc_literal_ldr_a0_o4_r0_v80000001_instruction+1
msr cpsr_f,r2
bx r3
.thumb
.balign 4
.global cpu_arm7_thumb_pc_literal_ldr_a0_o4_r0_v80000001_instruction
cpu_arm7_thumb_pc_literal_ldr_a0_o4_r0_v80000001_instruction:
.hword 0x4801
bx r5
.space 4,0
.word 0x80000001
.arm
.balign 4
2:
mrs r2,cpsr
and r2,r2,#0xf0000000
mov r0,r0
cmp sp,r7
orrne r2,r2,#1
str r0,[r4]
str r2,[r4,#4]
msr cpsr_f,r6
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_pc_literal_ldr_a0_o4_r0_v12345678
.type cpu_arm7_thumb_pc_literal_ldr_a0_o4_r0_v12345678, %function
cpu_arm7_thumb_pc_literal_ldr_a0_o4_r0_v12345678:
push {r4-r8,lr}
mov r4,r3
mrs r6,cpsr
mov r7,sp
ldr r0,=0xedcba987
mov r1,r0
ldr r8,=cpu_arm7_thumb_pc_literal_ldr_a0_o4_r0_v12345678_instruction
add r8,r8,#4
bic r8,r8,#3
ldr r5,=2f
ldr r3,=cpu_arm7_thumb_pc_literal_ldr_a0_o4_r0_v12345678_instruction+1
msr cpsr_f,r2
bx r3
.thumb
.balign 4
.global cpu_arm7_thumb_pc_literal_ldr_a0_o4_r0_v12345678_instruction
cpu_arm7_thumb_pc_literal_ldr_a0_o4_r0_v12345678_instruction:
.hword 0x4801
bx r5
.space 4,0
.word 0x12345678
.arm
.balign 4
2:
mrs r2,cpsr
and r2,r2,#0xf0000000
mov r0,r0
cmp sp,r7
orrne r2,r2,#1
str r0,[r4]
str r2,[r4,#4]
msr cpsr_f,r6
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_pc_literal_ldr_a0_o4_r1_v00000000
.type cpu_arm7_thumb_pc_literal_ldr_a0_o4_r1_v00000000, %function
cpu_arm7_thumb_pc_literal_ldr_a0_o4_r1_v00000000:
push {r4-r8,lr}
mov r4,r3
mrs r6,cpsr
mov r7,sp
ldr r0,=0xffffffff
mov r1,r0
ldr r8,=cpu_arm7_thumb_pc_literal_ldr_a0_o4_r1_v00000000_instruction
add r8,r8,#4
bic r8,r8,#3
ldr r5,=2f
ldr r3,=cpu_arm7_thumb_pc_literal_ldr_a0_o4_r1_v00000000_instruction+1
msr cpsr_f,r2
bx r3
.thumb
.balign 4
.global cpu_arm7_thumb_pc_literal_ldr_a0_o4_r1_v00000000_instruction
cpu_arm7_thumb_pc_literal_ldr_a0_o4_r1_v00000000_instruction:
.hword 0x4901
bx r5
.space 4,0
.word 0x00000000
.arm
.balign 4
2:
mrs r2,cpsr
and r2,r2,#0xf0000000
mov r0,r1
cmp sp,r7
orrne r2,r2,#1
str r0,[r4]
str r2,[r4,#4]
msr cpsr_f,r6
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_pc_literal_ldr_a0_o4_r1_vffffffff
.type cpu_arm7_thumb_pc_literal_ldr_a0_o4_r1_vffffffff, %function
cpu_arm7_thumb_pc_literal_ldr_a0_o4_r1_vffffffff:
push {r4-r8,lr}
mov r4,r3
mrs r6,cpsr
mov r7,sp
ldr r0,=0x00000000
mov r1,r0
ldr r8,=cpu_arm7_thumb_pc_literal_ldr_a0_o4_r1_vffffffff_instruction
add r8,r8,#4
bic r8,r8,#3
ldr r5,=2f
ldr r3,=cpu_arm7_thumb_pc_literal_ldr_a0_o4_r1_vffffffff_instruction+1
msr cpsr_f,r2
bx r3
.thumb
.balign 4
.global cpu_arm7_thumb_pc_literal_ldr_a0_o4_r1_vffffffff_instruction
cpu_arm7_thumb_pc_literal_ldr_a0_o4_r1_vffffffff_instruction:
.hword 0x4901
bx r5
.space 4,0
.word 0xffffffff
.arm
.balign 4
2:
mrs r2,cpsr
and r2,r2,#0xf0000000
mov r0,r1
cmp sp,r7
orrne r2,r2,#1
str r0,[r4]
str r2,[r4,#4]
msr cpsr_f,r6
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_pc_literal_ldr_a0_o4_r1_v80000001
.type cpu_arm7_thumb_pc_literal_ldr_a0_o4_r1_v80000001, %function
cpu_arm7_thumb_pc_literal_ldr_a0_o4_r1_v80000001:
push {r4-r8,lr}
mov r4,r3
mrs r6,cpsr
mov r7,sp
ldr r0,=0x7ffffffe
mov r1,r0
ldr r8,=cpu_arm7_thumb_pc_literal_ldr_a0_o4_r1_v80000001_instruction
add r8,r8,#4
bic r8,r8,#3
ldr r5,=2f
ldr r3,=cpu_arm7_thumb_pc_literal_ldr_a0_o4_r1_v80000001_instruction+1
msr cpsr_f,r2
bx r3
.thumb
.balign 4
.global cpu_arm7_thumb_pc_literal_ldr_a0_o4_r1_v80000001_instruction
cpu_arm7_thumb_pc_literal_ldr_a0_o4_r1_v80000001_instruction:
.hword 0x4901
bx r5
.space 4,0
.word 0x80000001
.arm
.balign 4
2:
mrs r2,cpsr
and r2,r2,#0xf0000000
mov r0,r1
cmp sp,r7
orrne r2,r2,#1
str r0,[r4]
str r2,[r4,#4]
msr cpsr_f,r6
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_pc_literal_ldr_a0_o4_r1_v12345678
.type cpu_arm7_thumb_pc_literal_ldr_a0_o4_r1_v12345678, %function
cpu_arm7_thumb_pc_literal_ldr_a0_o4_r1_v12345678:
push {r4-r8,lr}
mov r4,r3
mrs r6,cpsr
mov r7,sp
ldr r0,=0xedcba987
mov r1,r0
ldr r8,=cpu_arm7_thumb_pc_literal_ldr_a0_o4_r1_v12345678_instruction
add r8,r8,#4
bic r8,r8,#3
ldr r5,=2f
ldr r3,=cpu_arm7_thumb_pc_literal_ldr_a0_o4_r1_v12345678_instruction+1
msr cpsr_f,r2
bx r3
.thumb
.balign 4
.global cpu_arm7_thumb_pc_literal_ldr_a0_o4_r1_v12345678_instruction
cpu_arm7_thumb_pc_literal_ldr_a0_o4_r1_v12345678_instruction:
.hword 0x4901
bx r5
.space 4,0
.word 0x12345678
.arm
.balign 4
2:
mrs r2,cpsr
and r2,r2,#0xf0000000
mov r0,r1
cmp sp,r7
orrne r2,r2,#1
str r0,[r4]
str r2,[r4,#4]
msr cpsr_f,r6
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_pc_literal_ldr_a0_o1020_r0_v00000000
.type cpu_arm7_thumb_pc_literal_ldr_a0_o1020_r0_v00000000, %function
cpu_arm7_thumb_pc_literal_ldr_a0_o1020_r0_v00000000:
push {r4-r8,lr}
mov r4,r3
mrs r6,cpsr
mov r7,sp
ldr r0,=0xffffffff
mov r1,r0
ldr r8,=cpu_arm7_thumb_pc_literal_ldr_a0_o1020_r0_v00000000_instruction
add r8,r8,#4
bic r8,r8,#3
ldr r5,=2f
ldr r3,=cpu_arm7_thumb_pc_literal_ldr_a0_o1020_r0_v00000000_instruction+1
msr cpsr_f,r2
bx r3
.thumb
.balign 4
.global cpu_arm7_thumb_pc_literal_ldr_a0_o1020_r0_v00000000_instruction
cpu_arm7_thumb_pc_literal_ldr_a0_o1020_r0_v00000000_instruction:
.hword 0x48ff
bx r5
.space 1020,0
.word 0x00000000
.arm
.balign 4
2:
mrs r2,cpsr
and r2,r2,#0xf0000000
mov r0,r0
cmp sp,r7
orrne r2,r2,#1
str r0,[r4]
str r2,[r4,#4]
msr cpsr_f,r6
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_pc_literal_ldr_a0_o1020_r0_vffffffff
.type cpu_arm7_thumb_pc_literal_ldr_a0_o1020_r0_vffffffff, %function
cpu_arm7_thumb_pc_literal_ldr_a0_o1020_r0_vffffffff:
push {r4-r8,lr}
mov r4,r3
mrs r6,cpsr
mov r7,sp
ldr r0,=0x00000000
mov r1,r0
ldr r8,=cpu_arm7_thumb_pc_literal_ldr_a0_o1020_r0_vffffffff_instruction
add r8,r8,#4
bic r8,r8,#3
ldr r5,=2f
ldr r3,=cpu_arm7_thumb_pc_literal_ldr_a0_o1020_r0_vffffffff_instruction+1
msr cpsr_f,r2
bx r3
.thumb
.balign 4
.global cpu_arm7_thumb_pc_literal_ldr_a0_o1020_r0_vffffffff_instruction
cpu_arm7_thumb_pc_literal_ldr_a0_o1020_r0_vffffffff_instruction:
.hword 0x48ff
bx r5
.space 1020,0
.word 0xffffffff
.arm
.balign 4
2:
mrs r2,cpsr
and r2,r2,#0xf0000000
mov r0,r0
cmp sp,r7
orrne r2,r2,#1
str r0,[r4]
str r2,[r4,#4]
msr cpsr_f,r6
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_pc_literal_ldr_a0_o1020_r0_v80000001
.type cpu_arm7_thumb_pc_literal_ldr_a0_o1020_r0_v80000001, %function
cpu_arm7_thumb_pc_literal_ldr_a0_o1020_r0_v80000001:
push {r4-r8,lr}
mov r4,r3
mrs r6,cpsr
mov r7,sp
ldr r0,=0x7ffffffe
mov r1,r0
ldr r8,=cpu_arm7_thumb_pc_literal_ldr_a0_o1020_r0_v80000001_instruction
add r8,r8,#4
bic r8,r8,#3
ldr r5,=2f
ldr r3,=cpu_arm7_thumb_pc_literal_ldr_a0_o1020_r0_v80000001_instruction+1
msr cpsr_f,r2
bx r3
.thumb
.balign 4
.global cpu_arm7_thumb_pc_literal_ldr_a0_o1020_r0_v80000001_instruction
cpu_arm7_thumb_pc_literal_ldr_a0_o1020_r0_v80000001_instruction:
.hword 0x48ff
bx r5
.space 1020,0
.word 0x80000001
.arm
.balign 4
2:
mrs r2,cpsr
and r2,r2,#0xf0000000
mov r0,r0
cmp sp,r7
orrne r2,r2,#1
str r0,[r4]
str r2,[r4,#4]
msr cpsr_f,r6
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_pc_literal_ldr_a0_o1020_r0_v12345678
.type cpu_arm7_thumb_pc_literal_ldr_a0_o1020_r0_v12345678, %function
cpu_arm7_thumb_pc_literal_ldr_a0_o1020_r0_v12345678:
push {r4-r8,lr}
mov r4,r3
mrs r6,cpsr
mov r7,sp
ldr r0,=0xedcba987
mov r1,r0
ldr r8,=cpu_arm7_thumb_pc_literal_ldr_a0_o1020_r0_v12345678_instruction
add r8,r8,#4
bic r8,r8,#3
ldr r5,=2f
ldr r3,=cpu_arm7_thumb_pc_literal_ldr_a0_o1020_r0_v12345678_instruction+1
msr cpsr_f,r2
bx r3
.thumb
.balign 4
.global cpu_arm7_thumb_pc_literal_ldr_a0_o1020_r0_v12345678_instruction
cpu_arm7_thumb_pc_literal_ldr_a0_o1020_r0_v12345678_instruction:
.hword 0x48ff
bx r5
.space 1020,0
.word 0x12345678
.arm
.balign 4
2:
mrs r2,cpsr
and r2,r2,#0xf0000000
mov r0,r0
cmp sp,r7
orrne r2,r2,#1
str r0,[r4]
str r2,[r4,#4]
msr cpsr_f,r6
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_pc_literal_ldr_a0_o1020_r1_v00000000
.type cpu_arm7_thumb_pc_literal_ldr_a0_o1020_r1_v00000000, %function
cpu_arm7_thumb_pc_literal_ldr_a0_o1020_r1_v00000000:
push {r4-r8,lr}
mov r4,r3
mrs r6,cpsr
mov r7,sp
ldr r0,=0xffffffff
mov r1,r0
ldr r8,=cpu_arm7_thumb_pc_literal_ldr_a0_o1020_r1_v00000000_instruction
add r8,r8,#4
bic r8,r8,#3
ldr r5,=2f
ldr r3,=cpu_arm7_thumb_pc_literal_ldr_a0_o1020_r1_v00000000_instruction+1
msr cpsr_f,r2
bx r3
.thumb
.balign 4
.global cpu_arm7_thumb_pc_literal_ldr_a0_o1020_r1_v00000000_instruction
cpu_arm7_thumb_pc_literal_ldr_a0_o1020_r1_v00000000_instruction:
.hword 0x49ff
bx r5
.space 1020,0
.word 0x00000000
.arm
.balign 4
2:
mrs r2,cpsr
and r2,r2,#0xf0000000
mov r0,r1
cmp sp,r7
orrne r2,r2,#1
str r0,[r4]
str r2,[r4,#4]
msr cpsr_f,r6
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_pc_literal_ldr_a0_o1020_r1_vffffffff
.type cpu_arm7_thumb_pc_literal_ldr_a0_o1020_r1_vffffffff, %function
cpu_arm7_thumb_pc_literal_ldr_a0_o1020_r1_vffffffff:
push {r4-r8,lr}
mov r4,r3
mrs r6,cpsr
mov r7,sp
ldr r0,=0x00000000
mov r1,r0
ldr r8,=cpu_arm7_thumb_pc_literal_ldr_a0_o1020_r1_vffffffff_instruction
add r8,r8,#4
bic r8,r8,#3
ldr r5,=2f
ldr r3,=cpu_arm7_thumb_pc_literal_ldr_a0_o1020_r1_vffffffff_instruction+1
msr cpsr_f,r2
bx r3
.thumb
.balign 4
.global cpu_arm7_thumb_pc_literal_ldr_a0_o1020_r1_vffffffff_instruction
cpu_arm7_thumb_pc_literal_ldr_a0_o1020_r1_vffffffff_instruction:
.hword 0x49ff
bx r5
.space 1020,0
.word 0xffffffff
.arm
.balign 4
2:
mrs r2,cpsr
and r2,r2,#0xf0000000
mov r0,r1
cmp sp,r7
orrne r2,r2,#1
str r0,[r4]
str r2,[r4,#4]
msr cpsr_f,r6
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_pc_literal_ldr_a0_o1020_r1_v80000001
.type cpu_arm7_thumb_pc_literal_ldr_a0_o1020_r1_v80000001, %function
cpu_arm7_thumb_pc_literal_ldr_a0_o1020_r1_v80000001:
push {r4-r8,lr}
mov r4,r3
mrs r6,cpsr
mov r7,sp
ldr r0,=0x7ffffffe
mov r1,r0
ldr r8,=cpu_arm7_thumb_pc_literal_ldr_a0_o1020_r1_v80000001_instruction
add r8,r8,#4
bic r8,r8,#3
ldr r5,=2f
ldr r3,=cpu_arm7_thumb_pc_literal_ldr_a0_o1020_r1_v80000001_instruction+1
msr cpsr_f,r2
bx r3
.thumb
.balign 4
.global cpu_arm7_thumb_pc_literal_ldr_a0_o1020_r1_v80000001_instruction
cpu_arm7_thumb_pc_literal_ldr_a0_o1020_r1_v80000001_instruction:
.hword 0x49ff
bx r5
.space 1020,0
.word 0x80000001
.arm
.balign 4
2:
mrs r2,cpsr
and r2,r2,#0xf0000000
mov r0,r1
cmp sp,r7
orrne r2,r2,#1
str r0,[r4]
str r2,[r4,#4]
msr cpsr_f,r6
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_pc_literal_ldr_a0_o1020_r1_v12345678
.type cpu_arm7_thumb_pc_literal_ldr_a0_o1020_r1_v12345678, %function
cpu_arm7_thumb_pc_literal_ldr_a0_o1020_r1_v12345678:
push {r4-r8,lr}
mov r4,r3
mrs r6,cpsr
mov r7,sp
ldr r0,=0xedcba987
mov r1,r0
ldr r8,=cpu_arm7_thumb_pc_literal_ldr_a0_o1020_r1_v12345678_instruction
add r8,r8,#4
bic r8,r8,#3
ldr r5,=2f
ldr r3,=cpu_arm7_thumb_pc_literal_ldr_a0_o1020_r1_v12345678_instruction+1
msr cpsr_f,r2
bx r3
.thumb
.balign 4
.global cpu_arm7_thumb_pc_literal_ldr_a0_o1020_r1_v12345678_instruction
cpu_arm7_thumb_pc_literal_ldr_a0_o1020_r1_v12345678_instruction:
.hword 0x49ff
bx r5
.space 1020,0
.word 0x12345678
.arm
.balign 4
2:
mrs r2,cpsr
and r2,r2,#0xf0000000
mov r0,r1
cmp sp,r7
orrne r2,r2,#1
str r0,[r4]
str r2,[r4,#4]
msr cpsr_f,r6
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_pc_literal_ldr_a2_o0_r0_v46c04728
.type cpu_arm7_thumb_pc_literal_ldr_a2_o0_r0_v46c04728, %function
cpu_arm7_thumb_pc_literal_ldr_a2_o0_r0_v46c04728:
push {r4-r8,lr}
mov r4,r3
mrs r6,cpsr
mov r7,sp
ldr r0,=0xb93fb8d7
mov r1,r0
ldr r8,=cpu_arm7_thumb_pc_literal_ldr_a2_o0_r0_v46c04728_instruction
add r8,r8,#4
bic r8,r8,#3
ldr r5,=2f
ldr r3,=cpu_arm7_thumb_pc_literal_ldr_a2_o0_r0_v46c04728_instruction+1
msr cpsr_f,r2
bx r3
.thumb
.balign 4
nop
.global cpu_arm7_thumb_pc_literal_ldr_a2_o0_r0_v46c04728_instruction
cpu_arm7_thumb_pc_literal_ldr_a2_o0_r0_v46c04728_instruction:
.hword 0x4800
bx r5
nop
.arm
.balign 4
2:
mrs r2,cpsr
and r2,r2,#0xf0000000
mov r0,r0
cmp sp,r7
orrne r2,r2,#1
str r0,[r4]
str r2,[r4,#4]
msr cpsr_f,r6
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_pc_literal_ldr_a2_o0_r1_v46c04728
.type cpu_arm7_thumb_pc_literal_ldr_a2_o0_r1_v46c04728, %function
cpu_arm7_thumb_pc_literal_ldr_a2_o0_r1_v46c04728:
push {r4-r8,lr}
mov r4,r3
mrs r6,cpsr
mov r7,sp
ldr r0,=0xb93fb8d7
mov r1,r0
ldr r8,=cpu_arm7_thumb_pc_literal_ldr_a2_o0_r1_v46c04728_instruction
add r8,r8,#4
bic r8,r8,#3
ldr r5,=2f
ldr r3,=cpu_arm7_thumb_pc_literal_ldr_a2_o0_r1_v46c04728_instruction+1
msr cpsr_f,r2
bx r3
.thumb
.balign 4
nop
.global cpu_arm7_thumb_pc_literal_ldr_a2_o0_r1_v46c04728_instruction
cpu_arm7_thumb_pc_literal_ldr_a2_o0_r1_v46c04728_instruction:
.hword 0x4900
bx r5
nop
.arm
.balign 4
2:
mrs r2,cpsr
and r2,r2,#0xf0000000
mov r0,r1
cmp sp,r7
orrne r2,r2,#1
str r0,[r4]
str r2,[r4,#4]
msr cpsr_f,r6
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_pc_literal_ldr_a2_o4_r0_v00000000
.type cpu_arm7_thumb_pc_literal_ldr_a2_o4_r0_v00000000, %function
cpu_arm7_thumb_pc_literal_ldr_a2_o4_r0_v00000000:
push {r4-r8,lr}
mov r4,r3
mrs r6,cpsr
mov r7,sp
ldr r0,=0xffffffff
mov r1,r0
ldr r8,=cpu_arm7_thumb_pc_literal_ldr_a2_o4_r0_v00000000_instruction
add r8,r8,#4
bic r8,r8,#3
ldr r5,=2f
ldr r3,=cpu_arm7_thumb_pc_literal_ldr_a2_o4_r0_v00000000_instruction+1
msr cpsr_f,r2
bx r3
.thumb
.balign 4
nop
.global cpu_arm7_thumb_pc_literal_ldr_a2_o4_r0_v00000000_instruction
cpu_arm7_thumb_pc_literal_ldr_a2_o4_r0_v00000000_instruction:
.hword 0x4801
bx r5
.space 2,0
.word 0x00000000
.arm
.balign 4
2:
mrs r2,cpsr
and r2,r2,#0xf0000000
mov r0,r0
cmp sp,r7
orrne r2,r2,#1
str r0,[r4]
str r2,[r4,#4]
msr cpsr_f,r6
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_pc_literal_ldr_a2_o4_r0_vffffffff
.type cpu_arm7_thumb_pc_literal_ldr_a2_o4_r0_vffffffff, %function
cpu_arm7_thumb_pc_literal_ldr_a2_o4_r0_vffffffff:
push {r4-r8,lr}
mov r4,r3
mrs r6,cpsr
mov r7,sp
ldr r0,=0x00000000
mov r1,r0
ldr r8,=cpu_arm7_thumb_pc_literal_ldr_a2_o4_r0_vffffffff_instruction
add r8,r8,#4
bic r8,r8,#3
ldr r5,=2f
ldr r3,=cpu_arm7_thumb_pc_literal_ldr_a2_o4_r0_vffffffff_instruction+1
msr cpsr_f,r2
bx r3
.thumb
.balign 4
nop
.global cpu_arm7_thumb_pc_literal_ldr_a2_o4_r0_vffffffff_instruction
cpu_arm7_thumb_pc_literal_ldr_a2_o4_r0_vffffffff_instruction:
.hword 0x4801
bx r5
.space 2,0
.word 0xffffffff
.arm
.balign 4
2:
mrs r2,cpsr
and r2,r2,#0xf0000000
mov r0,r0
cmp sp,r7
orrne r2,r2,#1
str r0,[r4]
str r2,[r4,#4]
msr cpsr_f,r6
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_pc_literal_ldr_a2_o4_r0_v80000001
.type cpu_arm7_thumb_pc_literal_ldr_a2_o4_r0_v80000001, %function
cpu_arm7_thumb_pc_literal_ldr_a2_o4_r0_v80000001:
push {r4-r8,lr}
mov r4,r3
mrs r6,cpsr
mov r7,sp
ldr r0,=0x7ffffffe
mov r1,r0
ldr r8,=cpu_arm7_thumb_pc_literal_ldr_a2_o4_r0_v80000001_instruction
add r8,r8,#4
bic r8,r8,#3
ldr r5,=2f
ldr r3,=cpu_arm7_thumb_pc_literal_ldr_a2_o4_r0_v80000001_instruction+1
msr cpsr_f,r2
bx r3
.thumb
.balign 4
nop
.global cpu_arm7_thumb_pc_literal_ldr_a2_o4_r0_v80000001_instruction
cpu_arm7_thumb_pc_literal_ldr_a2_o4_r0_v80000001_instruction:
.hword 0x4801
bx r5
.space 2,0
.word 0x80000001
.arm
.balign 4
2:
mrs r2,cpsr
and r2,r2,#0xf0000000
mov r0,r0
cmp sp,r7
orrne r2,r2,#1
str r0,[r4]
str r2,[r4,#4]
msr cpsr_f,r6
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_pc_literal_ldr_a2_o4_r0_v12345678
.type cpu_arm7_thumb_pc_literal_ldr_a2_o4_r0_v12345678, %function
cpu_arm7_thumb_pc_literal_ldr_a2_o4_r0_v12345678:
push {r4-r8,lr}
mov r4,r3
mrs r6,cpsr
mov r7,sp
ldr r0,=0xedcba987
mov r1,r0
ldr r8,=cpu_arm7_thumb_pc_literal_ldr_a2_o4_r0_v12345678_instruction
add r8,r8,#4
bic r8,r8,#3
ldr r5,=2f
ldr r3,=cpu_arm7_thumb_pc_literal_ldr_a2_o4_r0_v12345678_instruction+1
msr cpsr_f,r2
bx r3
.thumb
.balign 4
nop
.global cpu_arm7_thumb_pc_literal_ldr_a2_o4_r0_v12345678_instruction
cpu_arm7_thumb_pc_literal_ldr_a2_o4_r0_v12345678_instruction:
.hword 0x4801
bx r5
.space 2,0
.word 0x12345678
.arm
.balign 4
2:
mrs r2,cpsr
and r2,r2,#0xf0000000
mov r0,r0
cmp sp,r7
orrne r2,r2,#1
str r0,[r4]
str r2,[r4,#4]
msr cpsr_f,r6
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_pc_literal_ldr_a2_o4_r1_v00000000
.type cpu_arm7_thumb_pc_literal_ldr_a2_o4_r1_v00000000, %function
cpu_arm7_thumb_pc_literal_ldr_a2_o4_r1_v00000000:
push {r4-r8,lr}
mov r4,r3
mrs r6,cpsr
mov r7,sp
ldr r0,=0xffffffff
mov r1,r0
ldr r8,=cpu_arm7_thumb_pc_literal_ldr_a2_o4_r1_v00000000_instruction
add r8,r8,#4
bic r8,r8,#3
ldr r5,=2f
ldr r3,=cpu_arm7_thumb_pc_literal_ldr_a2_o4_r1_v00000000_instruction+1
msr cpsr_f,r2
bx r3
.thumb
.balign 4
nop
.global cpu_arm7_thumb_pc_literal_ldr_a2_o4_r1_v00000000_instruction
cpu_arm7_thumb_pc_literal_ldr_a2_o4_r1_v00000000_instruction:
.hword 0x4901
bx r5
.space 2,0
.word 0x00000000
.arm
.balign 4
2:
mrs r2,cpsr
and r2,r2,#0xf0000000
mov r0,r1
cmp sp,r7
orrne r2,r2,#1
str r0,[r4]
str r2,[r4,#4]
msr cpsr_f,r6
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_pc_literal_ldr_a2_o4_r1_vffffffff
.type cpu_arm7_thumb_pc_literal_ldr_a2_o4_r1_vffffffff, %function
cpu_arm7_thumb_pc_literal_ldr_a2_o4_r1_vffffffff:
push {r4-r8,lr}
mov r4,r3
mrs r6,cpsr
mov r7,sp
ldr r0,=0x00000000
mov r1,r0
ldr r8,=cpu_arm7_thumb_pc_literal_ldr_a2_o4_r1_vffffffff_instruction
add r8,r8,#4
bic r8,r8,#3
ldr r5,=2f
ldr r3,=cpu_arm7_thumb_pc_literal_ldr_a2_o4_r1_vffffffff_instruction+1
msr cpsr_f,r2
bx r3
.thumb
.balign 4
nop
.global cpu_arm7_thumb_pc_literal_ldr_a2_o4_r1_vffffffff_instruction
cpu_arm7_thumb_pc_literal_ldr_a2_o4_r1_vffffffff_instruction:
.hword 0x4901
bx r5
.space 2,0
.word 0xffffffff
.arm
.balign 4
2:
mrs r2,cpsr
and r2,r2,#0xf0000000
mov r0,r1
cmp sp,r7
orrne r2,r2,#1
str r0,[r4]
str r2,[r4,#4]
msr cpsr_f,r6
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_pc_literal_ldr_a2_o4_r1_v80000001
.type cpu_arm7_thumb_pc_literal_ldr_a2_o4_r1_v80000001, %function
cpu_arm7_thumb_pc_literal_ldr_a2_o4_r1_v80000001:
push {r4-r8,lr}
mov r4,r3
mrs r6,cpsr
mov r7,sp
ldr r0,=0x7ffffffe
mov r1,r0
ldr r8,=cpu_arm7_thumb_pc_literal_ldr_a2_o4_r1_v80000001_instruction
add r8,r8,#4
bic r8,r8,#3
ldr r5,=2f
ldr r3,=cpu_arm7_thumb_pc_literal_ldr_a2_o4_r1_v80000001_instruction+1
msr cpsr_f,r2
bx r3
.thumb
.balign 4
nop
.global cpu_arm7_thumb_pc_literal_ldr_a2_o4_r1_v80000001_instruction
cpu_arm7_thumb_pc_literal_ldr_a2_o4_r1_v80000001_instruction:
.hword 0x4901
bx r5
.space 2,0
.word 0x80000001
.arm
.balign 4
2:
mrs r2,cpsr
and r2,r2,#0xf0000000
mov r0,r1
cmp sp,r7
orrne r2,r2,#1
str r0,[r4]
str r2,[r4,#4]
msr cpsr_f,r6
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_pc_literal_ldr_a2_o4_r1_v12345678
.type cpu_arm7_thumb_pc_literal_ldr_a2_o4_r1_v12345678, %function
cpu_arm7_thumb_pc_literal_ldr_a2_o4_r1_v12345678:
push {r4-r8,lr}
mov r4,r3
mrs r6,cpsr
mov r7,sp
ldr r0,=0xedcba987
mov r1,r0
ldr r8,=cpu_arm7_thumb_pc_literal_ldr_a2_o4_r1_v12345678_instruction
add r8,r8,#4
bic r8,r8,#3
ldr r5,=2f
ldr r3,=cpu_arm7_thumb_pc_literal_ldr_a2_o4_r1_v12345678_instruction+1
msr cpsr_f,r2
bx r3
.thumb
.balign 4
nop
.global cpu_arm7_thumb_pc_literal_ldr_a2_o4_r1_v12345678_instruction
cpu_arm7_thumb_pc_literal_ldr_a2_o4_r1_v12345678_instruction:
.hword 0x4901
bx r5
.space 2,0
.word 0x12345678
.arm
.balign 4
2:
mrs r2,cpsr
and r2,r2,#0xf0000000
mov r0,r1
cmp sp,r7
orrne r2,r2,#1
str r0,[r4]
str r2,[r4,#4]
msr cpsr_f,r6
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_pc_literal_ldr_a2_o1020_r0_v00000000
.type cpu_arm7_thumb_pc_literal_ldr_a2_o1020_r0_v00000000, %function
cpu_arm7_thumb_pc_literal_ldr_a2_o1020_r0_v00000000:
push {r4-r8,lr}
mov r4,r3
mrs r6,cpsr
mov r7,sp
ldr r0,=0xffffffff
mov r1,r0
ldr r8,=cpu_arm7_thumb_pc_literal_ldr_a2_o1020_r0_v00000000_instruction
add r8,r8,#4
bic r8,r8,#3
ldr r5,=2f
ldr r3,=cpu_arm7_thumb_pc_literal_ldr_a2_o1020_r0_v00000000_instruction+1
msr cpsr_f,r2
bx r3
.thumb
.balign 4
nop
.global cpu_arm7_thumb_pc_literal_ldr_a2_o1020_r0_v00000000_instruction
cpu_arm7_thumb_pc_literal_ldr_a2_o1020_r0_v00000000_instruction:
.hword 0x48ff
bx r5
.space 1018,0
.word 0x00000000
.arm
.balign 4
2:
mrs r2,cpsr
and r2,r2,#0xf0000000
mov r0,r0
cmp sp,r7
orrne r2,r2,#1
str r0,[r4]
str r2,[r4,#4]
msr cpsr_f,r6
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_pc_literal_ldr_a2_o1020_r0_vffffffff
.type cpu_arm7_thumb_pc_literal_ldr_a2_o1020_r0_vffffffff, %function
cpu_arm7_thumb_pc_literal_ldr_a2_o1020_r0_vffffffff:
push {r4-r8,lr}
mov r4,r3
mrs r6,cpsr
mov r7,sp
ldr r0,=0x00000000
mov r1,r0
ldr r8,=cpu_arm7_thumb_pc_literal_ldr_a2_o1020_r0_vffffffff_instruction
add r8,r8,#4
bic r8,r8,#3
ldr r5,=2f
ldr r3,=cpu_arm7_thumb_pc_literal_ldr_a2_o1020_r0_vffffffff_instruction+1
msr cpsr_f,r2
bx r3
.thumb
.balign 4
nop
.global cpu_arm7_thumb_pc_literal_ldr_a2_o1020_r0_vffffffff_instruction
cpu_arm7_thumb_pc_literal_ldr_a2_o1020_r0_vffffffff_instruction:
.hword 0x48ff
bx r5
.space 1018,0
.word 0xffffffff
.arm
.balign 4
2:
mrs r2,cpsr
and r2,r2,#0xf0000000
mov r0,r0
cmp sp,r7
orrne r2,r2,#1
str r0,[r4]
str r2,[r4,#4]
msr cpsr_f,r6
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_pc_literal_ldr_a2_o1020_r0_v80000001
.type cpu_arm7_thumb_pc_literal_ldr_a2_o1020_r0_v80000001, %function
cpu_arm7_thumb_pc_literal_ldr_a2_o1020_r0_v80000001:
push {r4-r8,lr}
mov r4,r3
mrs r6,cpsr
mov r7,sp
ldr r0,=0x7ffffffe
mov r1,r0
ldr r8,=cpu_arm7_thumb_pc_literal_ldr_a2_o1020_r0_v80000001_instruction
add r8,r8,#4
bic r8,r8,#3
ldr r5,=2f
ldr r3,=cpu_arm7_thumb_pc_literal_ldr_a2_o1020_r0_v80000001_instruction+1
msr cpsr_f,r2
bx r3
.thumb
.balign 4
nop
.global cpu_arm7_thumb_pc_literal_ldr_a2_o1020_r0_v80000001_instruction
cpu_arm7_thumb_pc_literal_ldr_a2_o1020_r0_v80000001_instruction:
.hword 0x48ff
bx r5
.space 1018,0
.word 0x80000001
.arm
.balign 4
2:
mrs r2,cpsr
and r2,r2,#0xf0000000
mov r0,r0
cmp sp,r7
orrne r2,r2,#1
str r0,[r4]
str r2,[r4,#4]
msr cpsr_f,r6
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_pc_literal_ldr_a2_o1020_r0_v12345678
.type cpu_arm7_thumb_pc_literal_ldr_a2_o1020_r0_v12345678, %function
cpu_arm7_thumb_pc_literal_ldr_a2_o1020_r0_v12345678:
push {r4-r8,lr}
mov r4,r3
mrs r6,cpsr
mov r7,sp
ldr r0,=0xedcba987
mov r1,r0
ldr r8,=cpu_arm7_thumb_pc_literal_ldr_a2_o1020_r0_v12345678_instruction
add r8,r8,#4
bic r8,r8,#3
ldr r5,=2f
ldr r3,=cpu_arm7_thumb_pc_literal_ldr_a2_o1020_r0_v12345678_instruction+1
msr cpsr_f,r2
bx r3
.thumb
.balign 4
nop
.global cpu_arm7_thumb_pc_literal_ldr_a2_o1020_r0_v12345678_instruction
cpu_arm7_thumb_pc_literal_ldr_a2_o1020_r0_v12345678_instruction:
.hword 0x48ff
bx r5
.space 1018,0
.word 0x12345678
.arm
.balign 4
2:
mrs r2,cpsr
and r2,r2,#0xf0000000
mov r0,r0
cmp sp,r7
orrne r2,r2,#1
str r0,[r4]
str r2,[r4,#4]
msr cpsr_f,r6
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_pc_literal_ldr_a2_o1020_r1_v00000000
.type cpu_arm7_thumb_pc_literal_ldr_a2_o1020_r1_v00000000, %function
cpu_arm7_thumb_pc_literal_ldr_a2_o1020_r1_v00000000:
push {r4-r8,lr}
mov r4,r3
mrs r6,cpsr
mov r7,sp
ldr r0,=0xffffffff
mov r1,r0
ldr r8,=cpu_arm7_thumb_pc_literal_ldr_a2_o1020_r1_v00000000_instruction
add r8,r8,#4
bic r8,r8,#3
ldr r5,=2f
ldr r3,=cpu_arm7_thumb_pc_literal_ldr_a2_o1020_r1_v00000000_instruction+1
msr cpsr_f,r2
bx r3
.thumb
.balign 4
nop
.global cpu_arm7_thumb_pc_literal_ldr_a2_o1020_r1_v00000000_instruction
cpu_arm7_thumb_pc_literal_ldr_a2_o1020_r1_v00000000_instruction:
.hword 0x49ff
bx r5
.space 1018,0
.word 0x00000000
.arm
.balign 4
2:
mrs r2,cpsr
and r2,r2,#0xf0000000
mov r0,r1
cmp sp,r7
orrne r2,r2,#1
str r0,[r4]
str r2,[r4,#4]
msr cpsr_f,r6
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_pc_literal_ldr_a2_o1020_r1_vffffffff
.type cpu_arm7_thumb_pc_literal_ldr_a2_o1020_r1_vffffffff, %function
cpu_arm7_thumb_pc_literal_ldr_a2_o1020_r1_vffffffff:
push {r4-r8,lr}
mov r4,r3
mrs r6,cpsr
mov r7,sp
ldr r0,=0x00000000
mov r1,r0
ldr r8,=cpu_arm7_thumb_pc_literal_ldr_a2_o1020_r1_vffffffff_instruction
add r8,r8,#4
bic r8,r8,#3
ldr r5,=2f
ldr r3,=cpu_arm7_thumb_pc_literal_ldr_a2_o1020_r1_vffffffff_instruction+1
msr cpsr_f,r2
bx r3
.thumb
.balign 4
nop
.global cpu_arm7_thumb_pc_literal_ldr_a2_o1020_r1_vffffffff_instruction
cpu_arm7_thumb_pc_literal_ldr_a2_o1020_r1_vffffffff_instruction:
.hword 0x49ff
bx r5
.space 1018,0
.word 0xffffffff
.arm
.balign 4
2:
mrs r2,cpsr
and r2,r2,#0xf0000000
mov r0,r1
cmp sp,r7
orrne r2,r2,#1
str r0,[r4]
str r2,[r4,#4]
msr cpsr_f,r6
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_pc_literal_ldr_a2_o1020_r1_v80000001
.type cpu_arm7_thumb_pc_literal_ldr_a2_o1020_r1_v80000001, %function
cpu_arm7_thumb_pc_literal_ldr_a2_o1020_r1_v80000001:
push {r4-r8,lr}
mov r4,r3
mrs r6,cpsr
mov r7,sp
ldr r0,=0x7ffffffe
mov r1,r0
ldr r8,=cpu_arm7_thumb_pc_literal_ldr_a2_o1020_r1_v80000001_instruction
add r8,r8,#4
bic r8,r8,#3
ldr r5,=2f
ldr r3,=cpu_arm7_thumb_pc_literal_ldr_a2_o1020_r1_v80000001_instruction+1
msr cpsr_f,r2
bx r3
.thumb
.balign 4
nop
.global cpu_arm7_thumb_pc_literal_ldr_a2_o1020_r1_v80000001_instruction
cpu_arm7_thumb_pc_literal_ldr_a2_o1020_r1_v80000001_instruction:
.hword 0x49ff
bx r5
.space 1018,0
.word 0x80000001
.arm
.balign 4
2:
mrs r2,cpsr
and r2,r2,#0xf0000000
mov r0,r1
cmp sp,r7
orrne r2,r2,#1
str r0,[r4]
str r2,[r4,#4]
msr cpsr_f,r6
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.balign 4
.global cpu_arm7_thumb_pc_literal_ldr_a2_o1020_r1_v12345678
.type cpu_arm7_thumb_pc_literal_ldr_a2_o1020_r1_v12345678, %function
cpu_arm7_thumb_pc_literal_ldr_a2_o1020_r1_v12345678:
push {r4-r8,lr}
mov r4,r3
mrs r6,cpsr
mov r7,sp
ldr r0,=0xedcba987
mov r1,r0
ldr r8,=cpu_arm7_thumb_pc_literal_ldr_a2_o1020_r1_v12345678_instruction
add r8,r8,#4
bic r8,r8,#3
ldr r5,=2f
ldr r3,=cpu_arm7_thumb_pc_literal_ldr_a2_o1020_r1_v12345678_instruction+1
msr cpsr_f,r2
bx r3
.thumb
.balign 4
nop
.global cpu_arm7_thumb_pc_literal_ldr_a2_o1020_r1_v12345678_instruction
cpu_arm7_thumb_pc_literal_ldr_a2_o1020_r1_v12345678_instruction:
.hword 0x49ff
bx r5
.space 1018,0
.word 0x12345678
.arm
.balign 4
2:
mrs r2,cpsr
and r2,r2,#0xf0000000
mov r0,r1
cmp sp,r7
orrne r2,r2,#1
str r0,[r4]
str r2,[r4,#4]
msr cpsr_f,r6
pop {r4-r8,lr}
bx lr
.ltorg
