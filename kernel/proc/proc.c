#include "proc.h"
#include "../libc/stdio.h"
#include "../libc/string.h"
#include "../memory/memory.h"

PCB PCBs[MAX_PROCS];
static PCB boot_pcb;  // boot PCB to hold init context
PCB *current_proc = 0;



void *new_page(size_t nr_page) {
    // Allocate nr_page contiguous physical pages using kalloc.
    // kalloc returns one page at a time; we allocate them sequentially.
    // Because the free list is LIFO and pages were freed in order,
    // consecutive kalloc calls often return contiguous pages.
    void *first = 0;
    for (size_t i = 0; i < nr_page; i++) {
        void *p = kalloc();
        if (!p) panic("new_page: out of memory");
        memset(p, 0, PAGE_SIZE);
        if (i == 0) first = p;
    }
    return first;
}

void map(AddrSpace *as, void *vaddr, void *paddr, int flags) {
    // Map a single page: vaddr -> paddr with flags
    if (map_pages(as->pgtable, (uint64_t)vaddr, (uint64_t)paddr,
                  PAGE_SIZE, flags) != 0) {
        panic("map: map_pages failed");
    }
}

void protect(AddrSpace *as) {
    // Allocate a new root page table for the process
    uint64_t *pgtable = (uint64_t *)kalloc();
    if (!pgtable) panic("protect: out of memory");
    memset(pgtable, 0, PAGE_SIZE);

    // Copy all kernel mappings from the kernel page table

    for (int i = 0; i < 512; i++) {
        pgtable[i] = kernel_pagetable[i];
    }

    as->pgtable = pgtable;
    // User virtual address space: 0x40000000 to 0x80000000 (1 GiB)
    as->start = (void *)0x40000000UL;
    as->end   = (void *)0x80000000UL;
}

// Context creation

// RISC-V sstatus bits
#define SSTATUS_SPP  (1UL << 8)   // Supervisor Previous Privilege: 1=S-mode
#define SSTATUS_SPIE (1UL << 5)   // Supervisor Previous Interrupt Enable

Context *kcontext(Area kstack, void (*entry)(void *), void *arg) {
    // Place Context at the top of the kernel stack
    Context *ctx = (Context *)((char *)kstack.end - sizeof(Context));
    memset(ctx, 0, sizeof(Context));

    ctx->sepc = (uint64_t)entry;
    ctx->sstatus = SSTATUS_SPP | SSTATUS_SPIE;  // return to S-mode, enable interrupts
    ctx->GPRx = (uint64_t)arg;   // a0 = arg
    ctx->pdir = (void *)MAKE_SATP((uint64_t)kernel_pagetable);
    ctx->np = 0;  // kernel mode

    return ctx;
}

Context *ucontext(AddrSpace *as, Area kstack, void (*entry)(void)) {
    Context *ctx = (Context *)((char *)kstack.end - sizeof(Context));
    memset(ctx, 0, sizeof(Context));

    ctx->sepc = (uint64_t)entry;
    ctx->sstatus = SSTATUS_SPIE;  // return to U-mode (SPP=0), enable interrupts
    ctx->pdir = (void *)MAKE_SATP((uint64_t)as->pgtable);
    ctx->np = 1;  // user mode

    return ctx;
}

// Scheduler

Context *schedule(Context *prev) {
    current_proc->cp = prev;

    int cur_idx = -1;
    if (current_proc >= &PCBs[0] && current_proc < &PCBs[MAX_PROCS]) {
        cur_idx = (int)(current_proc - PCBs);
    }

    for (int i = 0; i < MAX_PROCS; i++) {
        int idx = (cur_idx + 1 + i) % MAX_PROCS;
        if (PCBs[idx].cp != 0) {
            current_proc = &PCBs[idx];
            return current_proc->cp;
        }
    }

    panic("schedule: no runnable process");
    return 0;  // unreachable
}

// Test kernel thread

static void hello_fun(void *arg) {
    int j = 1;
    while (1) {
        if (j % 500000 == 0)
            printf("Hello from %s! count=%d\n", (char *)arg, j);
        j++;
        // Preempted by timer interrupt; no explicit yield needed
    }
}

void init_proc(void) {
    printf("Initializing process subsystem...\n");
    memset(PCBs, 0, sizeof(PCBs));
    memset(&boot_pcb, 0, sizeof(boot_pcb));
    context_kload(&PCBs[0], hello_fun, (void *)"Thread-A");
    context_kload(&PCBs[1], hello_fun, (void *)"Thread-B");

    // Set current to first process
    current_proc = &PCBs[0];
    Context *ctx = current_proc->cp;

    printf("[proc] Switching to first process (sepc=0x%lx)...\n", ctx->sepc);

    // Direct bootstrap: restore the context and sret into hello_fun.
    // switch is defined in trap.S 
    extern void asm_switch(Context *ctx) __attribute__((noreturn));
    asm_switch(ctx);
}
