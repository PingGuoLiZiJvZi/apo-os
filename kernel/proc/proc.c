#include "proc.h"
#include "../device/device.h"
#include "../libc/stdio.h"
#include "../libc/string.h"
#include "../memory/memory.h"

PCB PCBs[MAX_PROCS];
static PCB boot_pcb;  // boot PCB to hold init context
PCB *current_proc = 0;

#define TICKS_PER_SEC 10000000UL

static inline int pid_from_pcb(const PCB *pcb) {
    if (pcb < &PCBs[0] || pcb >= &PCBs[MAX_PROCS]) return -1;
    return (int)(pcb - &PCBs[0]) + 1;
}

static inline PCB *pcb_from_pid(int pid) {
    if (pid <= 0 || pid > MAX_PROCS) return 0;
    return &PCBs[pid - 1];
}

static void reset_pcb_slot(PCB *pcb) {
    pcb->cp = 0;
    pcb->as.start = 0;
    pcb->as.end = 0;
    pcb->as.pgtable = 0;
    pcb->max_brk = 0;
    for (int i = 0; i < MAX_FD; i++) {
        pcb->fd_table[i] = 0;
    }
    for (int i = 0; i < MAX_SUB_PROCS; i++) {
        pcb->sub_procs[i] = -1;
    }
    pcb->parent_pid = -1;
    pcb->proc_state = EMPTY_PROC;
    pcb->exit_status = 0;
    pcb->sleep_deadline = 0;
}

static int parent_has_child_pid(PCB *parent, int pid) {
    if (!parent) return 0;
    for (int i = 0; i < MAX_SUB_PROCS; i++) {
        if (parent->sub_procs[i] == pid) return 1;
    }
    return 0;
}

static int add_child_pid(PCB *parent, int pid) {
    for (int i = 0; i < MAX_SUB_PROCS; i++) {
        if (parent->sub_procs[i] == -1) {
            parent->sub_procs[i] = pid;
            return 0;
        }
    }
    return -1;
}

static void remove_child_pid(PCB *parent, int pid) {
    if (!parent) return;
    for (int i = 0; i < MAX_SUB_PROCS; i++) {
        if (parent->sub_procs[i] == pid) {
            parent->sub_procs[i] = -1;
            return;
        }
    }
}

static int find_empty_proc_slot(void) {
    for (int i = 0; i < MAX_PROCS; i++) {
        if (PCBs[i].proc_state == EMPTY_PROC) return i;
    }
    return -1;
}



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
    // Save context unless this process has already become non-runnable.
    if (current_proc->cp != 0)
        current_proc->cp = prev;

    uint64_t now = timer_get_time();
    for (int i = 0; i < MAX_PROCS; i++) {
        if (PCBs[i].proc_state == SLEEPING_PROC &&
            PCBs[i].sleep_deadline != 0 &&
            now >= PCBs[i].sleep_deadline) {
            PCBs[i].sleep_deadline = 0;
            PCBs[i].proc_state = RUNNING_PROC;
        }
    }

    int cur_idx = -1;
    if (current_proc >= &PCBs[0] && current_proc < &PCBs[MAX_PROCS]) {
        cur_idx = (int)(current_proc - PCBs);
    }

    for (int i = 0; i < MAX_PROCS; i++) {
        int idx = (cur_idx + 1 + i) % MAX_PROCS;
        if (PCBs[idx].cp != 0 && PCBs[idx].proc_state == RUNNING_PROC) {
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

static void terminate_proc(PCB *pcb, int status) {
    if (pcb == 0 || pcb->proc_state == EMPTY_PROC || pcb->proc_state == ZOMBIE_PROC) {
        return;
    }

    for (int i = 0; i < MAX_FD; i++) {
        if (pcb->fd_table[i]) {
            fs_close(pcb->fd_table[i]);
            pcb->fd_table[i] = 0;
        }
    }

    if (pcb->as.pgtable != 0) {
        free_user_pages(&pcb->as);
    }

    pcb->max_brk = 0;
    pcb->sleep_deadline = 0;
    pcb->cp = 0;
    pcb->exit_status = status;
    pcb->proc_state = ZOMBIE_PROC;

    int self_pid = pid_from_pcb(pcb);
    PCB *parent = pcb_from_pid(pcb->parent_pid);
    if (!parent || !parent_has_child_pid(parent, self_pid)) {
        reset_pcb_slot(pcb);
    }
}

// Exit the current process: close fds, reclaim memory, mark as not runnable
void sys_exit(int status) {
    terminate_proc(current_proc, status);
}

static int clone_user_pages(PCB *parent, PCB *child) {
    for (uintptr_t va = (uintptr_t)parent->as.start;
         va < (uintptr_t)parent->as.end;
         va += PAGE_SIZE) {
        uint64_t *pte = walk(parent->as.pgtable, va, 0);
        if (pte == 0) continue;
        if (!(*pte & PTE_V)) continue;
        if (!(*pte & (PTE_R | PTE_W | PTE_X))) continue;

        uint64_t pa = PTE2PA(*pte);
        void *newpa = new_page(1);
        memcpy(newpa, (void *)pa, PAGE_SIZE);

        uint64_t perm = *pte & (PTE_R | PTE_W | PTE_X | PTE_U);
        if (map_pages(child->as.pgtable, va, (uint64_t)newpa, PAGE_SIZE, perm) != 0) {
            return -1;
        }
    }
    return 0;
}

int proc_fork_current(Context *parent_ctx) {
    PCB *parent = current_proc;
    if (!parent || !parent_ctx) return -1;

    int free_slot = find_empty_proc_slot();
    if (free_slot < 0) return -1;

    int parent_pid = pid_from_pcb(parent);
    if (add_child_pid(parent, free_slot + 1) < 0) {
        return -1;
    }

    PCB *child = &PCBs[free_slot];
    memset(child, 0, sizeof(*child));
    for (int i = 0; i < MAX_SUB_PROCS; i++) {
        child->sub_procs[i] = -1;
    }

    protect(&child->as);
    child->max_brk = parent->max_brk;
    child->parent_pid = parent_pid;
    child->proc_state = RUNNING_PROC;
    child->exit_status = 0;
    child->sleep_deadline = 0;

    if (clone_user_pages(parent, child) < 0) {
        proc_exec_reclaim(child);
        reset_pcb_slot(child);
        remove_child_pid(parent, free_slot + 1);
        return -1;
    }

    memcpy(child->stack, parent->stack, sizeof(parent->stack));
    intptr_t ctx_off = (intptr_t)((char *)parent_ctx - parent->stack);
    if (ctx_off < 0 || ctx_off > (intptr_t)(sizeof(parent->stack) - sizeof(Context))) {
        proc_exec_reclaim(child);
        reset_pcb_slot(child);
        remove_child_pid(parent, free_slot + 1);
        return -1;
    }

    child->cp = (Context *)(child->stack + ctx_off);
    child->cp->pdir = (void *)MAKE_SATP((uint64_t)child->as.pgtable);
    child->cp->GPRx = 0;

    for (int i = 0; i < MAX_FD; i++) {
        if (parent->fd_table[i]) {
            child->fd_table[i] = fs_dup(parent->fd_table[i]);
        }
    }

    return free_slot + 1;
}

int proc_kill_pid(int pid, int sig) {
    PCB *target = pcb_from_pid(pid);
    if (!target) return -1;
    if (target->proc_state == EMPTY_PROC || target->proc_state == ZOMBIE_PROC || target->cp == 0) {
        return -1;
    }

    int status = 128 + (sig & 0x7f);
    if (target == current_proc) {
        terminate_proc(target, status);
        return 1;
    }

    terminate_proc(target, status);
    return 0;
}

int proc_try_waitpid(int parent_pid, int target_pid, int *out_pid, int *out_status) {
    PCB *parent = pcb_from_pid(parent_pid);
    if (!parent) return -1;

    int has_match = 0;
    for (int i = 0; i < MAX_SUB_PROCS; i++) {
        int child_pid = parent->sub_procs[i];
        if (child_pid <= 0) continue;
        if (target_pid > 0 && child_pid != target_pid) continue;

        has_match = 1;
        PCB *child = pcb_from_pid(child_pid);
        if (!child) {
            parent->sub_procs[i] = -1;
            continue;
        }

        if (child->proc_state == ZOMBIE_PROC) {
            if (out_pid) *out_pid = child_pid;
            if (out_status) *out_status = child->exit_status;
            remove_child_pid(parent, child_pid);
            reset_pcb_slot(child);
            return 1;
        }
    }

    return has_match ? 0 : -1;
}

void proc_sleep_current(uint64_t seconds) {
    if (seconds == 0) return;
    uint64_t now = timer_get_time();
    uint64_t delta = seconds * TICKS_PER_SEC;
    current_proc->sleep_deadline = now + delta;
    current_proc->proc_state = SLEEPING_PROC;
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

    for(int i = 0; i < MAX_PROCS; i++) {
        reset_pcb_slot(&PCBs[i]);
    }
    
    // M1 desktop bootstrap: run desktop as first process.
    const char *argv0[] = {"desktop", 0};
    const char *envp0[] = {0};
    context_uload(&PCBs[0], "/bin/desktop", argv0, envp0);
    proc_init_stdio(&PCBs[0]);

    // Keep remaining slots empty in M1.

    // Set current to first process
    current_proc = &PCBs[0];
    Context *ctx = current_proc->cp;

    printf("Switching to first user process (sepc=0x%lx)...\n", ctx->sepc);

    // Direct bootstrap: restore the context and sret into user entry.
    // asm_switch is defined in trap.S 
    extern void asm_switch(Context *ctx) __attribute__((noreturn));
    asm_switch(ctx);
}
