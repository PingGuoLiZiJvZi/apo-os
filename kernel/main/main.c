#include "../libc/stdio.h"
#include "../libc/string.h"
#include "../system/system.h"
#include "../memory/memory.h"
#include "../irq/context.h"
#include "../device/device.h"
#include "../disk/disk.h"
#include "../fs/fs.h"
#include "../proc/proc.h"
#include "../system/syscall.h"
#include "sbi.h"


void main(unsigned long hartid, const void *dtb)
{
    printf("Booting ApplePlumOrange OS...\n");
    printf("  Hart ID : %d\n", hartid);
    extern void trap();
    asm volatile("csrw stvec, %0" : : "r"((uint64_t)trap));

    init_memory();// initial memory manager 
    init_device();// initial device - UART, PLIC, CLINT
    init_disk();// initial disk
    init_fs();// initial filesystem
    fs_test();

    init_proc(); // creates kernel threads and directly switches — does not return
}
