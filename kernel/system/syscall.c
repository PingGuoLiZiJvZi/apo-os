#include "syscall.h"
static int sys_write(Context *c) {
    int fd         = (int)c->GPR2;       // a0
    uintptr_t buf  = (uintptr_t)c->GPR3; // a1
    size_t    len  = (size_t)c->GPR4;     // a2

    // Validate fd
    if (fd < 0 || fd >= MAX_FD || current_proc->fd_table[fd] == 0) {
        return -1;
    }
    File *f = current_proc->fd_table[fd];

    // Copy user data to kernel buffer (switch to user page table + SUM)
    char kbuf[512];
    size_t total = 0;
    while (total < len) {
        size_t chunk = len - total;
        if (chunk > sizeof(kbuf)) chunk = sizeof(kbuf);

        uint64_t old_satp;
        asm volatile("csrr %0, satp" : "=r"(old_satp));
        asm volatile("csrw satp, %0" : : "r"((uint64_t)c->pdir));
        asm volatile("sfence.vma");
        uint64_t old_sstatus;
        asm volatile("csrr %0, sstatus" : "=r"(old_sstatus));
        asm volatile("csrs sstatus, %0" : : "r"(1UL << 18));  // SUM

        for (size_t i = 0; i < chunk; i++) {
            kbuf[i] = ((const char *)buf)[total + i];
        }

        asm volatile("csrw sstatus, %0" : : "r"(old_sstatus));
        asm volatile("csrw satp, %0" : : "r"(old_satp));
        asm volatile("sfence.vma");

        int r = fs_write(f, kbuf, chunk);
        if (r <= 0) break;
        total += r;
    }

    return (int)total;
}

// Simple brk: grow the process heap by mapping new pages.
static int sys_brk(Context *c) {
    uintptr_t new_brk = (uintptr_t)c->GPR2;  // a0
    PCB *pcb = current_proc;

    uintptr_t brk_page = ROUNDUP(new_brk, PAGE_SIZE);
    uintptr_t cur_page = pcb->max_brk;

    for (uintptr_t a = cur_page; a < brk_page; a += PAGE_SIZE) {
        void *page = new_page(1);
        map(&pcb->as, (void *)a, page, 0);
    }

    if (brk_page > pcb->max_brk)
        pcb->max_brk = brk_page;

    return 0;  // success
}

Context* syscall_handler(Context *c) {
    int syscall_id = c->GPR1;  
    switch (syscall_id) {
        case SYS_yield:
            return schedule(c);
        case SYS_exit:
            printf("exit(%ld)\n", c->GPR2);
            sys_exit(c->GPR2);
            return schedule(c);
        case SYS_write:
            c->GPRx = sys_write(c);
            return c;
        case SYS_brk:
            c->GPRx = sys_brk(c);
            return c;
        default:
            printf("unhandled syscall id=%d\n", syscall_id);
            c->GPRx = -1;
            return c;
    }
}
