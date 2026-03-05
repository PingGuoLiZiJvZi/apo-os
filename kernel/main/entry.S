    .section .text.entry
    .globl _start

_start:
    /* OpenSBI passes: a0 = hartid, a1 = dtb pointer */
    la   sp, _stack_top
    call main

    /* Should never reach here */
spin:
    wfi
    j    spin
