#include "device.h"
#include "virtio_gpu.h"
#include "virtio_input.h"
#include "virtio_sound.h"
#include "../libc/stdio.h"
#include "../libc/string.h"
#include "../main/sbi.h"
#include "../proc/proc.h"

typedef struct {
    uint8_t mask;
    uint8_t reserved[3];
    GpuDirtyRect rects[MAX_PROCS];
} FbSyncInfo;

static int input_focus_pid = 1;

static int current_pid(void) {
    if (!current_proc) return 0;
    if (current_proc < &PCBs[0] || current_proc >= &PCBs[MAX_PROCS]) return 0;
    return (int)(current_proc - &PCBs[0]) + 1;
}

static int pid_can_receive_input(int pid) {
    if (pid <= 0 || pid > MAX_PROCS) return 0;
    int state = PCBs[pid - 1].proc_state;
    return state != EMPTY_PROC && state != ZOMBIE_PROC;
}

static int input_focus_current(void) {
    if (!pid_can_receive_input(input_focus_pid)) input_focus_pid = 1;
    return input_focus_pid;
}

static void input_focus_set(int pid) {
    if (pid_can_receive_input(pid)) {
        input_focus_pid = pid;
    } else {
        input_focus_pid = 1;
    }
}

static int proc_fb_dirty_add(PCB *pcb, const GpuDirtyRect *rect) {
    if (!pcb || !rect) return 0;

    int screen_w = 0;
    int screen_h = 0;
    if (virtio_gpu_resolution(&screen_w, &screen_h) < 0) return 0;
    if (screen_w <= 0 || screen_h <= 0) return 0;

    int x1 = rect->x;
    int y1 = rect->y;
    int x2 = rect->x + rect->w;
    int y2 = rect->y + rect->h;

    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x2 > screen_w) x2 = screen_w;
    if (y2 > screen_h) y2 = screen_h;
    if (x1 >= x2 || y1 >= y2) return 0;

    if (!pcb->fb_dirty || pcb->fb_dirty_w <= 0 || pcb->fb_dirty_h <= 0) {
        pcb->fb_dirty_x = x1;
        pcb->fb_dirty_y = y1;
        pcb->fb_dirty_w = x2 - x1;
        pcb->fb_dirty_h = y2 - y1;
    } else {
        int old_x2 = pcb->fb_dirty_x + pcb->fb_dirty_w;
        int old_y2 = pcb->fb_dirty_y + pcb->fb_dirty_h;
        if (x1 > pcb->fb_dirty_x) x1 = pcb->fb_dirty_x;
        if (y1 > pcb->fb_dirty_y) y1 = pcb->fb_dirty_y;
        if (x2 < old_x2) x2 = old_x2;
        if (y2 < old_y2) y2 = old_y2;
        pcb->fb_dirty_x = x1;
        pcb->fb_dirty_y = y1;
        pcb->fb_dirty_w = x2 - x1;
        pcb->fb_dirty_h = y2 - y1;
    }
    pcb->fb_dirty = 1;
    return 1;
}

static void proc_fb_dirty_mark_full_if_empty(PCB *pcb) {
    if (!pcb) return;
    pcb->fb_dirty = 1;
    if (pcb->fb_dirty_w > 0 && pcb->fb_dirty_h > 0) return;

    int screen_w = 0;
    int screen_h = 0;
    if (virtio_gpu_resolution(&screen_w, &screen_h) < 0) return;
    pcb->fb_dirty_x = 0;
    pcb->fb_dirty_y = 0;
    pcb->fb_dirty_w = screen_w;
    pcb->fb_dirty_h = screen_h;
}

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
    int pid = current_pid();
    int focus_pid = input_focus_current();
    int is_desktop = virtio_gpu_is_desktop_proc();
    int include_pointer = is_desktop;
    int include_keyboard = (pid == focus_pid);

    while (virtio_input_get_event_filtered(include_pointer, include_keyboard, &ev)) {
        char out[64];
        int len = 0;

        if (ev.type == 1) {
            len = sprintf(out, "%s %u\n", ev.value ? "kd" : "ku", (unsigned)ev.code);
        } else if (ev.type == 3) {
            len = sprintf(out, "ma %u %u\n", (unsigned)ev.code, (unsigned)ev.value);
        } else {
            continue;
        }

        if (len < 0) return 0;
        if ((size_t)len > n) len = (int)n;
        memcpy(buf, out, (size_t)len);
        return len;
    }

    return 0;
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
    // Desktop reads fbsync to get dirty bitmask 
    if (strcmp(name, "fbsync") == 0) {
        if (n < 1) return 0;
        if (n >= sizeof(FbSyncInfo)) {
            FbSyncInfo info;
            memset(&info, 0, sizeof(info));
            for (int i = 0; i < MAX_PROCS; i++) {
                if (PCBs[i].fb_dirty) {
                    info.mask |= (uint8_t)(1 << i);
                    info.rects[i].x = PCBs[i].fb_dirty_x;
                    info.rects[i].y = PCBs[i].fb_dirty_y;
                    info.rects[i].w = PCBs[i].fb_dirty_w;
                    info.rects[i].h = PCBs[i].fb_dirty_h;
                    PCBs[i].fb_dirty = 0;
                    PCBs[i].fb_dirty_x = 0;
                    PCBs[i].fb_dirty_y = 0;
                    PCBs[i].fb_dirty_w = 0;
                    PCBs[i].fb_dirty_h = 0;
                }
            }
            memcpy(buf, &info, sizeof(info));
            return (int)sizeof(info);
        }

        uint8_t mask = 0;
        for (int i = 0; i < MAX_PROCS; i++) {
            if (PCBs[i].fb_dirty) {
                mask |= (uint8_t)(1 << i);
                PCBs[i].fb_dirty = 0;
                PCBs[i].fb_dirty_x = 0;
                PCBs[i].fb_dirty_y = 0;
                PCBs[i].fb_dirty_w = 0;
                PCBs[i].fb_dirty_h = 0;
            }
        }
        *(uint8_t *)buf = mask;
        return 1;
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

    if (strcmp(name, "input") == 0) {
        if (!virtio_gpu_is_desktop_proc()) return -1;
        if (n >= sizeof(int)) {
            int pid = *(const int *)buf;
            input_focus_set(pid);
        }
        if (off) *off += (uint32_t)n;
        return (int)n;
    }

    if (strcmp(name, "audio") == 0) {
        int w = virtio_sound_write(buf, n);
        if (w > 0 && off) *off += (uint32_t)w;
        return w;
    }

    if (strcmp(name, "fbsync") == 0) {
        if (virtio_gpu_is_desktop_proc()) {
            if (n >= sizeof(GpuDirtyRect) && (n % sizeof(GpuDirtyRect)) == 0) {
                if (virtio_gpu_fbdirty_rects((const GpuDirtyRect *)buf, n / sizeof(GpuDirtyRect)) < 0) {
                    return -1;
                }
            }
            if (virtio_gpu_fbsync() < 0) return -1;
        } else {
            int has_rect = 0;
            if (n >= sizeof(GpuDirtyRect) && (n % sizeof(GpuDirtyRect)) == 0) {
                const GpuDirtyRect *rects = (const GpuDirtyRect *)buf;
                size_t count = n / sizeof(GpuDirtyRect);
                for (size_t i = 0; i < count; i++) {
                    if (proc_fb_dirty_add(current_proc, &rects[i])) {
                        has_rect = 1;
                    }
                }
            }
            if (!has_rect) {
                proc_fb_dirty_mark_full_if_empty(current_proc);
            }
        }
        if (off) *off += (uint32_t)n;
        return (int)n;
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

            if (virtio_gpu_fbwrite(x, y, (int)chunk_px, 1, src + written_px) < 0) {
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

uint64_t device_mmap_size(const char *name) {
    if (!name) return 0;
    if (strcmp(name, "fb") == 0) {
        uint64_t fb_sz = virtio_gpu_fb_size();
        if (virtio_gpu_is_desktop_proc()) {
            // Desktop can mmap real fb + all child shadow FBs 
            return fb_sz * (1 + MAX_PROCS);
        }
        return fb_sz;
    }
    return 0;
}

int device_mmap_page(const char *name, uint64_t offset, uint64_t *pa) {
    if (!name || !pa) return -1;
    if (strcmp(name, "fb") == 0) {
        uint64_t fb_sz = virtio_gpu_fb_size();
        if (virtio_gpu_is_desktop_proc()) {
            if (offset < fb_sz) {
                // Real GPU framebuffer
                return virtio_gpu_fb_page(offset, pa);
            } else {
                // Child shadow FB: child_idx = (offset - fb_sz) / fb_sz 
                uint64_t child_off = offset - fb_sz;
                int child_idx = (int)(child_off / fb_sz);
                uint64_t page_off = child_off % fb_sz;
                // Align to page boundary 
                page_off = page_off & ~(uint64_t)(PAGE_SIZE - 1);
                return virtio_gpu_child_fb_page(child_idx, page_off, pa);
            }
        } else {
            // Non-desktop: redirect to shadow FB 
            return virtio_gpu_shadow_fb_page(offset, pa);
        }
    }
    return -1;
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
