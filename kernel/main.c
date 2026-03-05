#include "libc/stdio.h"
#include "device/fdt.h"
#include "system/system.h"

void main(unsigned long hartid, const void *dtb)
{
    printf("Booting ApplePlumOrange OS...\n");
    printf("  Hart ID : %d\n", hartid);
    printf("  DTB addr: %p\n\n", dtb);

    if (fdt_check(dtb) != 0) {
        panic("Invalid device tree (bad magic)");
    }

    fdt_print(dtb);
    printf("\n");

    for (int i = 0; i < 3; i++) {
        printf("Hello ApplePlumOrange\n");
    }

    shutdown();
}
