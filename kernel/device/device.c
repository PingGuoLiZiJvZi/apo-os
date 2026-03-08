#include "device.h"
#include "../libc/stdio.h"
#include "../main/sbi.h"

static inline void mmio_write8(uint64_t addr, uint8_t val) {
    *(volatile uint8_t *)addr = val;
}

static inline uint8_t mmio_read8(uint64_t addr) {
    return *(volatile uint8_t *)addr;
}

static inline void mmio_write32(uint64_t addr, uint32_t val) {
    *(volatile uint32_t *)addr = val;
}

static inline uint32_t mmio_read32(uint64_t addr) {
    return *(volatile uint32_t *)addr;
}

void uart_init() {
    // Opensbi has done the initialization
    mmio_write8(UART0_BASE + UART_IER, UART_IER_RX_ENABLE);
}

void uart_putchar(char c) {
    // Wait until THR is empty 
    while ((mmio_read8(UART0_BASE + UART_LSR) & UART_LSR_TX_IDLE) == 0)
        ;
    mmio_write8(UART0_BASE + UART_THR, c);
}

int uart_getchar() {
    if (mmio_read8(UART0_BASE + UART_LSR) & UART_LSR_RX_READY) {
        return mmio_read8(UART0_BASE + UART_RHR);
    }
    return -1; /* no data available */
}

void plic_init() {
    mmio_write32(PLIC_PRIORITY(UART0_IRQ), 1);

    uint32_t enable = mmio_read32(PLIC_SENABLE(0));
    enable |= (1U << UART0_IRQ);
    mmio_write32(PLIC_SENABLE(0), enable);

    mmio_write32(PLIC_SPRIORITY(0), 0);
}

int plic_claim() {
    return (int)mmio_read32(PLIC_SCLAIM(0));
}

void plic_complete(int irq) {
    mmio_write32(PLIC_SCLAIM(0), (uint32_t)irq);
}

#define SBI_EXT_TIME  0x54494D45
#define SBI_FID_SET_TIMER 0

static void sbi_set_timer(uint64_t stime_value) {
    sbi_call(SBI_EXT_TIME, SBI_FID_SET_TIMER, (long)stime_value, 0, 0);
}

uint64_t timer_get_time() {
    uint64_t t;
    asm volatile("csrr %0, time" : "=r"(t));
    return t;
}

void timer_set_next() {
    uint64_t next = timer_get_time() + TIMER_INTERVAL;
    sbi_set_timer(next);
}

void timer_init() {
    timer_set_next();
    uint64_t sie;
    asm volatile("csrr %0, sie" : "=r"(sie));
    sie |= (1UL << 5);  
    sie |= (1UL << 9);  
    asm volatile("csrw sie, %0" : : "r"(sie));

    // uint64_t sstatus;
    // asm volatile("csrr %0, sstatus" : "=r"(sstatus));
    // sstatus |= (1UL << 1); 
    // asm volatile("csrw sstatus, %0" : : "r"(sstatus));
}

void init_device(const void *dtb) {
    printf("Initializing devices...\n");

    uart_init();
    printf("  UART (NS16550a) initialized at 0x%lx\n", UART0_BASE);

    plic_init();
    printf("  PLIC initialized at 0x%lx\n", PLIC_BASE);

    timer_init();
    printf("  Timer initialized (interval=%lu ticks)\n", TIMER_INTERVAL);

    printf("All devices initialized.\n");
}
