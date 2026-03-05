#ifndef __MEMORY_H__
#define __MEMORY_H__

#include <stdint.h>
#include <stddef.h>

#define PAGE_SIZE 4096

void init_memory(const void *dtb);
void *kalloc(void);
void kfree(void *pa);

#endif /* __MEMORY_H__ */ 