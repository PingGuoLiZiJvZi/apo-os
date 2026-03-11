#include "proc.h"
#include "../libc/stdio.h"

PCB PCBs[MAX_PROCS];
PCB *current_proc = 0;

void init_proc(void) {
    // TODO: initialize process table
}

// ---- Stubs: to be implemented when virtual memory / context switch is ready ----

void *new_page(size_t nr_page) {
    // TODO: allocate nr_page contiguous physical pages
    (void)nr_page;
    panic("new_page: not implemented");
    return 0;
}

void map(AddrSpace *as, void *vaddr, void *paddr, int flags) {
    // TODO: map vaddr -> paddr in as->pgtable
    (void)as; (void)vaddr; (void)paddr; (void)flags;
    panic("map: not implemented");
}

void protect(AddrSpace *as) {
    // TODO: allocate page table for process, copy kernel mappings
    (void)as;
    panic("protect: not implemented");
}

Context *ucontext(AddrSpace *as, Area kstack, void (*entry)(void)) {
    // TODO: create user-mode context
    (void)as; (void)kstack; (void)entry;
    panic("ucontext: not implemented");
    return 0;
}

Context *kcontext(Area kstack, void (*entry)(void *), void *arg) {
    // TODO: create kernel-mode context
    (void)kstack; (void)entry; (void)arg;
    panic("kcontext: not implemented");
    return 0;
}
