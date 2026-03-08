#include "context.h"
#include "../device/device.h"
#include "../libc/stdio.h"

extern void trap();
extern void syscall_handler(Context *c);

void *get_satp() {
    uint64_t satp;
    asm volatile("csrr %0, satp" : "=r"(satp));
    return (void *)satp;
}

void set_satp(void *pdir) {
    uint64_t satp = (uint64_t)pdir;
    asm volatile("csrw satp, %0" : : "r"(satp));
}

void clint_handler(Context *c) {
    c;
    timer_set_next();
    /* TODO: schedule / context switch here */
}

void plic_handler(Context *c) {
    c;
    int irq = plic_claim();
    if (irq == UART0_IRQ) {
        int ch = uart_getchar();
        if (ch >= 0) {
            printf("[UART RX] %c\n", (char)ch);
        }
    }
    if (irq > 0) {
        plic_complete(irq);
    }
}

Context *trap_handle(Context *c) {
    c->pdir = get_satp();

    switch (c->scause) {
        case YIELD:
            syscall_handler(c);
            break;
        case IRQ_TIMER:
            clint_handler(c);
            break;
        case IRQ_UART:
            plic_handler(c);
            break;
        default:
            printf("Unhandled trap: scause=0x%lx, sepc=0x%lx\n",
                   c->scause, c->sepc);
            break;
    }

    set_satp(c->pdir);
    return c;
}
