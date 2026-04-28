#include "memory.h"
#include "../libc/stdio.h"
#include "../libc/string.h"
#include "../system/system.h"

extern char _bss_end[]; 
extern char _stack_top[];

// Free list structure: stored directly inside the free pages 
struct Run {
    struct Run *next;
};

static struct {
    struct Run *freelist;
} kmem;

// Round up to the nearest page boundary 
 
static inline uint64_t align_up(uint64_t addr, uint64_t align) {
    return (addr + align - 1) & ~(align - 1);
}

static inline uint64_t align_down(uint64_t addr, uint64_t align) {
    return addr & ~(align - 1);
}


// Free a physical page
// The physical address `pa` must be page-aligned.

void kfree(void *pa) {
    if (((uint64_t)pa % PAGE_SIZE) != 0 || (char*)pa < _bss_end) {
        panic("kfree: invalid pa aligned or below bss");
    }

    // Fill with junk to catch dangling refs.
    memset(pa, 1, PAGE_SIZE);

    struct Run *r = (struct Run *)pa;
    r->next = kmem.freelist;
    kmem.freelist = r;
}


// Allocate a 4096-byte physical page.
// Returns a pointer that the kernel can use, or NULL if out of memory.

void *kalloc() {
    struct Run *r = kmem.freelist;
    if (r) {
        kmem.freelist = r->next;
        memset((void*)r, 5, PAGE_SIZE); // Fill with another junk to catch bugs
    }
    return (void *)r;
}


// Free a range of physical memory by dividing it into pages
// and pushing them onto the free list.
 
static void free_range(void *pa_start, void *pa_end) {
    char *p = (char *)align_up((uint64_t)pa_start, PAGE_SIZE);
    char *end = (char *)align_down((uint64_t)pa_end, PAGE_SIZE);
    
    for (; p + PAGE_SIZE <= end; p += PAGE_SIZE) {
        kfree(p);
    }
}

// Sv39 PTE definitions are in memory.h


uint64_t *kernel_pagetable;

// Return the address of the PTE in page table 'pagetable' that corresponds to virtual address 'va'. 
// If 'alloc' is true, create any missing page directory pages.
uint64_t *walk(uint64_t *pagetable, uint64_t va, int alloc) {

    for (int level = 2; level > 0; level--) {
        uint64_t *pte = &pagetable[PX(level, va)];
        if (*pte & PTE_V) {
            pagetable = (uint64_t *)PTE2PA(*pte);
        } else {
            if (!alloc || (pagetable = (uint64_t *)kalloc()) == NULL) {
                return NULL;
            }
            memset(pagetable, 0, PAGE_SIZE);
            *pte = PA2PTE(pagetable) | PTE_V;
        }
    }
    return &pagetable[PX(0, va)];
}

// Create PTEs for virtual addresses starting at `va` that refer to
// physical addresses starting at `pa`. `va` and size must be page-aligned.
// Returns 0 on success, -1 on failure.
int map_pages(uint64_t *pagetable, uint64_t va, uint64_t pa, uint64_t size, uint64_t perm) {
    uint64_t a, last;
    uint64_t *pte;

    if (size == 0) return 0;
    
    a = align_down(va, PAGE_SIZE);
    last = align_down(va + size - 1, PAGE_SIZE);
    for (;;) {
        if ((pte = walk(pagetable, a, 1)) == NULL) {
            return -1;
        }
        if (*pte & PTE_V) {
            panic("map_pages: remap");
        }
        *pte = PA2PTE(pa) | perm | PTE_V;
        if (a == last)
            break;
        a += PAGE_SIZE;
        pa += PAGE_SIZE;
    }
    return 0;
}

static uint64_t ram_end = 0x88000000;
static uint64_t ram_base = 0x80000000;
static uint64_t kernel_end;

// VA == PA for [ram_base, ram_end).
static void map_kernel_memory() {
    printf("Mapping physical memory 0x%lx -> 0x%lx (Identity Map)...\n", ram_base, ram_end);
    if (map_pages(kernel_pagetable, ram_base, ram_base, ram_end - ram_base, PTE_R | PTE_W | PTE_X) != 0) {
        panic("map_kernel_memory: failed to map physical RAM");
    }
}


static void map_mmio_memory() {
    // UART0 (8250 compatible, QEMU virt: 0x10000000)
    uint64_t uart_base = 0x10000000;
    if (map_pages(kernel_pagetable, uart_base, uart_base, PAGE_SIZE, PTE_R | PTE_W) != 0) {
        panic("map_mmio_memory: failed to map UART");
    }

    // CLINT (Core-Local Interruptor, QEMU virt: 0x02000000, size 0x10000)
    uint64_t clint_base = 0x02000000;
    if (map_pages(kernel_pagetable, clint_base, clint_base, 0x10000, PTE_R | PTE_W) != 0) {
        panic("map_mmio_memory: failed to map CLINT");
    }

    // PLIC (Platform-Level Interrupt Controller, QEMU virt: 0x0C000000, size 0x600000)
    uint64_t plic_base = 0x0C000000;
    if (map_pages(kernel_pagetable, plic_base, plic_base, 0x600000, PTE_R | PTE_W) != 0) {
        panic("map_mmio_memory: failed to map PLIC");
    }

    // VirtIO MMIO devices (QEMU virt: 0x10001000 .. 0x10008000, 8 slots × 0x1000)
    uint64_t virtio_base = 0x10001000;
    if (map_pages(kernel_pagetable, virtio_base, virtio_base, 8 * PAGE_SIZE, PTE_R | PTE_W) != 0) {
        panic("map_mmio_memory: failed to map VirtIO");
    }
}

static void enable_vm() {
    asm volatile("sfence.vma zero, zero");

    uint64_t satp = MAKE_SATP((uint64_t)kernel_pagetable);
    printf("Enabling MMU with SATP=0x%lx\n", satp);

    asm volatile("csrw satp, %0" : : "r" (satp));
    asm volatile("sfence.vma zero, zero");

    printf("MMU enabled successfully! Virtual memory is now active.\n");
}

static void init_physical_memory() {
    // init manage manager
    kernel_end = align_up((uint64_t)_stack_top, PAGE_SIZE);

    printf("Kernel physical end: 0x%lx\n", kernel_end);
    printf("Initializing free list from 0x%lx to 0x%lx\n", kernel_end, ram_end);

    kmem.freelist = NULL;
    free_range((void *)kernel_end, (void *)ram_end);
}

static void init_virtual_memory() {
    // prepare kernel page table
    kernel_pagetable = (uint64_t *)kalloc();
    if (!kernel_pagetable) {
        panic("init_memory: failed to allocate root page table");
    }
    memset(kernel_pagetable, 0, PAGE_SIZE);

    printf("Root page table allocated at PA: 0x%lx\n", (uint64_t)kernel_pagetable);

    map_kernel_memory();
    map_mmio_memory();

    enable_vm();
}

void init_memory() {
    init_physical_memory();
    init_virtual_memory();
}