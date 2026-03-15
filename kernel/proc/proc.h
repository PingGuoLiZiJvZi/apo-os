#ifndef __PROC_H__
#define __PROC_H__
#include "../irq/context.h"
#include "../memory/memory.h"
#include "../fs/fs.h"

#define KSTACK_PAGENUM 8
#define MAX_PROCS 8
#define PGSIZE PAGE_SIZE
#define MAX_FD 32

#define ROUNDUP(a, sz) (((a) + (sz) - 1) & ~((sz) - 1))

// A contiguous memory region [start, end)
typedef struct Area {
    void *start;
    void *end;
} Area;


typedef struct AddrSpace {
    void *start;  // user virtual address range start
    void *end;    // user virtual address range end
    uint64_t *pgtable;
} AddrSpace;

typedef struct PCB {
    Context *cp;          // pointer to saved context (top of kstack)
    AddrSpace as;
    uintptr_t max_brk;
    char stack[KSTACK_PAGENUM * PAGE_SIZE];
    File *fd_table[MAX_FD];
} PCB;

extern PCB PCBs[MAX_PROCS];
extern PCB *current_proc;

void init_proc(void);

// Allocate nr_page physical pages
void *new_page(size_t nr_page);

// Map vaddr -> paddr in the address space's page table
void map(AddrSpace *as, void *vaddr, void *paddr, int flags);

// Create a new address space for a process (allocate page table, copy kernel mappings)
void protect(AddrSpace *as);

// Create a user-mode Context on the given kernel stack, with the given entry point
Context *ucontext(AddrSpace *as, Area kstack, void (*entry)(void));

// Create a kernel-mode Context on the given kernel stack
Context *kcontext(Area kstack, void (*entry)(void *), void *arg);

// save prev context, switch to next process, return its context
Context *schedule(Context *prev);
// Exit the current process
void sys_exit(int status);
// Reclaim current user address space for execve 
void proc_exec_reclaim(PCB *pcb);

// Loader functions
void naive_uload(PCB *pcb, const char *filename);
void context_uload(PCB *pcb, char *filename, const char *argv[], const char *envp[]);
void context_kload(PCB *pcb, void (*entry)(void *), void *arg);

#endif