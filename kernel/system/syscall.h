#ifndef __SYSCALL_H__
#define __SYSCALL_H__

enum {
    SYS_exit = 0,
    SYS_yield = 1,
    SYS_open = 2,
    SYS_read = 3,
    SYS_write = 4,
    SYS_close = 5,
    SYS_lseek = 6,
    SYS_brk = 7,
    SYS_execve = 8,
    SYS_getpid = 9,
};

#endif
