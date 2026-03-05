    .section .text
    .globl trap_vector
    .align 4
trap_vector:
    /* Basic panic on trap. We don't save registers yet, just jump to a freeze loop. 
       We could call a C function, but to be 100% safe, let's just loop here so GDB can catch us. */
1:  j 1b
