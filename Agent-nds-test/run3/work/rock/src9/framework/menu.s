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
