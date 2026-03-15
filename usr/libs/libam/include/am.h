#ifndef AM_H__
#define AM_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MMAP_NONE 0x00000000
#define MMAP_READ 0x00000001
#define MMAP_WRITE 0x00000002

typedef struct {
  void *start;
  void *end;
} Area;

typedef struct Context Context;

typedef struct {
  enum {
    EVENT_NULL = 0,
    EVENT_YIELD,
    EVENT_SYSCALL,
    EVENT_PAGEFAULT,
    EVENT_ERROR,
    EVENT_IRQ_TIMER,
    EVENT_IRQ_IODEV,
  } event;
  uintptr_t cause;
  uintptr_t ref;
  const char *msg;
} Event;

typedef struct {
  int pgsize;
  Area area;
  void *ptr;
} AddrSpace;

#ifdef __cplusplus
extern "C" {
#endif

extern Area heap;

void putch(char ch);
void halt(int code) __attribute__((__noreturn__));

bool ioe_init(void);
void ioe_read(int reg, void *buf);
void ioe_write(int reg, void *buf);

#include "amdev.h"

bool cte_init(Context *(*handler)(Event ev, Context *ctx));
void yield(void);
bool ienabled(void);
void iset(bool enable);
Context *kcontext(Area kstack, void (*entry)(void *), void *arg);

bool vme_init(void *(*pgalloc)(int), void (*pgfree)(void *));
void protect(AddrSpace *as);
void unprotect(AddrSpace *as);
void map(AddrSpace *as, void *vaddr, void *paddr, int prot);
Context *ucontext(AddrSpace *as, Area kstack, void *entry);

bool mpe_init(void (*entry)());
int cpu_count(void);
int cpu_current(void);
int atomic_xchg(int *addr, int newval);

#ifdef __cplusplus
}
#endif

#endif
