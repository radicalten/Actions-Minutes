.syntax unified
.arm
.text
.global service_restart7
service_restart7:
 mov r0,#0xdf
 msr cpsr_c,r0
 b arm7_main_post_inittest
