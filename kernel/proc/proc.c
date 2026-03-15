#include "proc.h"
#include "../libc/stdio.h"
#include "../libc/string.h"
#include "../memory/memory.h"

PCB PCBs[MAX_PROCS];
static PCB boot_pcb;  // boot PCB to hold init context
PCB *current_proc = 0;



void *new_page(size_t nr_page) {
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
    // Always grant full user-mode access for user address space mappings
    uint64_t perm = PTE_U | PTE_R | PTE_W | PTE_X | (uint64_t)flags;
    if (map_pages(as->pgtable, (uint64_t)vaddr, (uint64_t)paddr,
                  PAGE_SIZE, perm) != 0) {
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
    // Only save context if the process hasn't exited
    if (current_proc->cp != 0)
        current_proc->cp = prev;

    int cur_idx = -1;
    if (current_proc >= &PCBs[0] && current_proc < &PCBs[MAX_PROCS]) {
        cur_idx = (int)(current_proc - PCBs);
    }

    for (int i = 0; i < MAX_PROCS; i++) {
        int idx = (cur_idx + 1 + i) % MAX_PROCS;
        if (PCBs[idx].cp != 0) {
            // printf("schedule: switching to PCBs[%d] cp=%p sepc=0x%lx\n",
            //        idx, (void *)PCBs[idx].cp, PCBs[idx].cp->sepc);
            current_proc = &PCBs[idx];
            return current_proc->cp;
        }
    }

    panic("schedule: no runnable process");
    return 0;  // unreachable
}

// Recursively free all physical pages and page table pages at levels 0-1.
static void freewalk_level(uint64_t *pagetable, int level) {
    for (int i = 0; i < 512; i++) {
        uint64_t pte = pagetable[i];
        if (!(pte & PTE_V)) continue;

        if (level > 0 && !(pte & (PTE_R | PTE_W | PTE_X))) {
            freewalk_level((uint64_t *)PTE2PA(pte), level - 1);
        } else {
            kfree((void *)PTE2PA(pte));
        }
        pagetable[i] = 0;
    }
    // Free this page table page itself
    kfree(pagetable);
}

// Skips kernel entries at level 2 (they are shared copies from kernel_pagetable).
static void free_user_pages(AddrSpace *as) {
    uint64_t *pgtable = as->pgtable;

    // Switch to kernel page table FIRST — we're about to destroy the user page table

    asm volatile("csrw satp, %0" : : "r"(MAKE_SATP((uint64_t)kernel_pagetable)));
    asm volatile("sfence.vma");

    for (int i = 0; i < 512; i++) {
        // Skip kernel entries (copied from kernel_pagetable in protect())
        if (pgtable[i] == kernel_pagetable[i]) continue;
        if (!(pgtable[i] & PTE_V)) continue;

        // This is a user-created subtree, recurse and free
        freewalk_level((uint64_t *)PTE2PA(pgtable[i]), 1);
        pgtable[i] = 0;
    }
    // Free the root page table itself
    kfree(pgtable);
    as->pgtable = 0;
}

void proc_exec_reclaim(PCB *pcb) {
    if (pcb == 0) return;
    if (pcb->as.pgtable != 0) {
        free_user_pages(&pcb->as);
    }
    pcb->max_brk = 0;
}

// Exit the current process: close fds, reclaim memory, mark as not runnable
void sys_exit(int status) {
    PCB *pcb = current_proc;
    printf("sys_exit: closing fds...\n");
    // Close all open file descriptors
    for (int i = 0; i < MAX_FD; i++) {
        if (pcb->fd_table[i]) {
            fs_close(pcb->fd_table[i]);
            pcb->fd_table[i] = 0;
        }
    }
    printf("sys_exit: freeing user pages...\n");
    // Reclaim all user-space physical pages and page tables
    free_user_pages(&pcb->as);
    printf("sys_exit: done, marking not runnable\n");
    pcb->cp = 0;  // mark as not runnable
}

static void proc_init_stdio(PCB *pcb) {
    File *serial = fs_open("/device/serial");
    if (!serial) panic("proc_init_stdio: cannot open /device/serial");
    pcb->fd_table[0] = serial;      // stdin

    // Open two more references for stdout and stderr
    File *serial1 = fs_open("/device/serial");
    if (!serial1) panic("proc_init_stdio: cannot open /device/serial");
    pcb->fd_table[1] = serial1;     // stdout

    File *serial2 = fs_open("/device/serial");
    if (!serial2) panic("proc_init_stdio: cannot open /device/serial");
    pcb->fd_table[2] = serial2;     // stderr
}

void init_proc(void) {
    printf("Initializing process subsystem...\n");
    memset(PCBs, 0, sizeof(PCBs));
    memset(&boot_pcb, 0, sizeof(boot_pcb));

    // Load input smoke test + 2 hello user processes
    const char *argv0[] = {"input-smoke", 0};
    const char *envp0[] = {0};
    context_uload(&PCBs[0], "/bin/bird", argv0, envp0);
    proc_init_stdio(&PCBs[0]);

    const char *argv1[] = {"hello-orange", 0};
    const char *envp1[] = {0};
    context_uload(&PCBs[1], "/bin/hello-orange", argv1, envp1);
    proc_init_stdio(&PCBs[1]);

    const char *argv2[] = {"hello-plum", 0};
    const char *envp2[] = {0};
    context_uload(&PCBs[2], "/bin/hello-plum", argv2, envp2);
    proc_init_stdio(&PCBs[2]);

    // Set current to first process
    current_proc = &PCBs[0];
    Context *ctx = current_proc->cp;

    printf("Switching to first user process (sepc=0x%lx)...\n", ctx->sepc);

    // Direct bootstrap: restore the context and sret into user entry.
    // asm_switch is defined in trap.S 
    extern void asm_switch(Context *ctx) __attribute__((noreturn));
    asm_switch(ctx);
}
