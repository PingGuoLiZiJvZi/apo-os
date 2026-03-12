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

void syscall_handler(Context *c) {
    int syscall_id = c->GPR1;  // a7 = syscall number
    switch (syscall_id) {
        case SYS_yield:
            // Just return — the next timer interrupt will schedule
            break;
        case SYS_exit:
            printf("[syscall] exit(%ld)\n", c->GPR2);
            shutdown();
            break;
        default:
            printf("[syscall] unhandled syscall id=%d\n", syscall_id);
            c->GPRx = -1;
            break;
    }
}

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
