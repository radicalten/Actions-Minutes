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
