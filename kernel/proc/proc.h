#ifndef __PROC_H__
#define __PROC_H__
#include "../irq/context.h"
#include "../memory/memory.h"
#include "../fs/fs.h"

#define KSTACK_PAGENUM 8
#define MAX_PROCS 8
#define MAX_SUB_PROCS 4
#define PGSIZE PAGE_SIZE
#define MAX_FD 32

#define ROUNDUP(a, sz) (((a) + (sz) - 1) & ~((sz) - 1))

#define EMPTY_PROC 0
#define RUNNING_PROC 1
#define SLEEPING_PROC 2
#define ZOMBIE_PROC 3

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
    uintptr_t mmap_base;
    char stack[KSTACK_PAGENUM * PAGE_SIZE];
    File *fd_table[MAX_FD];
    int sub_procs[MAX_SUB_PROCS]; 
    int parent_pid;
    int proc_state;
    int exit_status;
    uint64_t sleep_deadline;

    char shadow_fb_pages[512][PAGE_SIZE]; // physical pages (max 512 = 2MB)
    int   shadow_fb_npages;     // number of allocated pages
    uint8_t fb_dirty;           // set by fbsync, read/cleared by desktop
} PCB;
// pid is the order in the PCBs array, i.e. &PCBs[pid] is the PCB for pid
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

// Process management helpers for syscalls
int proc_fork_current(Context *parent_ctx);
int proc_kill_pid(int pid, int sig);
int proc_try_waitpid(int parent_pid, int target_pid, int *out_pid, int *out_status);
void proc_sleep_current(uint64_t seconds);

// Loader functions
void naive_uload(PCB *pcb, const char *filename);
void context_uload(PCB *pcb, char *filename, const char *argv[], const char *envp[]);
void context_kload(PCB *pcb, void (*entry)(void *), void *arg);

#endif
