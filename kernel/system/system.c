#include "system.h"
#include "../main/sbi.h"
#include "../libc/stdio.h"
#include "../disk/disk.h"

void panic(const char *msg)
{
    printf("\n[PANIC] %s\n", msg);
    printf("[PANIC] System halted.\n");

    // 关闭中断
    asm volatile("csrci sstatus, 2");
    while (1)
        asm volatile("wfi");
}

void shutdown()
{
    printf("Shutting down...\n");
    disk_flush();
    sbi_system_reset(SBI_SRST_RESET_TYPE_SHUTDOWN,
                     SBI_SRST_RESET_REASON_NONE);

    while (1)
        asm volatile("wfi");
}

void reboot()
{
    printf("Rebooting...\n");
    disk_flush();
    sbi_system_reset(SBI_SRST_RESET_TYPE_COLD_REBOOT,
                     SBI_SRST_RESET_REASON_NONE);

    while (1)
        asm volatile("wfi");
}


void yield() {
    asm volatile("ecall");
}