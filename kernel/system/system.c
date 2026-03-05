#include "system.h"
#include "../main/sbi.h"
#include "../libc/stdio.h"

void panic(const char *msg)
{
    printf("\n[PANIC] %s\n", msg);
    printf("[PANIC] System halted.\n");

    // 关闭中断
    asm volatile("csrci sstatus, 2");
    while (1)
        asm volatile("wfi");
}

void shutdown(void)
{
    printf("Shutting down...\n");
    sbi_system_reset(SBI_SRST_RESET_TYPE_SHUTDOWN,
                     SBI_SRST_RESET_REASON_NONE);

    while (1)
        asm volatile("wfi");
}

void reboot(void)
{
    printf("Rebooting...\n");
    sbi_system_reset(SBI_SRST_RESET_TYPE_COLD_REBOOT,
                     SBI_SRST_RESET_REASON_NONE);

    while (1)
        asm volatile("wfi");
}
