#include "context.h"
#include "../device/device.h"
#include "../libc/stdio.h"

extern void trap();
extern Context *syscall_handler(Context *c);
extern Context *schedule(Context *prev);

Context *trap_handle(Context *c) {
    switch (c->scause) {
        case ECALL_U:
        case ECALL_S:
            // ecall: advance sepc past the ecall instruction
            c->sepc += 4;
            return syscall_handler(c);
        case IRQ_TIMER:
            timer_set_next();
            return schedule(c);
        case IRQ_UART:
        {
            int irq = plic_claim();
            if (irq == UART0_IRQ) {
                int ch = uart_getchar();
                if (ch >= 0) {
                    printf("UART RX: %c\n", (char)ch);
                }
            } else if (irq > 0) {
                if (!device_handle_irq(irq)) {
                    printf("Unhandled external irq=%d\n", irq);
                }
            }
            if (irq > 0) {
                plic_complete(irq);
            }
            break;
        }
        default:
         {
             uint64_t stval = 0;
             asm volatile("csrr %0, stval" : "=r"(stval));
             printf("Unhandled trap: scause=0x%lx, sepc=0x%lx, stval=0x%lx, np=%lu\n",
                 c->scause, c->sepc, stval, c->np);
            break;
         }
    }

    return c;
}
