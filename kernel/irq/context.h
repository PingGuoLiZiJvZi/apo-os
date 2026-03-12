#ifndef CONTEXT_H
#define CONTEXT_H
#include<stdint.h>
#define ECALL_U    8
#define ECALL_S    9
#define IRQ_TIMER  0x8000000000000005UL
#define IRQ_UART   0x8000000000000009UL

typedef struct Context {
    uint64_t gprs[32];
    uint64_t scause;     
    uint64_t sstatus;    
    uint64_t sepc;       
    void* pdir;          // page directory
    uint64_t np;         // privilege level: 0=kernel, 1=user
} Context;

#define GPR1 gprs[17]
#define GPR2 gprs[10]
#define GPR3 gprs[11]
#define GPR4 gprs[12]
#define GPRx gprs[10]
#endif