.syntax unified
.arch armv4t
.text
.arm
.align 2
.global cpu_arm_and
.type cpu_arm_and, %function
cpu_arm_and:
push {r4,lr}
mov r4,r3
msr cpsr_f,r2
ands r0,r0,r1
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
pop {r4,lr}
bx lr
.arm
.align 2
.global cpu_arm_eor
.type cpu_arm_eor, %function
cpu_arm_eor:
push {r4,lr}
mov r4,r3
msr cpsr_f,r2
eors r0,r0,r1
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
pop {r4,lr}
bx lr
.arm
.align 2
.global cpu_arm_sub
.type cpu_arm_sub, %function
cpu_arm_sub:
push {r4,lr}
mov r4,r3
msr cpsr_f,r2
subs r0,r0,r1
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
pop {r4,lr}
bx lr
.arm
.align 2
.global cpu_arm_rsb
.type cpu_arm_rsb, %function
cpu_arm_rsb:
push {r4,lr}
mov r4,r3
msr cpsr_f,r2
rsbs r0,r0,r1
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
pop {r4,lr}
bx lr
.arm
.align 2
.global cpu_arm_add
.type cpu_arm_add, %function
cpu_arm_add:
push {r4,lr}
mov r4,r3
msr cpsr_f,r2
adds r0,r0,r1
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
pop {r4,lr}
bx lr
.arm
.align 2
.global cpu_arm_adc
.type cpu_arm_adc, %function
cpu_arm_adc:
push {r4,lr}
mov r4,r3
msr cpsr_f,r2
adcs r0,r0,r1
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
pop {r4,lr}
bx lr
.arm
.align 2
.global cpu_arm_sbc
.type cpu_arm_sbc, %function
cpu_arm_sbc:
push {r4,lr}
mov r4,r3
msr cpsr_f,r2
sbcs r0,r0,r1
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
pop {r4,lr}
bx lr
.arm
.align 2
.global cpu_arm_rsc
.type cpu_arm_rsc, %function
cpu_arm_rsc:
push {r4,lr}
mov r4,r3
msr cpsr_f,r2
rscs r0,r0,r1
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
pop {r4,lr}
bx lr
.arm
.align 2
.global cpu_arm_tst
.type cpu_arm_tst, %function
cpu_arm_tst:
push {r4,lr}
mov r4,r3
msr cpsr_f,r2
tst r0,r1
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
pop {r4,lr}
bx lr
.arm
.align 2
.global cpu_arm_teq
.type cpu_arm_teq, %function
cpu_arm_teq:
push {r4,lr}
mov r4,r3
msr cpsr_f,r2
teq r0,r1
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
pop {r4,lr}
bx lr
.arm
.align 2
.global cpu_arm_cmp
.type cpu_arm_cmp, %function
cpu_arm_cmp:
push {r4,lr}
mov r4,r3
msr cpsr_f,r2
cmp r0,r1
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
pop {r4,lr}
bx lr
.arm
.align 2
.global cpu_arm_cmn
.type cpu_arm_cmn, %function
cpu_arm_cmn:
push {r4,lr}
mov r4,r3
msr cpsr_f,r2
cmn r0,r1
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
pop {r4,lr}
bx lr
.arm
.align 2
.global cpu_arm_orr
.type cpu_arm_orr, %function
cpu_arm_orr:
push {r4,lr}
mov r4,r3
msr cpsr_f,r2
orrs r0,r0,r1
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
pop {r4,lr}
bx lr
.arm
.align 2
.global cpu_arm_mov
.type cpu_arm_mov, %function
cpu_arm_mov:
push {r4,lr}
mov r4,r3
msr cpsr_f,r2
movs r0,r1
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
pop {r4,lr}
bx lr
.arm
.align 2
.global cpu_arm_bic
.type cpu_arm_bic, %function
cpu_arm_bic:
push {r4,lr}
mov r4,r3
msr cpsr_f,r2
bics r0,r0,r1
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
pop {r4,lr}
bx lr
.arm
.align 2
.global cpu_arm_mvn
.type cpu_arm_mvn, %function
cpu_arm_mvn:
push {r4,lr}
mov r4,r3
msr cpsr_f,r2
mvns r0,r1
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
pop {r4,lr}
bx lr
.arm
.align 2
.global cpu_arm_mul
.type cpu_arm_mul, %function
cpu_arm_mul:
push {r4,lr}
mov r4,r3
msr cpsr_f,r2
muls r2,r0,r1
mov r0,r2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
pop {r4,lr}
bx lr
.arm
.align 2
.global cpu_thumb_and
.type cpu_thumb_and, %function
cpu_thumb_and:
push {r4,lr}
mov r4,r3
msr cpsr_f,r2
adr r3,1f+1
bx r3
.thumb
.align 2
1:
ands r0,r1
nop
bx pc
nop
.arm
.align 2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
pop {r4,lr}
bx lr
.arm
.align 2
.global cpu_thumb_eor
.type cpu_thumb_eor, %function
cpu_thumb_eor:
push {r4,lr}
mov r4,r3
msr cpsr_f,r2
adr r3,1f+1
bx r3
.thumb
.align 2
1:
eors r0,r1
nop
bx pc
nop
.arm
.align 2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
pop {r4,lr}
bx lr
.arm
.align 2
.global cpu_thumb_sub
.type cpu_thumb_sub, %function
cpu_thumb_sub:
push {r4,lr}
mov r4,r3
msr cpsr_f,r2
adr r3,1f+1
bx r3
.thumb
.align 2
1:
subs r0,r0,r1
nop
bx pc
nop
.arm
.align 2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
pop {r4,lr}
bx lr
.arm
.align 2
.global cpu_thumb_add
.type cpu_thumb_add, %function
cpu_thumb_add:
push {r4,lr}
mov r4,r3
msr cpsr_f,r2
adr r3,1f+1
bx r3
.thumb
.align 2
1:
adds r0,r0,r1
nop
bx pc
nop
.arm
.align 2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
pop {r4,lr}
bx lr
.arm
.align 2
.global cpu_thumb_adc
.type cpu_thumb_adc, %function
cpu_thumb_adc:
push {r4,lr}
mov r4,r3
msr cpsr_f,r2
adr r3,1f+1
bx r3
.thumb
.align 2
1:
adcs r0,r1
nop
bx pc
nop
.arm
.align 2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
pop {r4,lr}
bx lr
.arm
.align 2
.global cpu_thumb_sbc
.type cpu_thumb_sbc, %function
cpu_thumb_sbc:
push {r4,lr}
mov r4,r3
msr cpsr_f,r2
adr r3,1f+1
bx r3
.thumb
.align 2
1:
sbcs r0,r1
nop
bx pc
nop
.arm
.align 2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
pop {r4,lr}
bx lr
.arm
.align 2
.global cpu_thumb_tst
.type cpu_thumb_tst, %function
cpu_thumb_tst:
push {r4,lr}
mov r4,r3
msr cpsr_f,r2
adr r3,1f+1
bx r3
.thumb
.align 2
1:
tst r0,r1
nop
bx pc
nop
.arm
.align 2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
pop {r4,lr}
bx lr
.arm
.align 2
.global cpu_thumb_cmp
.type cpu_thumb_cmp, %function
cpu_thumb_cmp:
push {r4,lr}
mov r4,r3
msr cpsr_f,r2
adr r3,1f+1
bx r3
.thumb
.align 2
1:
cmp r0,r1
nop
bx pc
nop
.arm
.align 2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
pop {r4,lr}
bx lr
.arm
.align 2
.global cpu_thumb_cmn
.type cpu_thumb_cmn, %function
cpu_thumb_cmn:
push {r4,lr}
mov r4,r3
msr cpsr_f,r2
adr r3,1f+1
bx r3
.thumb
.align 2
1:
cmn r0,r1
nop
bx pc
nop
.arm
.align 2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
pop {r4,lr}
bx lr
.arm
.align 2
.global cpu_thumb_orr
.type cpu_thumb_orr, %function
cpu_thumb_orr:
push {r4,lr}
mov r4,r3
msr cpsr_f,r2
adr r3,1f+1
bx r3
.thumb
.align 2
1:
orrs r0,r1
nop
bx pc
nop
.arm
.align 2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
pop {r4,lr}
bx lr
.arm
.align 2
.global cpu_thumb_bic
.type cpu_thumb_bic, %function
cpu_thumb_bic:
push {r4,lr}
mov r4,r3
msr cpsr_f,r2
adr r3,1f+1
bx r3
.thumb
.align 2
1:
bics r0,r1
nop
bx pc
nop
.arm
.align 2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
pop {r4,lr}
bx lr
.arm
.align 2
.global cpu_thumb_mvn
.type cpu_thumb_mvn, %function
cpu_thumb_mvn:
push {r4,lr}
mov r4,r3
msr cpsr_f,r2
adr r3,1f+1
bx r3
.thumb
.align 2
1:
mvns r0,r1
nop
bx pc
nop
.arm
.align 2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
pop {r4,lr}
bx lr
.arm
.align 2
.global cpu_thumb_mul
.type cpu_thumb_mul, %function
cpu_thumb_mul:
push {r4,lr}
mov r4,r3
msr cpsr_f,r2
adr r3,1f+1
bx r3
.thumb
.align 2
1:
muls r0,r1,r0
nop
bx pc
nop
.arm
.align 2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
pop {r4,lr}
bx lr
.arm
.align 2
.global cpu_thumb_neg
.type cpu_thumb_neg, %function
cpu_thumb_neg:
push {r4,lr}
mov r4,r3
msr cpsr_f,r2
adr r3,1f+1
bx r3
.thumb
.align 2
1:
rsbs r0,r1,#0
nop
bx pc
nop
.arm
.align 2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
pop {r4,lr}
bx lr
.arm
.align 2
.global cpu_thumb_lsl
.type cpu_thumb_lsl, %function
cpu_thumb_lsl:
push {r4,lr}
mov r4,r3
msr cpsr_f,r2
adr r3,1f+1
bx r3
.thumb
.align 2
1:
lsls r0,r1
nop
bx pc
nop
.arm
.align 2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
pop {r4,lr}
bx lr
.arm
.align 2
.global cpu_thumb_lsr
.type cpu_thumb_lsr, %function
cpu_thumb_lsr:
push {r4,lr}
mov r4,r3
msr cpsr_f,r2
adr r3,1f+1
bx r3
.thumb
.align 2
1:
lsrs r0,r1
nop
bx pc
nop
.arm
.align 2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
pop {r4,lr}
bx lr
.arm
.align 2
.global cpu_thumb_asr
.type cpu_thumb_asr, %function
cpu_thumb_asr:
push {r4,lr}
mov r4,r3
msr cpsr_f,r2
adr r3,1f+1
bx r3
.thumb
.align 2
1:
asrs r0,r1
nop
bx pc
nop
.arm
.align 2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
pop {r4,lr}
bx lr
.arm
.align 2
.global cpu_thumb_ror
.type cpu_thumb_ror, %function
cpu_thumb_ror:
push {r4,lr}
mov r4,r3
msr cpsr_f,r2
adr r3,1f+1
bx r3
.thumb
.align 2
1:
rors r0,r1
nop
bx pc
nop
.arm
.align 2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
pop {r4,lr}
bx lr
.arm
.align 2
.global cpu_arm_lsl_register
.type cpu_arm_lsl_register, %function
cpu_arm_lsl_register:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
movs r0,r0,lsl r1
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_lsr_register
.type cpu_arm_lsr_register, %function
cpu_arm_lsr_register:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
movs r0,r0,lsr r1
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_asr_register
.type cpu_arm_asr_register, %function
cpu_arm_asr_register:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
movs r0,r0,asr r1
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_ror_register
.type cpu_arm_ror_register, %function
cpu_arm_ror_register:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
movs r0,r0,ror r1
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_lsl_imm_0
.type cpu_arm_lsl_imm_0, %function
cpu_arm_lsl_imm_0:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
movs r0,r0,lsl #0
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_lsl_imm_1
.type cpu_arm_lsl_imm_1, %function
cpu_arm_lsl_imm_1:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
movs r0,r0,lsl #1
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_lsl_imm_31
.type cpu_arm_lsl_imm_31, %function
cpu_arm_lsl_imm_31:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
movs r0,r0,lsl #31
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_lsr_imm_1
.type cpu_arm_lsr_imm_1, %function
cpu_arm_lsr_imm_1:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
movs r0,r0,lsr #1
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_lsr_imm_31
.type cpu_arm_lsr_imm_31, %function
cpu_arm_lsr_imm_31:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
movs r0,r0,lsr #31
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_lsr_imm_32
.type cpu_arm_lsr_imm_32, %function
cpu_arm_lsr_imm_32:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
movs r0,r0,lsr #32
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_asr_imm_1
.type cpu_arm_asr_imm_1, %function
cpu_arm_asr_imm_1:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
movs r0,r0,asr #1
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_asr_imm_31
.type cpu_arm_asr_imm_31, %function
cpu_arm_asr_imm_31:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
movs r0,r0,asr #31
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_asr_imm_32
.type cpu_arm_asr_imm_32, %function
cpu_arm_asr_imm_32:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
movs r0,r0,asr #32
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_ror_imm_1
.type cpu_arm_ror_imm_1, %function
cpu_arm_ror_imm_1:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
movs r0,r0,ror #1
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_ror_imm_31
.type cpu_arm_ror_imm_31, %function
cpu_arm_ror_imm_31:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
movs r0,r0,ror #31
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_thumb_lsl_imm_0
.type cpu_thumb_lsl_imm_0, %function
cpu_thumb_lsl_imm_0:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
adr r3,1f+1
bx r3
.thumb
.align 2
1:
lsls r0,r0,#0
.balign 4
bx pc
nop
.arm
.align 2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_thumb_lsl_imm_1
.type cpu_thumb_lsl_imm_1, %function
cpu_thumb_lsl_imm_1:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
adr r3,1f+1
bx r3
.thumb
.align 2
1:
lsls r0,r0,#1
.balign 4
bx pc
nop
.arm
.align 2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_thumb_lsl_imm_31
.type cpu_thumb_lsl_imm_31, %function
cpu_thumb_lsl_imm_31:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
adr r3,1f+1
bx r3
.thumb
.align 2
1:
lsls r0,r0,#31
.balign 4
bx pc
nop
.arm
.align 2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_thumb_lsr_imm_1
.type cpu_thumb_lsr_imm_1, %function
cpu_thumb_lsr_imm_1:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
adr r3,1f+1
bx r3
.thumb
.align 2
1:
lsrs r0,r0,#1
.balign 4
bx pc
nop
.arm
.align 2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_thumb_lsr_imm_31
.type cpu_thumb_lsr_imm_31, %function
cpu_thumb_lsr_imm_31:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
adr r3,1f+1
bx r3
.thumb
.align 2
1:
lsrs r0,r0,#31
.balign 4
bx pc
nop
.arm
.align 2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_thumb_lsr_imm_32
.type cpu_thumb_lsr_imm_32, %function
cpu_thumb_lsr_imm_32:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
adr r3,1f+1
bx r3
.thumb
.align 2
1:
lsrs r0,r0,#32
.balign 4
bx pc
nop
.arm
.align 2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_thumb_asr_imm_1
.type cpu_thumb_asr_imm_1, %function
cpu_thumb_asr_imm_1:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
adr r3,1f+1
bx r3
.thumb
.align 2
1:
asrs r0,r0,#1
.balign 4
bx pc
nop
.arm
.align 2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_thumb_asr_imm_31
.type cpu_thumb_asr_imm_31, %function
cpu_thumb_asr_imm_31:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
adr r3,1f+1
bx r3
.thumb
.align 2
1:
asrs r0,r0,#31
.balign 4
bx pc
nop
.arm
.align 2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_thumb_asr_imm_32
.type cpu_thumb_asr_imm_32, %function
cpu_thumb_asr_imm_32:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
adr r3,1f+1
bx r3
.thumb
.align 2
1:
asrs r0,r0,#32
.balign 4
bx pc
nop
.arm
.align 2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_rrx
.type cpu_arm_rrx, %function
cpu_arm_rrx:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
movs r0,r0,rrx
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_umull_64
.type cpu_arm_umull_64, %function
cpu_arm_umull_64:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
mov r8,#0
msr cpsr_f,r8
mov r6,r2
mov r7,#0
umulls r6,r7,r0,r1
mov r0,r6
mov r1,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_umull_nz
.type cpu_arm_umull_nz, %function
cpu_arm_umull_nz:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
mov r8,#0
msr cpsr_f,r8
mov r6,r2
mov r7,#0
umulls r6,r7,r0,r1
mov r0,r6
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_smull_64
.type cpu_arm_smull_64, %function
cpu_arm_smull_64:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
mov r8,#0
msr cpsr_f,r8
mov r6,r2
mov r7,#0
smulls r6,r7,r0,r1
mov r0,r6
mov r1,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_smull_nz
.type cpu_arm_smull_nz, %function
cpu_arm_smull_nz:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
mov r8,#0
msr cpsr_f,r8
mov r6,r2
mov r7,#0
smulls r6,r7,r0,r1
mov r0,r6
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_umlal_64
.type cpu_arm_umlal_64, %function
cpu_arm_umlal_64:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
mov r8,#0
msr cpsr_f,r8
mov r6,r2
mov r7,#0
umlals r6,r7,r0,r1
mov r0,r6
mov r1,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_umlal_nz
.type cpu_arm_umlal_nz, %function
cpu_arm_umlal_nz:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
mov r8,#0
msr cpsr_f,r8
mov r6,r2
mov r7,#0
umlals r6,r7,r0,r1
mov r0,r6
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_smlal_64
.type cpu_arm_smlal_64, %function
cpu_arm_smlal_64:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
mov r8,#0
msr cpsr_f,r8
mov r6,r2
mov r7,#0
smlals r6,r7,r0,r1
mov r0,r6
mov r1,r7
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_smlal_nz
.type cpu_arm_smlal_nz, %function
cpu_arm_smlal_nz:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
mov r8,#0
msr cpsr_f,r8
mov r6,r2
mov r7,#0
smlals r6,r7,r0,r1
mov r0,r6
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_mla
.type cpu_arm_mla, %function
cpu_arm_mla:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
mov r8,#0
msr cpsr_f,r8
mlas r6,r0,r1,r2
mov r0,r6
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_ldr_pre_plus
.type cpu_arm_ldr_pre_plus, %function
cpu_arm_ldr_pre_plus:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
add r6,r5,#16
mov r7,#1
str r0,[r6,#-4]
str r0,[r6]
str r0,[r6,#4]
ldr r0,[r6,#4]!
sub r1,r6,r5
sub r1,r1,#16
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_str_pre_plus
.type cpu_arm_str_pre_plus, %function
cpu_arm_str_pre_plus:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
add r6,r5,#16
mov r7,#1
str r1,[r6,#-4]
str r1,[r6]
str r1,[r6,#4]
str r0,[r6,#4]!
ldr r0,[r5,#20]
sub r1,r6,r5
sub r1,r1,#16
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_ldr_pre_minus
.type cpu_arm_ldr_pre_minus, %function
cpu_arm_ldr_pre_minus:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
add r6,r5,#16
mov r7,#1
str r0,[r6,#-4]
str r0,[r6]
str r0,[r6,#4]
ldr r0,[r6,#-4]!
sub r1,r6,r5
sub r1,r1,#16
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_str_pre_minus
.type cpu_arm_str_pre_minus, %function
cpu_arm_str_pre_minus:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
add r6,r5,#16
mov r7,#1
str r1,[r6,#-4]
str r1,[r6]
str r1,[r6,#4]
str r0,[r6,#-4]!
ldr r0,[r5,#12]
sub r1,r6,r5
sub r1,r1,#16
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_ldr_post_plus
.type cpu_arm_ldr_post_plus, %function
cpu_arm_ldr_post_plus:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
add r6,r5,#16
mov r7,#1
str r0,[r6,#-4]
str r0,[r6]
str r0,[r6,#4]
ldr r0,[r6],#4
sub r1,r6,r5
sub r1,r1,#16
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_str_post_plus
.type cpu_arm_str_post_plus, %function
cpu_arm_str_post_plus:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
add r6,r5,#16
mov r7,#1
str r1,[r6,#-4]
str r1,[r6]
str r1,[r6,#4]
str r0,[r6],#4
ldr r0,[r5,#16]
sub r1,r6,r5
sub r1,r1,#16
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_ldr_post_minus
.type cpu_arm_ldr_post_minus, %function
cpu_arm_ldr_post_minus:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
add r6,r5,#16
mov r7,#1
str r0,[r6,#-4]
str r0,[r6]
str r0,[r6,#4]
ldr r0,[r6],#-4
sub r1,r6,r5
sub r1,r1,#16
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_str_post_minus
.type cpu_arm_str_post_minus, %function
cpu_arm_str_post_minus:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
add r6,r5,#16
mov r7,#1
str r1,[r6,#-4]
str r1,[r6]
str r1,[r6,#4]
str r0,[r6],#-4
ldr r0,[r5,#16]
sub r1,r6,r5
sub r1,r1,#16
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_ldr_offset
.type cpu_arm_ldr_offset, %function
cpu_arm_ldr_offset:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
add r6,r5,#16
mov r7,#1
str r0,[r6,#-4]
str r0,[r6]
str r0,[r6,#4]
ldr r0,[r6,#4]
sub r1,r6,r5
sub r1,r1,#16
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_str_offset
.type cpu_arm_str_offset, %function
cpu_arm_str_offset:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
add r6,r5,#16
mov r7,#1
str r1,[r6,#-4]
str r1,[r6]
str r1,[r6,#4]
str r0,[r6,#4]
ldr r0,[r5,#20]
sub r1,r6,r5
sub r1,r1,#16
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_ldr_reg_shift
.type cpu_arm_ldr_reg_shift, %function
cpu_arm_ldr_reg_shift:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
add r6,r5,#16
mov r7,#1
str r0,[r6,#-4]
str r0,[r6]
str r0,[r6,#4]
ldr r0,[r6,r7,lsl #2]
sub r1,r6,r5
sub r1,r1,#16
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_str_reg_shift
.type cpu_arm_str_reg_shift, %function
cpu_arm_str_reg_shift:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
add r6,r5,#16
mov r7,#1
str r1,[r6,#-4]
str r1,[r6]
str r1,[r6,#4]
str r0,[r6,r7,lsl #2]
ldr r0,[r5,#20]
sub r1,r6,r5
sub r1,r1,#16
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_ldrb_aligned
.type cpu_arm_ldrb_aligned, %function
cpu_arm_ldrb_aligned:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
str r0,[r5]
str r1,[r5,#4]
movs r6,#0
ldrb r0,[r5]
ldr r1,[r5,#4]
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_ldrh_aligned
.type cpu_arm_ldrh_aligned, %function
cpu_arm_ldrh_aligned:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
str r0,[r5]
str r1,[r5,#4]
movs r6,#0
ldrh r0,[r5]
ldr r1,[r5,#4]
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_ldrsb_aligned
.type cpu_arm_ldrsb_aligned, %function
cpu_arm_ldrsb_aligned:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
str r0,[r5]
str r1,[r5,#4]
movs r6,#0
ldrsb r0,[r5]
ldr r1,[r5,#4]
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_ldrsh_aligned
.type cpu_arm_ldrsh_aligned, %function
cpu_arm_ldrsh_aligned:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
str r0,[r5]
str r1,[r5,#4]
movs r6,#0
ldrsh r0,[r5]
ldr r1,[r5,#4]
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_strb_lanes
.type cpu_arm_strb_lanes, %function
cpu_arm_strb_lanes:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
str r1,[r5]
str r1,[r5,#4]
strb r0,[r5,#1]
ldr r0,[r5]
ldr r1,[r5,#4]
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_strh_lanes
.type cpu_arm_strh_lanes, %function
cpu_arm_strh_lanes:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
str r1,[r5]
str r1,[r5,#4]
strh r0,[r5,#2]
ldr r0,[r5]
ldr r1,[r5,#4]
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_thumb_ldrb_aligned
.type cpu_thumb_ldrb_aligned, %function
cpu_thumb_ldrb_aligned:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
adr r3,1f+1
bx r3
.thumb
.align 2
1:
str r0,[r5]
str r1,[r5,#4]
movs r6,#0
ldrb r0,[r5,r6]
ldr r1,[r5,#4]
.balign 4
bx pc
nop
.arm
.align 2
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_thumb_ldrh_aligned
.type cpu_thumb_ldrh_aligned, %function
cpu_thumb_ldrh_aligned:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
adr r3,1f+1
bx r3
.thumb
.align 2
1:
str r0,[r5]
str r1,[r5,#4]
movs r6,#0
ldrh r0,[r5,r6]
ldr r1,[r5,#4]
.balign 4
bx pc
nop
.arm
.align 2
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_thumb_ldrsb_aligned
.type cpu_thumb_ldrsb_aligned, %function
cpu_thumb_ldrsb_aligned:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
adr r3,1f+1
bx r3
.thumb
.align 2
1:
str r0,[r5]
str r1,[r5,#4]
movs r6,#0
ldrsb r0,[r5,r6]
ldr r1,[r5,#4]
.balign 4
bx pc
nop
.arm
.align 2
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_thumb_ldrsh_aligned
.type cpu_thumb_ldrsh_aligned, %function
cpu_thumb_ldrsh_aligned:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
adr r3,1f+1
bx r3
.thumb
.align 2
1:
str r0,[r5]
str r1,[r5,#4]
movs r6,#0
ldrsh r0,[r5,r6]
ldr r1,[r5,#4]
.balign 4
bx pc
nop
.arm
.align 2
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_thumb_strb_lanes
.type cpu_thumb_strb_lanes, %function
cpu_thumb_strb_lanes:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
adr r3,1f+1
bx r3
.thumb
.align 2
1:
str r1,[r5]
str r1,[r5,#4]
strb r0,[r5,#1]
ldr r0,[r5]
ldr r1,[r5,#4]
.balign 4
bx pc
nop
.arm
.align 2
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_thumb_strh_lanes
.type cpu_thumb_strh_lanes, %function
cpu_thumb_strh_lanes:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
adr r3,1f+1
bx r3
.thumb
.align 2
1:
str r1,[r5]
str r1,[r5,#4]
strh r0,[r5,#2]
ldr r0,[r5]
ldr r1,[r5,#4]
.balign 4
bx pc
nop
.arm
.align 2
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_swp
.type cpu_arm_swp, %function
cpu_arm_swp:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
str r0,[r5]
swp r6,r1,[r5]
mov r0,r6
ldr r1,[r5]
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_swpb
.type cpu_arm_swpb, %function
cpu_arm_swpb:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
str r0,[r5]
swpb r6,r1,[r5]
mov r0,r6
ldr r1,[r5]
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_thumb_ldr_immediate
.type cpu_thumb_ldr_immediate, %function
cpu_thumb_ldr_immediate:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
adr r3,1f+1
bx r3
.thumb
.align 2
1:
str r0,[r5,#4]
movs r6,#4
ldr r0,[r5,#4]
movs r1,#4
.balign 4
bx pc
nop
.arm
.align 2
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_thumb_str_immediate
.type cpu_thumb_str_immediate, %function
cpu_thumb_str_immediate:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
adr r3,1f+1
bx r3
.thumb
.align 2
1:
str r1,[r5]
str r1,[r5,#4]
movs r6,#4
str r0,[r5,#4]
ldr r0,[r5,#4]
ldr r1,[r5]
.balign 4
bx pc
nop
.arm
.align 2
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_thumb_ldr_register
.type cpu_thumb_ldr_register, %function
cpu_thumb_ldr_register:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
adr r3,1f+1
bx r3
.thumb
.align 2
1:
str r0,[r5,#4]
movs r6,#4
ldr r0,[r5,r6]
movs r1,#4
.balign 4
bx pc
nop
.arm
.align 2
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_thumb_str_register
.type cpu_thumb_str_register, %function
cpu_thumb_str_register:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
adr r3,1f+1
bx r3
.thumb
.align 2
1:
str r1,[r5]
str r1,[r5,#4]
movs r6,#4
str r0,[r5,r6]
ldr r0,[r5,#4]
ldr r1,[r5]
.balign 4
bx pc
nop
.arm
.align 2
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_ldm_ia
.type cpu_arm_ldm_ia, %function
cpu_arm_ldm_ia:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
add r6,r5,#16
str r0,[r6,#0]
str r1,[r6,#4]
ldmia r6!,{r0,r1}
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_stm_ia
.type cpu_arm_stm_ia, %function
cpu_arm_stm_ia:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
add r6,r5,#16
stmia r6!,{r0,r1}
ldr r0,[r5,#16]
ldr r1,[r5,#20]
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_ldm_ib
.type cpu_arm_ldm_ib, %function
cpu_arm_ldm_ib:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
add r6,r5,#16
str r0,[r6,#4]
str r1,[r6,#8]
ldmib r6!,{r0,r1}
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_stm_ib
.type cpu_arm_stm_ib, %function
cpu_arm_stm_ib:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
add r6,r5,#16
stmib r6!,{r0,r1}
ldr r0,[r5,#20]
ldr r1,[r5,#24]
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_ldm_da
.type cpu_arm_ldm_da, %function
cpu_arm_ldm_da:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
add r6,r5,#16
str r0,[r6,#-4]
str r1,[r6,#0]
ldmda r6!,{r0,r1}
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_stm_da
.type cpu_arm_stm_da, %function
cpu_arm_stm_da:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
add r6,r5,#16
stmda r6!,{r0,r1}
ldr r0,[r5,#12]
ldr r1,[r5,#16]
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_ldm_db
.type cpu_arm_ldm_db, %function
cpu_arm_ldm_db:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
add r6,r5,#16
str r0,[r6,#-8]
str r1,[r6,#-4]
ldmdb r6!,{r0,r1}
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_stm_db
.type cpu_arm_stm_db, %function
cpu_arm_stm_db:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
add r6,r5,#16
stmdb r6!,{r0,r1}
ldr r0,[r5,#8]
ldr r1,[r5,#12]
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_thumb_branch_eq
.type cpu_thumb_branch_eq, %function
cpu_thumb_branch_eq:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
adr r3,1f+1
bx r3
.thumb
.align 2
1:
beq 2f
movs r0,#0
b 3f
2:
movs r0,#1
3:
.balign 4
bx pc
nop
.arm
.align 2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_thumb_branch_ne
.type cpu_thumb_branch_ne, %function
cpu_thumb_branch_ne:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
adr r3,1f+1
bx r3
.thumb
.align 2
1:
bne 2f
movs r0,#0
b 3f
2:
movs r0,#1
3:
.balign 4
bx pc
nop
.arm
.align 2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_thumb_branch_cs
.type cpu_thumb_branch_cs, %function
cpu_thumb_branch_cs:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
adr r3,1f+1
bx r3
.thumb
.align 2
1:
bcs 2f
movs r0,#0
b 3f
2:
movs r0,#1
3:
.balign 4
bx pc
nop
.arm
.align 2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_thumb_branch_cc
.type cpu_thumb_branch_cc, %function
cpu_thumb_branch_cc:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
adr r3,1f+1
bx r3
.thumb
.align 2
1:
bcc 2f
movs r0,#0
b 3f
2:
movs r0,#1
3:
.balign 4
bx pc
nop
.arm
.align 2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_thumb_branch_mi
.type cpu_thumb_branch_mi, %function
cpu_thumb_branch_mi:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
adr r3,1f+1
bx r3
.thumb
.align 2
1:
bmi 2f
movs r0,#0
b 3f
2:
movs r0,#1
3:
.balign 4
bx pc
nop
.arm
.align 2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_thumb_branch_pl
.type cpu_thumb_branch_pl, %function
cpu_thumb_branch_pl:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
adr r3,1f+1
bx r3
.thumb
.align 2
1:
bpl 2f
movs r0,#0
b 3f
2:
movs r0,#1
3:
.balign 4
bx pc
nop
.arm
.align 2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_thumb_branch_vs
.type cpu_thumb_branch_vs, %function
cpu_thumb_branch_vs:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
adr r3,1f+1
bx r3
.thumb
.align 2
1:
bvs 2f
movs r0,#0
b 3f
2:
movs r0,#1
3:
.balign 4
bx pc
nop
.arm
.align 2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_thumb_branch_vc
.type cpu_thumb_branch_vc, %function
cpu_thumb_branch_vc:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
adr r3,1f+1
bx r3
.thumb
.align 2
1:
bvc 2f
movs r0,#0
b 3f
2:
movs r0,#1
3:
.balign 4
bx pc
nop
.arm
.align 2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_thumb_branch_hi
.type cpu_thumb_branch_hi, %function
cpu_thumb_branch_hi:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
adr r3,1f+1
bx r3
.thumb
.align 2
1:
bhi 2f
movs r0,#0
b 3f
2:
movs r0,#1
3:
.balign 4
bx pc
nop
.arm
.align 2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_thumb_branch_ls
.type cpu_thumb_branch_ls, %function
cpu_thumb_branch_ls:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
adr r3,1f+1
bx r3
.thumb
.align 2
1:
bls 2f
movs r0,#0
b 3f
2:
movs r0,#1
3:
.balign 4
bx pc
nop
.arm
.align 2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_thumb_branch_ge
.type cpu_thumb_branch_ge, %function
cpu_thumb_branch_ge:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
adr r3,1f+1
bx r3
.thumb
.align 2
1:
bge 2f
movs r0,#0
b 3f
2:
movs r0,#1
3:
.balign 4
bx pc
nop
.arm
.align 2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_thumb_branch_lt
.type cpu_thumb_branch_lt, %function
cpu_thumb_branch_lt:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
adr r3,1f+1
bx r3
.thumb
.align 2
1:
blt 2f
movs r0,#0
b 3f
2:
movs r0,#1
3:
.balign 4
bx pc
nop
.arm
.align 2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_thumb_branch_gt
.type cpu_thumb_branch_gt, %function
cpu_thumb_branch_gt:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
adr r3,1f+1
bx r3
.thumb
.align 2
1:
bgt 2f
movs r0,#0
b 3f
2:
movs r0,#1
3:
.balign 4
bx pc
nop
.arm
.align 2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_thumb_branch_le
.type cpu_thumb_branch_le, %function
cpu_thumb_branch_le:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
adr r3,1f+1
bx r3
.thumb
.align 2
1:
ble 2f
movs r0,#0
b 3f
2:
movs r0,#1
3:
.balign 4
bx pc
nop
.arm
.align 2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_thumb_mov_high_roundtrip
.type cpu_thumb_mov_high_roundtrip, %function
cpu_thumb_mov_high_roundtrip:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
adr r3,1f+1
bx r3
.thumb
.align 2
1:
mov r8,r0
mov r0,r8
.balign 4
bx pc
nop
.arm
.align 2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_thumb_add_high_destination
.type cpu_thumb_add_high_destination, %function
cpu_thumb_add_high_destination:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
adr r3,1f+1
bx r3
.thumb
.align 2
1:
mov r8,r0
add r8,r1
mov r0,r8
.balign 4
bx pc
nop
.arm
.align 2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_thumb_add_high_source
.type cpu_thumb_add_high_source, %function
cpu_thumb_add_high_source:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
adr r3,1f+1
bx r3
.thumb
.align 2
1:
mov r8,r1
add r0,r8
.balign 4
bx pc
nop
.arm
.align 2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_thumb_cmp_high_destination
.type cpu_thumb_cmp_high_destination, %function
cpu_thumb_cmp_high_destination:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
adr r3,1f+1
bx r3
.thumb
.align 2
1:
mov r8,r0
cmp r8,r1
.balign 4
bx pc
nop
.arm
.align 2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_thumb_cmp_high_source
.type cpu_thumb_cmp_high_source, %function
cpu_thumb_cmp_high_source:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
adr r3,1f+1
bx r3
.thumb
.align 2
1:
mov r8,r1
cmp r0,r8
.balign 4
bx pc
nop
.arm
.align 2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_thumb_bl_forward
.type cpu_thumb_bl_forward, %function
cpu_thumb_bl_forward:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
adr r3,1f+1
bx r3
.thumb
.align 2
1:
bl 2f
b 3f
2:
adds r0,#1
mov r1,lr
movs r2,#1
ands r1,r2
bx lr
3:
.balign 4
bx pc
nop
.arm
.align 2
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_thumb_bl_backward
.type cpu_thumb_bl_backward, %function
cpu_thumb_bl_backward:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
adr r3,1f+1
bx r3
.thumb
.align 2
1:
b 3f
2:
adds r0,#1
mov r1,lr
movs r2,#1
ands r1,r2
bx lr
3:
bl 2b
.balign 4
bx pc
nop
.arm
.align 2
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_thumb_b_forward_backward
.type cpu_thumb_b_forward_backward, %function
cpu_thumb_b_forward_backward:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
adr r3,1f+1
bx r3
.thumb
.align 2
1:
b 3f
2:
adds r0,#1
b 4f
3:
b 2b
4:
.balign 4
bx pc
nop
.arm
.align 2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_thumb_ldr_literal
.type cpu_thumb_ldr_literal, %function
cpu_thumb_ldr_literal:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
adr r3,1f+1
bx r3
.thumb
.align 2
1:
ldr r0,2f
b 3f
.balign 4
2: .word 0x89abcdef
3:
.balign 4
bx pc
nop
.arm
.align 2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_thumb_adr_pc
.type cpu_thumb_adr_pc, %function
cpu_thumb_adr_pc:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
adr r6,2f
adr r3,1f+1
bx r3
.thumb
.align 2
1:
adr r0,2f
subs r0,r0,r6
b 3f
.balign 4
2: .word 0
3:
.balign 4
bx pc
nop
.arm
.align 2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_thumb_add_sp_address
.type cpu_thumb_add_sp_address, %function
cpu_thumb_add_sp_address:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
adr r3,1f+1
bx r3
.thumb
.align 2
1:
add r0,sp,#12
mov r1,sp
subs r0,r0,r1
.balign 4
bx pc
nop
.arm
.align 2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_thumb_adjust_sp_plus
.type cpu_thumb_adjust_sp_plus, %function
cpu_thumb_adjust_sp_plus:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
adr r3,1f+1
bx r3
.thumb
.align 2
1:
add sp,#508
mov r0,sp
subs r0,r0,r5
sub sp,#508
.balign 4
bx pc
nop
.arm
.align 2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_thumb_adjust_sp_minus
.type cpu_thumb_adjust_sp_minus, %function
cpu_thumb_adjust_sp_minus:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
adr r3,1f+1
bx r3
.thumb
.align 2
1:
sub sp,#508
mov r0,sp
subs r0,r0,r5
add sp,#508
.balign 4
bx pc
nop
.arm
.align 2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_thumb_ldr_sp
.type cpu_thumb_ldr_sp, %function
cpu_thumb_ldr_sp:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
str r0,[sp,#12]
adr r3,1f+1
bx r3
.thumb
.align 2
1:
ldr r0,[sp,#12]
.balign 4
bx pc
nop
.arm
.align 2
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_thumb_str_sp
.type cpu_thumb_str_sp, %function
cpu_thumb_str_sp:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
str r1,[sp,#8]
str r1,[sp,#12]
adr r3,1f+1
bx r3
.thumb
.align 2
1:
str r0,[sp,#12]
ldr r0,[r5,#12]
ldr r1,[sp,#8]
.balign 4
bx pc
nop
.arm
.align 2
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_thumb_push_low
.type cpu_thumb_push_low, %function
cpu_thumb_push_low:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
adr r3,1f+1
bx r3
.thumb
.align 2
1:
push {r0,r1}
ldr r0,[sp]
ldr r1,[sp,#4]
add sp,#8
.balign 4
bx pc
nop
.arm
.align 2
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_thumb_pop_low
.type cpu_thumb_pop_low, %function
cpu_thumb_pop_low:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
adr r3,1f+1
bx r3
.thumb
.align 2
1:
str r0,[sp]
str r1,[sp,#4]
pop {r0,r1}
sub sp,#8
.balign 4
bx pc
nop
.arm
.align 2
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_thumb_push_lr
.type cpu_thumb_push_lr, %function
cpu_thumb_push_lr:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
adr r3,1f+1
bx r3
.thumb
.align 2
1:
mov lr,r0
push {r1,lr}
ldr r0,[sp,#4]
ldr r1,[sp]
add sp,#8
.balign 4
bx pc
nop
.arm
.align 2
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_thumb_pop_pc
.type cpu_thumb_pop_pc, %function
cpu_thumb_pop_pc:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
adr r6,3f+1
str r6,[sp]
adr r3,1f+1
bx r3
.thumb
.align 2
1:
pop {pc}
movs r0,#0
b 4f
3:
movs r0,#1
4:
sub sp,#4
mov r1,sp
subs r1,r1,r5
.balign 4
bx pc
nop
.arm
.align 2
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_thumb_ldmia_data
.type cpu_thumb_ldmia_data, %function
cpu_thumb_ldmia_data:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
adr r3,1f+1
bx r3
.thumb
.align 2
1:
movs r6,r5
str r0,[r5]
str r1,[r5,#4]
ldmia r6!,{r0,r1}
.balign 4
bx pc
nop
.arm
.align 2
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_thumb_ldmia_writeback
.type cpu_thumb_ldmia_writeback, %function
cpu_thumb_ldmia_writeback:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
adr r3,1f+1
bx r3
.thumb
.align 2
1:
movs r6,r5
str r0,[r5]
str r1,[r5,#4]
ldmia r6!,{r0,r1}
subs r0,r6,r5
movs r1,#0
.balign 4
bx pc
nop
.arm
.align 2
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_thumb_stmia_data
.type cpu_thumb_stmia_data, %function
cpu_thumb_stmia_data:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
adr r3,1f+1
bx r3
.thumb
.align 2
1:
movs r6,r5
stmia r6!,{r0,r1}
ldr r0,[r5]
ldr r1,[r5,#4]
.balign 4
bx pc
nop
.arm
.align 2
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_thumb_stmia_writeback
.type cpu_thumb_stmia_writeback, %function
cpu_thumb_stmia_writeback:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
adr r3,1f+1
bx r3
.thumb
.align 2
1:
movs r6,r5
stmia r6!,{r0,r1}
subs r0,r6,r5
movs r1,#0
.balign 4
bx pc
nop
.arm
.align 2
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_cond_eq
.type cpu_arm_cond_eq, %function
cpu_arm_cond_eq:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
mov r0,#0
moveq r0,#1
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_cond_ne
.type cpu_arm_cond_ne, %function
cpu_arm_cond_ne:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
mov r0,#0
movne r0,#1
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_cond_cs
.type cpu_arm_cond_cs, %function
cpu_arm_cond_cs:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
mov r0,#0
movcs r0,#1
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_cond_cc
.type cpu_arm_cond_cc, %function
cpu_arm_cond_cc:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
mov r0,#0
movcc r0,#1
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_cond_mi
.type cpu_arm_cond_mi, %function
cpu_arm_cond_mi:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
mov r0,#0
movmi r0,#1
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_cond_pl
.type cpu_arm_cond_pl, %function
cpu_arm_cond_pl:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
mov r0,#0
movpl r0,#1
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_cond_vs
.type cpu_arm_cond_vs, %function
cpu_arm_cond_vs:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
mov r0,#0
movvs r0,#1
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_cond_vc
.type cpu_arm_cond_vc, %function
cpu_arm_cond_vc:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
mov r0,#0
movvc r0,#1
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_cond_hi
.type cpu_arm_cond_hi, %function
cpu_arm_cond_hi:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
mov r0,#0
movhi r0,#1
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_cond_ls
.type cpu_arm_cond_ls, %function
cpu_arm_cond_ls:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
mov r0,#0
movls r0,#1
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_cond_ge
.type cpu_arm_cond_ge, %function
cpu_arm_cond_ge:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
mov r0,#0
movge r0,#1
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_cond_lt
.type cpu_arm_cond_lt, %function
cpu_arm_cond_lt:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
mov r0,#0
movlt r0,#1
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_cond_gt
.type cpu_arm_cond_gt, %function
cpu_arm_cond_gt:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
mov r0,#0
movgt r0,#1
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_cond_le
.type cpu_arm_cond_le, %function
cpu_arm_cond_le:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
mov r0,#0
movle r0,#1
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_cond_al
.type cpu_arm_cond_al, %function
cpu_arm_cond_al:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
mov r0,#0
moval r0,#1
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_and_operand2_lsl_0
.type cpu_arm_and_operand2_lsl_0, %function
cpu_arm_and_operand2_lsl_0:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
ands r0,r0,r1,lsl #0
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_and_operand2_lsl_1
.type cpu_arm_and_operand2_lsl_1, %function
cpu_arm_and_operand2_lsl_1:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
ands r0,r0,r1,lsl #1
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_and_operand2_lsl_31
.type cpu_arm_and_operand2_lsl_31, %function
cpu_arm_and_operand2_lsl_31:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
ands r0,r0,r1,lsl #31
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_and_operand2_lsr_1
.type cpu_arm_and_operand2_lsr_1, %function
cpu_arm_and_operand2_lsr_1:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
ands r0,r0,r1,lsr #1
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_and_operand2_lsr_32
.type cpu_arm_and_operand2_lsr_32, %function
cpu_arm_and_operand2_lsr_32:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
ands r0,r0,r1,lsr #32
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_and_operand2_asr_1
.type cpu_arm_and_operand2_asr_1, %function
cpu_arm_and_operand2_asr_1:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
ands r0,r0,r1,asr #1
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_and_operand2_asr_32
.type cpu_arm_and_operand2_asr_32, %function
cpu_arm_and_operand2_asr_32:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
ands r0,r0,r1,asr #32
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_and_operand2_ror_1
.type cpu_arm_and_operand2_ror_1, %function
cpu_arm_and_operand2_ror_1:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
ands r0,r0,r1,ror #1
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_and_operand2_ror_31
.type cpu_arm_and_operand2_ror_31, %function
cpu_arm_and_operand2_ror_31:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
ands r0,r0,r1,ror #31
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_and_operand2_rrx_0
.type cpu_arm_and_operand2_rrx_0, %function
cpu_arm_and_operand2_rrx_0:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
ands r0,r0,r1,rrx
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_eor_operand2_lsl_0
.type cpu_arm_eor_operand2_lsl_0, %function
cpu_arm_eor_operand2_lsl_0:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
eors r0,r0,r1,lsl #0
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_eor_operand2_lsl_1
.type cpu_arm_eor_operand2_lsl_1, %function
cpu_arm_eor_operand2_lsl_1:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
eors r0,r0,r1,lsl #1
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_eor_operand2_lsl_31
.type cpu_arm_eor_operand2_lsl_31, %function
cpu_arm_eor_operand2_lsl_31:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
eors r0,r0,r1,lsl #31
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_eor_operand2_lsr_1
.type cpu_arm_eor_operand2_lsr_1, %function
cpu_arm_eor_operand2_lsr_1:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
eors r0,r0,r1,lsr #1
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_eor_operand2_lsr_32
.type cpu_arm_eor_operand2_lsr_32, %function
cpu_arm_eor_operand2_lsr_32:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
eors r0,r0,r1,lsr #32
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_eor_operand2_asr_1
.type cpu_arm_eor_operand2_asr_1, %function
cpu_arm_eor_operand2_asr_1:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
eors r0,r0,r1,asr #1
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_eor_operand2_asr_32
.type cpu_arm_eor_operand2_asr_32, %function
cpu_arm_eor_operand2_asr_32:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
eors r0,r0,r1,asr #32
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_eor_operand2_ror_1
.type cpu_arm_eor_operand2_ror_1, %function
cpu_arm_eor_operand2_ror_1:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
eors r0,r0,r1,ror #1
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_eor_operand2_ror_31
.type cpu_arm_eor_operand2_ror_31, %function
cpu_arm_eor_operand2_ror_31:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
eors r0,r0,r1,ror #31
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_eor_operand2_rrx_0
.type cpu_arm_eor_operand2_rrx_0, %function
cpu_arm_eor_operand2_rrx_0:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
eors r0,r0,r1,rrx
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_orr_operand2_lsl_0
.type cpu_arm_orr_operand2_lsl_0, %function
cpu_arm_orr_operand2_lsl_0:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
orrs r0,r0,r1,lsl #0
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_orr_operand2_lsl_1
.type cpu_arm_orr_operand2_lsl_1, %function
cpu_arm_orr_operand2_lsl_1:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
orrs r0,r0,r1,lsl #1
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_orr_operand2_lsl_31
.type cpu_arm_orr_operand2_lsl_31, %function
cpu_arm_orr_operand2_lsl_31:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
orrs r0,r0,r1,lsl #31
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_orr_operand2_lsr_1
.type cpu_arm_orr_operand2_lsr_1, %function
cpu_arm_orr_operand2_lsr_1:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
orrs r0,r0,r1,lsr #1
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_orr_operand2_lsr_32
.type cpu_arm_orr_operand2_lsr_32, %function
cpu_arm_orr_operand2_lsr_32:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
orrs r0,r0,r1,lsr #32
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_orr_operand2_asr_1
.type cpu_arm_orr_operand2_asr_1, %function
cpu_arm_orr_operand2_asr_1:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
orrs r0,r0,r1,asr #1
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_orr_operand2_asr_32
.type cpu_arm_orr_operand2_asr_32, %function
cpu_arm_orr_operand2_asr_32:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
orrs r0,r0,r1,asr #32
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_orr_operand2_ror_1
.type cpu_arm_orr_operand2_ror_1, %function
cpu_arm_orr_operand2_ror_1:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
orrs r0,r0,r1,ror #1
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_orr_operand2_ror_31
.type cpu_arm_orr_operand2_ror_31, %function
cpu_arm_orr_operand2_ror_31:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
orrs r0,r0,r1,ror #31
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_orr_operand2_rrx_0
.type cpu_arm_orr_operand2_rrx_0, %function
cpu_arm_orr_operand2_rrx_0:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
orrs r0,r0,r1,rrx
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_bic_operand2_lsl_0
.type cpu_arm_bic_operand2_lsl_0, %function
cpu_arm_bic_operand2_lsl_0:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
bics r0,r0,r1,lsl #0
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_bic_operand2_lsl_1
.type cpu_arm_bic_operand2_lsl_1, %function
cpu_arm_bic_operand2_lsl_1:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
bics r0,r0,r1,lsl #1
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_bic_operand2_lsl_31
.type cpu_arm_bic_operand2_lsl_31, %function
cpu_arm_bic_operand2_lsl_31:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
bics r0,r0,r1,lsl #31
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_bic_operand2_lsr_1
.type cpu_arm_bic_operand2_lsr_1, %function
cpu_arm_bic_operand2_lsr_1:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
bics r0,r0,r1,lsr #1
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_bic_operand2_lsr_32
.type cpu_arm_bic_operand2_lsr_32, %function
cpu_arm_bic_operand2_lsr_32:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
bics r0,r0,r1,lsr #32
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_bic_operand2_asr_1
.type cpu_arm_bic_operand2_asr_1, %function
cpu_arm_bic_operand2_asr_1:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
bics r0,r0,r1,asr #1
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_bic_operand2_asr_32
.type cpu_arm_bic_operand2_asr_32, %function
cpu_arm_bic_operand2_asr_32:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
bics r0,r0,r1,asr #32
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_bic_operand2_ror_1
.type cpu_arm_bic_operand2_ror_1, %function
cpu_arm_bic_operand2_ror_1:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
bics r0,r0,r1,ror #1
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_bic_operand2_ror_31
.type cpu_arm_bic_operand2_ror_31, %function
cpu_arm_bic_operand2_ror_31:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
bics r0,r0,r1,ror #31
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_bic_operand2_rrx_0
.type cpu_arm_bic_operand2_rrx_0, %function
cpu_arm_bic_operand2_rrx_0:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
bics r0,r0,r1,rrx
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_mov_operand2_lsl_0
.type cpu_arm_mov_operand2_lsl_0, %function
cpu_arm_mov_operand2_lsl_0:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
movs r0,r1,lsl #0
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_mov_operand2_lsl_1
.type cpu_arm_mov_operand2_lsl_1, %function
cpu_arm_mov_operand2_lsl_1:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
movs r0,r1,lsl #1
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_mov_operand2_lsl_31
.type cpu_arm_mov_operand2_lsl_31, %function
cpu_arm_mov_operand2_lsl_31:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
movs r0,r1,lsl #31
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_mov_operand2_lsr_1
.type cpu_arm_mov_operand2_lsr_1, %function
cpu_arm_mov_operand2_lsr_1:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
movs r0,r1,lsr #1
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_mov_operand2_lsr_32
.type cpu_arm_mov_operand2_lsr_32, %function
cpu_arm_mov_operand2_lsr_32:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
movs r0,r1,lsr #32
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_mov_operand2_asr_1
.type cpu_arm_mov_operand2_asr_1, %function
cpu_arm_mov_operand2_asr_1:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
movs r0,r1,asr #1
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_mov_operand2_asr_32
.type cpu_arm_mov_operand2_asr_32, %function
cpu_arm_mov_operand2_asr_32:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
movs r0,r1,asr #32
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_mov_operand2_ror_1
.type cpu_arm_mov_operand2_ror_1, %function
cpu_arm_mov_operand2_ror_1:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
movs r0,r1,ror #1
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_mov_operand2_ror_31
.type cpu_arm_mov_operand2_ror_31, %function
cpu_arm_mov_operand2_ror_31:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
movs r0,r1,ror #31
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_mov_operand2_rrx_0
.type cpu_arm_mov_operand2_rrx_0, %function
cpu_arm_mov_operand2_rrx_0:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
movs r0,r1,rrx
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_mvn_operand2_lsl_0
.type cpu_arm_mvn_operand2_lsl_0, %function
cpu_arm_mvn_operand2_lsl_0:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
mvns r0,r1,lsl #0
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_mvn_operand2_lsl_1
.type cpu_arm_mvn_operand2_lsl_1, %function
cpu_arm_mvn_operand2_lsl_1:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
mvns r0,r1,lsl #1
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_mvn_operand2_lsl_31
.type cpu_arm_mvn_operand2_lsl_31, %function
cpu_arm_mvn_operand2_lsl_31:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
mvns r0,r1,lsl #31
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_mvn_operand2_lsr_1
.type cpu_arm_mvn_operand2_lsr_1, %function
cpu_arm_mvn_operand2_lsr_1:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
mvns r0,r1,lsr #1
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_mvn_operand2_lsr_32
.type cpu_arm_mvn_operand2_lsr_32, %function
cpu_arm_mvn_operand2_lsr_32:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
mvns r0,r1,lsr #32
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_mvn_operand2_asr_1
.type cpu_arm_mvn_operand2_asr_1, %function
cpu_arm_mvn_operand2_asr_1:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
mvns r0,r1,asr #1
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_mvn_operand2_asr_32
.type cpu_arm_mvn_operand2_asr_32, %function
cpu_arm_mvn_operand2_asr_32:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
mvns r0,r1,asr #32
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_mvn_operand2_ror_1
.type cpu_arm_mvn_operand2_ror_1, %function
cpu_arm_mvn_operand2_ror_1:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
mvns r0,r1,ror #1
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_mvn_operand2_ror_31
.type cpu_arm_mvn_operand2_ror_31, %function
cpu_arm_mvn_operand2_ror_31:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
mvns r0,r1,ror #31
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_mvn_operand2_rrx_0
.type cpu_arm_mvn_operand2_rrx_0, %function
cpu_arm_mvn_operand2_rrx_0:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
mvns r0,r1,rrx
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_add_operand2_lsl_0
.type cpu_arm_add_operand2_lsl_0, %function
cpu_arm_add_operand2_lsl_0:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
adds r0,r0,r1,lsl #0
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_add_operand2_lsl_1
.type cpu_arm_add_operand2_lsl_1, %function
cpu_arm_add_operand2_lsl_1:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
adds r0,r0,r1,lsl #1
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_add_operand2_lsl_31
.type cpu_arm_add_operand2_lsl_31, %function
cpu_arm_add_operand2_lsl_31:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
adds r0,r0,r1,lsl #31
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_add_operand2_lsr_1
.type cpu_arm_add_operand2_lsr_1, %function
cpu_arm_add_operand2_lsr_1:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
adds r0,r0,r1,lsr #1
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_add_operand2_lsr_32
.type cpu_arm_add_operand2_lsr_32, %function
cpu_arm_add_operand2_lsr_32:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
adds r0,r0,r1,lsr #32
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_add_operand2_asr_1
.type cpu_arm_add_operand2_asr_1, %function
cpu_arm_add_operand2_asr_1:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
adds r0,r0,r1,asr #1
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_add_operand2_asr_32
.type cpu_arm_add_operand2_asr_32, %function
cpu_arm_add_operand2_asr_32:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
adds r0,r0,r1,asr #32
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_add_operand2_ror_1
.type cpu_arm_add_operand2_ror_1, %function
cpu_arm_add_operand2_ror_1:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
adds r0,r0,r1,ror #1
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_add_operand2_ror_31
.type cpu_arm_add_operand2_ror_31, %function
cpu_arm_add_operand2_ror_31:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
adds r0,r0,r1,ror #31
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_add_operand2_rrx_0
.type cpu_arm_add_operand2_rrx_0, %function
cpu_arm_add_operand2_rrx_0:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
adds r0,r0,r1,rrx
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_sub_operand2_lsl_0
.type cpu_arm_sub_operand2_lsl_0, %function
cpu_arm_sub_operand2_lsl_0:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
subs r0,r0,r1,lsl #0
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_sub_operand2_lsl_1
.type cpu_arm_sub_operand2_lsl_1, %function
cpu_arm_sub_operand2_lsl_1:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
subs r0,r0,r1,lsl #1
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_sub_operand2_lsl_31
.type cpu_arm_sub_operand2_lsl_31, %function
cpu_arm_sub_operand2_lsl_31:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
subs r0,r0,r1,lsl #31
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_sub_operand2_lsr_1
.type cpu_arm_sub_operand2_lsr_1, %function
cpu_arm_sub_operand2_lsr_1:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
subs r0,r0,r1,lsr #1
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_sub_operand2_lsr_32
.type cpu_arm_sub_operand2_lsr_32, %function
cpu_arm_sub_operand2_lsr_32:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
subs r0,r0,r1,lsr #32
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_sub_operand2_asr_1
.type cpu_arm_sub_operand2_asr_1, %function
cpu_arm_sub_operand2_asr_1:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
subs r0,r0,r1,asr #1
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_sub_operand2_asr_32
.type cpu_arm_sub_operand2_asr_32, %function
cpu_arm_sub_operand2_asr_32:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
subs r0,r0,r1,asr #32
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_sub_operand2_ror_1
.type cpu_arm_sub_operand2_ror_1, %function
cpu_arm_sub_operand2_ror_1:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
subs r0,r0,r1,ror #1
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_sub_operand2_ror_31
.type cpu_arm_sub_operand2_ror_31, %function
cpu_arm_sub_operand2_ror_31:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
subs r0,r0,r1,ror #31
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_sub_operand2_rrx_0
.type cpu_arm_sub_operand2_rrx_0, %function
cpu_arm_sub_operand2_rrx_0:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
subs r0,r0,r1,rrx
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_adc_operand2_lsl_0
.type cpu_arm_adc_operand2_lsl_0, %function
cpu_arm_adc_operand2_lsl_0:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
adcs r0,r0,r1,lsl #0
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_adc_operand2_lsl_1
.type cpu_arm_adc_operand2_lsl_1, %function
cpu_arm_adc_operand2_lsl_1:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
adcs r0,r0,r1,lsl #1
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_adc_operand2_lsl_31
.type cpu_arm_adc_operand2_lsl_31, %function
cpu_arm_adc_operand2_lsl_31:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
adcs r0,r0,r1,lsl #31
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_adc_operand2_lsr_1
.type cpu_arm_adc_operand2_lsr_1, %function
cpu_arm_adc_operand2_lsr_1:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
adcs r0,r0,r1,lsr #1
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_adc_operand2_lsr_32
.type cpu_arm_adc_operand2_lsr_32, %function
cpu_arm_adc_operand2_lsr_32:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
adcs r0,r0,r1,lsr #32
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_adc_operand2_asr_1
.type cpu_arm_adc_operand2_asr_1, %function
cpu_arm_adc_operand2_asr_1:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
adcs r0,r0,r1,asr #1
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_adc_operand2_asr_32
.type cpu_arm_adc_operand2_asr_32, %function
cpu_arm_adc_operand2_asr_32:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
adcs r0,r0,r1,asr #32
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_adc_operand2_ror_1
.type cpu_arm_adc_operand2_ror_1, %function
cpu_arm_adc_operand2_ror_1:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
adcs r0,r0,r1,ror #1
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_adc_operand2_ror_31
.type cpu_arm_adc_operand2_ror_31, %function
cpu_arm_adc_operand2_ror_31:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
adcs r0,r0,r1,ror #31
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_adc_operand2_rrx_0
.type cpu_arm_adc_operand2_rrx_0, %function
cpu_arm_adc_operand2_rrx_0:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
adcs r0,r0,r1,rrx
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_sbc_operand2_lsl_0
.type cpu_arm_sbc_operand2_lsl_0, %function
cpu_arm_sbc_operand2_lsl_0:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
sbcs r0,r0,r1,lsl #0
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_sbc_operand2_lsl_1
.type cpu_arm_sbc_operand2_lsl_1, %function
cpu_arm_sbc_operand2_lsl_1:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
sbcs r0,r0,r1,lsl #1
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_sbc_operand2_lsl_31
.type cpu_arm_sbc_operand2_lsl_31, %function
cpu_arm_sbc_operand2_lsl_31:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
sbcs r0,r0,r1,lsl #31
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_sbc_operand2_lsr_1
.type cpu_arm_sbc_operand2_lsr_1, %function
cpu_arm_sbc_operand2_lsr_1:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
sbcs r0,r0,r1,lsr #1
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_sbc_operand2_lsr_32
.type cpu_arm_sbc_operand2_lsr_32, %function
cpu_arm_sbc_operand2_lsr_32:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
sbcs r0,r0,r1,lsr #32
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_sbc_operand2_asr_1
.type cpu_arm_sbc_operand2_asr_1, %function
cpu_arm_sbc_operand2_asr_1:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
sbcs r0,r0,r1,asr #1
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_sbc_operand2_asr_32
.type cpu_arm_sbc_operand2_asr_32, %function
cpu_arm_sbc_operand2_asr_32:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
sbcs r0,r0,r1,asr #32
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_sbc_operand2_ror_1
.type cpu_arm_sbc_operand2_ror_1, %function
cpu_arm_sbc_operand2_ror_1:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
sbcs r0,r0,r1,ror #1
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_sbc_operand2_ror_31
.type cpu_arm_sbc_operand2_ror_31, %function
cpu_arm_sbc_operand2_ror_31:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
sbcs r0,r0,r1,ror #31
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_sbc_operand2_rrx_0
.type cpu_arm_sbc_operand2_rrx_0, %function
cpu_arm_sbc_operand2_rrx_0:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
sbcs r0,r0,r1,rrx
mrs r1,cpsr
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
.arm
.align 2
.global cpu_arm_msr_flags_mrs
.type cpu_arm_msr_flags_mrs, %function
cpu_arm_msr_flags_mrs:
push {r4-r8,lr}
mov r4,r3
sub sp,sp,#64
mov r5,sp
msr cpsr_f,r2
mrs r6,cpsr
msr cpsr_f,r0
mrs r7,cpsr
and r0,r7,#0xf0000000
eor r1,r6,r7
bic r1,r1,#0xf0000000
str r0,[r4]
str r1,[r4,#4]
add sp,sp,#64
pop {r4-r8,lr}
bx lr
.ltorg
