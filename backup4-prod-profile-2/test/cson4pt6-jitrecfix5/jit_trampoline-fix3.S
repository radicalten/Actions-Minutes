    .section .text
    .global executeBlock_asm
    .type   executeBlock_asm, @function
    .balign 32

executeBlock_asm:
    mflr    %r0
    stwu    %r1, -32(%r1)
    stw     %r0,  36(%r1)
    stw     %r31, 28(%r1)
    stw     %r30, 24(%r1)

    # Force cached alias: set bit 31 of r3
    oris    %r3, %r3, 0x8000

    mtctr   %r3
    bctrl

    lwz     %r30, 24(%r1)
    lwz     %r31, 28(%r1)
    lwz     %r0,  36(%r1)
    addi    %r1,  %r1, 32
    mtlr    %r0
    blr

    .size executeBlock_asm, .-executeBlock_asm
