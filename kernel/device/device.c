#include "device.h"
#include "virtio_gpu.h"
#include "virtio_input.h"
#include "virtio_sound.h"
#include "../libc/stdio.h"
#include "../libc/string.h"
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
    for (int irq = VIRTIO_IRQ_MIN; irq <= VIRTIO_IRQ_MAX; irq++) {
        mmio_write32(PLIC_PRIORITY(irq), 1);
    }

    uint32_t enable = mmio_read32(PLIC_SENABLE(0));
    enable |= (1U << UART0_IRQ);
    for (int irq = VIRTIO_IRQ_MIN; irq <= VIRTIO_IRQ_MAX; irq++) {
        enable |= (1U << irq);
    }
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

static int read_events_device(void *buf, size_t n) {
    VirtioInputEvent ev;
    if (!buf || n == 0) return 0;
    if (!virtio_input_get_event(&ev)) return 0;

    char out[64];
    int len;
    if (ev.type == 1) {
        len = sprintf(out, "%s %u\n", ev.value ? "kd" : "ku", (unsigned)ev.code);
    } else {
        len = sprintf(out, "m %u %u %d\n", (unsigned)ev.type, (unsigned)ev.code, (int32_t)ev.value);
    }

    if (len < 0) return 0;
    if ((size_t)len > n) len = (int)n;
    memcpy(buf, out, (size_t)len);
    return len;
}

static int read_dispinfo_device(void *buf, size_t n) {
    if (!buf || n == 0) return 0;
    int w = 0, h = 0;
    if (virtio_gpu_resolution(&w, &h) < 0) return 0;

    char out[64];
    int len = sprintf(out, "WIDTH:%d\nHEIGHT:%d\n", w, h);
    if (len < 0) return 0;
    if ((size_t)len > n) len = (int)n;
    memcpy(buf, out, (size_t)len);
    return len;
}

int device_fs_read(const char *name, uint32_t *off, void *buf, size_t n) {
    (void)off;
    if (!name || !buf) return -1;

    if (strcmp(name, "events") == 0) {
        return read_events_device(buf, n);
    }
    if (strcmp(name, "dispinfo") == 0) {
        return read_dispinfo_device(buf, n);
    }

    char *p = (char *)buf;
    for (size_t i = 0; i < n; i++) {
        int ch = uart_getchar();
        if (ch < 0) return (int)i;
        p[i] = (char)ch;
    }
    return (int)n;
}

int device_fs_write(const char *name, uint32_t *off, const void *buf, size_t n) {
    if (!name || !buf) return -1;

    if (strcmp(name, "audio") == 0) {
        int w = virtio_sound_write(buf, n);
        if (w > 0 && off) *off += (uint32_t)w;
        return w;
    }

    if (strcmp(name, "fb") == 0) {
        int w = 0, h = 0;
        if (virtio_gpu_resolution(&w, &h) < 0) return -1;
        if (w <= 0 || h <= 0) return -1;
        if ((n % 4) != 0 || !off) return -1;

        const uint32_t *src = (const uint32_t *)buf;
        uint32_t px_off = *off / 4;
        uint32_t total_px = (uint32_t)(n / 4);
        uint32_t written_px = 0;

        while (written_px < total_px) {
            int x = (int)(px_off % (uint32_t)w);
            int y = (int)(px_off / (uint32_t)w);
            if (y >= h) break;

            uint32_t row_left = (uint32_t)(w - x);
            uint32_t remain = total_px - written_px;
            uint32_t chunk_px = remain < row_left ? remain : row_left;

            if (virtio_gpu_fbdraw(x, y, (int)chunk_px, 1, src + written_px, 1) < 0) {
                return -1;
            }

            written_px += chunk_px;
            px_off += chunk_px;
        }

        uint32_t written_bytes = written_px * 4;
        *off += written_bytes;
        return (int)written_bytes;
    }

    const char *p = (const char *)buf;
    for (size_t i = 0; i < n; i++) {
        uart_putchar(p[i]);
    }
    return (int)n;
}

void device_poll() {
    virtio_input_poll();
    virtio_gpu_poll();
    virtio_sound_poll();
}

int device_handle_irq(int irq) {
    if (virtio_input_match_irq(irq)) {
        virtio_input_handle_irq();
        return 1;
    }
    if (virtio_gpu_match_irq(irq)) {
        virtio_gpu_handle_irq();
        return 1;
    }
    if (virtio_sound_match_irq(irq)) {
        virtio_sound_handle_irq();
        return 1;
    }
    return 0;
}

void init_device() {
    printf("Initializing devices...\n");

    uart_init();
    printf("  UART (NS16550a) initialized at 0x%lx\n", UART0_BASE);

    plic_init();
    printf("  PLIC initialized at 0x%lx\n", PLIC_BASE);

    timer_init();
    printf("  Timer initialized (interval=%lu ticks)\n", TIMER_INTERVAL);

    virtio_gpu_init();
    virtio_input_init();
    virtio_sound_init();

    printf("All devices initialized.\n");
}
