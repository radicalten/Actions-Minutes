OUTPUT_FORMAT("elf32-littlearm", "elf32-bigarm", "elf32-littlearm")
OUTPUT_ARCH(arm)
ENTRY(main)

MEMORY
{
    rom : ORIGIN = 0x02000100, LENGTH = 0x1EFF00  /* stop before raw MPU-abort scratch at 021f0000 */
}

SECTIONS
{
    .text : { *(.text*) *(.bss*) } > rom   /* retain zero-initialized storage in the flat loaded image */
}
