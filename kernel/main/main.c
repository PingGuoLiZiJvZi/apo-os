#include "../libc/stdio.h"
#include "../system/system.h"
#include "../memory/memory.h"
#include "../irq/context.h"
#include "../device/device.h"
#include "../disk/disk.h"

void syscall_handler(Context *c) { c; /* TODO */ }
void main(unsigned long hartid, const void *dtb)
{
    printf("Booting ApplePlumOrange OS...\n");
    printf("  Hart ID : %d\n", hartid);
    extern void trap();
    asm volatile("csrw stvec, %0" : : "r"((uint64_t)trap));

    init_memory();// initial memory manager 
    init_device();// initial device - UART, PLIC, CLINT
    init_disk();// initial disk
    disk_test();
    // init_fs();// 初始化文件系统
    // init_proc();// 初始化进程

    // yield();

    for (int i = 0; i < 3; i++) {
        printf("Hello ApplePlumOrange\n");
    }

    shutdown();
}
