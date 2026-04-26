#ifndef __MEMORY_H__
#define __MEMORY_H__

#include <stdint.h>
#include <stddef.h>

#define PAGE_SIZE 4096

// Sv39 PTE flag bits
#define PTE_V (1L << 0) // Valid
#define PTE_R (1L << 1) // Read
#define PTE_W (1L << 2) // Write
#define PTE_X (1L << 3) // Execute
#define PTE_U (1L << 4) // User
#define PTE_SHARED (1L << 8) // RSW: mapped page is not owned by this process

// PTE <-> PA conversions
#define PTE2PA(pte) (((pte) >> 10) << 12)
#define PA2PTE(pa)  ((((uint64_t)(pa)) >> 12) << 10)

// Extract VPN index at given level from VA
#define PX(level, va) ((((uint64_t)(va)) >> (12 + 9 * (level))) & 0x1FF)

// SATP register (Sv39 mode = 8)
#define SATP_SV39 (8ULL << 60)
#define MAKE_SATP(pagetable) (SATP_SV39 | (((uint64_t)(pagetable)) >> 12))

// Kernel page table (set up during init_memory)
extern uint64_t *kernel_pagetable;

void init_memory(void);
void *kalloc(void);
void kfree(void *pa);

// Walk the page table to find/create the PTE for va
uint64_t *walk(uint64_t *pagetable, uint64_t va, int alloc);

// Map [va, va+size) -> [pa, pa+size) with permissions perm
int map_pages(uint64_t *pagetable, uint64_t va, uint64_t pa,
              uint64_t size, uint64_t perm);

#endif
