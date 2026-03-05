#include "../libc/stdio.h"
#include <libfdt.h>
#include "../system/system.h"
#include "../memory/memory.h"

// void init_memory(void);
// void init_device(void);
// void init_disk(void);
// void init_irq(void);
// void init_fs(void);
// void init_proc(void);

void main(unsigned long hartid, const void *dtb)
{
    extern void trap_vector(void);
    asm volatile("csrw stvec, %0" : : "r"((unsigned long)trap_vector));

    printf("Booting ApplePlumOrange OS...\n");
    printf("  Hart ID : %d\n", hartid);
    printf("  DTB addr: %p\n\n", dtb);

    int err = fdt_check_header(dtb);
    if (err != 0) {
        printf("FDT Error: Invalid device tree (libfdt error: %d)\n", err);
    } else {
        printf("FDT initialized successfully via libfdt.\n");
        printf("  Total Size: %d bytes\n", fdt_totalsize(dtb));
        printf("  Version: %d\n", fdt_version(dtb));
    }


    init_memory(dtb);// initial memory manager 

    init_device(dtb);// initial device - only uart in stage1
    // init_disk();// 初始化磁盘
    // init_irq();// 初始化中断
    // init_fs();// 初始化文件系统
    // init_proc();// 初始化进程

    // yield();

    for (int i = 0; i < 3; i++) {
        printf("Hello ApplePlumOrange\n");
    }

    shutdown();
}
