OUTPUT_FORMAT("elf32-littlearm", "elf32-bigarm", "elf32-littlearm")
OUTPUT_ARCH(arm)
ENTRY(main)

MEMORY
{
    rom : ORIGIN = 0x02200000, LENGTH = 0x100000
}

SECTIONS
{
    .text : { *(.text*) *(.bss*) } > rom   /* retain zero-initialized storage in the flat loaded image */
}
