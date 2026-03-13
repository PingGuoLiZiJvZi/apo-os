/* user-linker.ld — Linker script for user applications.
 * All user processes start at virtual address 0x40000000.
 */

OUTPUT_ARCH(riscv)
ENTRY(_start)

SECTIONS
{
    . = 0x40000000;

    .text : {
        *(.text.entry)
        *(.text .text.*)
    }

    .rodata : {
        *(.rodata .rodata.*)
    }

    .data : {
        *(.data .data.*)
        *(.sdata .sdata.*)
    }

    .bss : {
        __bss_start = .;
        *(.bss .bss.*)
        *(.sbss .sbss.*)
        *(COMMON)
        __bss_end = .;
    }

    /* 'end' symbol used by _sbrk in libos for heap management */
    . = ALIGN(4096);
    PROVIDE(end = .);

    /DISCARD/ : {
        *(.eh_frame)
        *(.comment)
        *(.note.*)
    }
}
