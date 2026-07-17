/*
 * jit_trampoline.S - JIT block execution trampoline for PowerPC (Wii/Broadway)
 *
 * void executeBlock_asm(uint32_t* blockCode);
 *
 * Calls blockCode as a PPC function using bctrl so that:
 *   - LR is set to a valid PPC return address before the block executes
 *   - The block's mflr captures that address correctly
 *   - The block's blr returns here
 */

    .section .text
    .global executeBlock_asm
    .type   executeBlock_asm, @function
    .balign 32

executeBlock_asm:
    # Save our return address and allocate minimal frame
    mflr    %r0
    stwu    %r1, -32(%r1)       /* 32-byte frame: 16 min + 16 for saves */
    stw     %r0,  36(%r1)       /* LR at old_r1 + 4 (ABI LR save area)  */
    stw     %r31, 28(%r1)       /* save r31 (scratch)                    */
    stw     %r30, 24(%r1)       /* save r30 (scratch)                    */

    /* r3 = blockCode, load into CTR and call */
    mtctr   %r3
    bctrl                       /* LR = address of next instruction      */

    /* Block's blr returns here */
    lwz     %r30, 24(%r1)
    lwz     %r31, 28(%r1)
    lwz     %r0,  36(%r1)
    addi    %r1,  %r1, 32
    mtlr    %r0
    blr

    .size executeBlock_asm, .-executeBlock_asm
