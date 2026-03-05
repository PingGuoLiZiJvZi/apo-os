#include "memory.h"
#include "../libc/stdio.h"
#include "../libc/string.h"
#include "../system/system.h"
#include <libfdt.h>

extern char _bss_end[]; 
extern char _stack_top[];

// Free list structure: stored directly inside the free pages 
struct run {
    struct run *next;
};

static struct {
    struct run *freelist;
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

    struct run *r = (struct run *)pa;
    r->next = kmem.freelist;
    kmem.freelist = r;
}


// Allocate a 4096-byte physical page.
// Returns a pointer that the kernel can use, or NULL if out of memory.

void *kalloc(void) {
    struct run *r = kmem.freelist;
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

// Sv39 Page Table Definitions 
#define PTE_V     (1L << 0) // Valid
#define PTE_R     (1L << 1)
#define PTE_W     (1L << 2)
#define PTE_X     (1L << 3)
#define PTE_U     (1L << 4) // User

// extract physical address from PTE
#define PTE2PA(pte) (((pte) >> 10) << 12)
// convert physical address to PTE
#define PA2PTE(pa)  ((((uint64_t)pa) >> 12) << 10)

// 9 bits per level, 3 levels, page size = 4096 (12 bits)
#define PX(level, va) ((((uint64_t)(va)) >> (12 + 9 * (level))) & 0x1FF)

// SATP Mode 8 is Sv39
#define SATP_SV39 (8ULL << 60)
#define MAKE_SATP(pagetable) (SATP_SV39 | (((uint64_t)pagetable) >> 12))

static uint64_t *kernel_pagetable;

/*
 * Return the address of the PTE in page table 'pagetable' that corresponds to virtual address 'va'. 
 * If 'alloc' is true, create any missing page directory pages.
 */
static uint64_t *walk(uint64_t *pagetable, uint64_t va, int alloc) {
    if (va >= (1ULL << 38)) { 
        // Sv39 supports 39-bit VA (up to 512GB). 
        // We do not handle negative upper half logic thoroughly right now.
        // We just ensure we don't exceed max positive mapped range for identity mapping.
        // Actually, RISC-V requires bits 63:39 to match bit 38, so typical valid VAs 
        // are 0x000000x... or 0xFFFFFFx... 
        // For simple identity mapping on QEMU, va is around 0x80000000 which is fine.
    }

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

/*
 * Create PTEs for virtual addresses starting at `va` that refer to
 * physical addresses starting at `pa`. `va` and size must be page-aligned.
 * Returns 0 on success, -1 on failure.
 */
static int map_pages(uint64_t *pagetable, uint64_t va, uint64_t pa, uint64_t size, uint64_t perm) {
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
/* Module-level memory layout info, populated by init_memory() */
static uint64_t ram_base;
static uint64_t ram_size;
static uint64_t ram_end;

/*
 * Identity-map the entire physical RAM region.
 * VA == PA for [ram_base, ram_end).
 */
static void map_kernel_memory(void) {
    printf("Mapping physical memory 0x%lx -> 0x%lx (Identity Map)...\n", ram_base, ram_end);
    if (map_pages(kernel_pagetable, ram_base, ram_base, ram_size, PTE_R | PTE_W | PTE_X) != 0) {
        panic("map_kernel_memory: failed to map physical RAM");
    }
}

/*
 * Identity-map MMIO device regions so the kernel can access
 * hardware registers after the MMU is enabled.
 */
static void map_mmio_memory(void) {
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
}

/*
 * Flush the TLB, write SATP to activate Sv39 paging, then flush again.
 */
static void enable_vm(void) {
    asm volatile("sfence.vma zero, zero");

    uint64_t satp = MAKE_SATP((uint64_t)kernel_pagetable);
    printf("Enabling MMU with SATP=0x%lx\n", satp);

    asm volatile("csrw satp, %0" : : "r" (satp));
    asm volatile("sfence.vma zero, zero");

    printf("MMU enabled successfully! Virtual memory is now active.\n");
}

/*
 * init_memory — called once at boot.
 *
 * 1. Parse DTB to discover the physical memory layout.
 * 2. Initialise the physical page allocator (free-list).
 * 3. Build the kernel page table (identity map RAM + MMIO).
 * 4. Enable Sv39 virtual memory.
 */
static void parse_dtb_memory(const void *dtb) {
    if (fdt_check_header(dtb) != 0) {
        panic("init_memory: invalid dtb");
    }

    // device tree phasing

    int root_node = fdt_path_offset(dtb, "/");
    if (root_node < 0) {
        panic("init_memory: no root node in dtb");
    }

    int ac = fdt_address_cells(dtb, root_node);
    int sc = fdt_size_cells(dtb, root_node);

    if (ac != 2 || sc != 2) {
        printf("Warning: Expected address/size cells = 2, got ac=%d, sc=%d. Might fail parsing memory!\n", ac, sc);
    }

    int mem_node = fdt_path_offset(dtb, "/memory");
    if (mem_node < 0) {
        mem_node = fdt_node_offset_by_prop_value(dtb, -1, "device_type", "memory", 7);
        if (mem_node < 0) {
            panic("init_memory: no memory node found in dtb");
        }
    }

    int len;
    const fdt32_t *reg = fdt_getprop(dtb, mem_node, "reg", &len);
    if (!reg || len < (ac + sc) * (int)sizeof(fdt32_t)) {
        panic("init_memory: invalid memory reg property");
    }

    if (ac == 2) {
        ram_base = ((uint64_t)fdt32_to_cpu(reg[0]) << 32) | fdt32_to_cpu(reg[1]);
        reg += 2;
    } else {
        ram_base = fdt32_to_cpu(reg[0]);
        reg += 1;
    }

    if (sc == 2) {
        ram_size = ((uint64_t)fdt32_to_cpu(reg[0]) << 32) | fdt32_to_cpu(reg[1]);
    } else {
        ram_size = fdt32_to_cpu(reg[0]);
    }

    ram_end = ram_base + ram_size;

    printf("Memory: base=0x%lx, size=0x%lx bytes\n", ram_base, ram_size);
}

static void init_physical_memory() {
    // init manage manager
    uint64_t kernel_end = align_up((uint64_t)_stack_top, PAGE_SIZE);

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

void init_memory(const void *dtb) {
    parse_dtb_memory(dtb);
    init_physical_memory();
    init_virtual_memory();
}