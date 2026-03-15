#include "syscall.h"

#include "../device/device.h"
#include "../fs/fs.h"
#include "../libc/string.h"

#define USER_COPY_CHUNK 512
#define MAX_PATH_LEN 256
#define MAX_EXEC_ARGS 16
#define MAX_EXEC_ARG_LEN 64

#ifndef SEEK_SET
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#endif

typedef struct {
    long tv_sec;
    long tv_usec;
} KTimeVal;

static inline void user_access_begin(Context *c, uint64_t *old_satp, uint64_t *old_sstatus) {
    asm volatile("csrr %0, satp" : "=r"(*old_satp));
    asm volatile("csrw satp, %0" : : "r"((uint64_t)c->pdir));
    asm volatile("sfence.vma");
    asm volatile("csrr %0, sstatus" : "=r"(*old_sstatus));
    asm volatile("csrs sstatus, %0" : : "r"(1UL << 18));
}

static inline void user_access_end(uint64_t old_satp, uint64_t old_sstatus) {
    asm volatile("csrw sstatus, %0" : : "r"(old_sstatus));
    asm volatile("csrw satp, %0" : : "r"(old_satp));
    asm volatile("sfence.vma");
}

static int copy_from_user(Context *c, void *dst, uintptr_t src, size_t len) {
    uint64_t old_satp, old_sstatus;
    user_access_begin(c, &old_satp, &old_sstatus);
    memcpy(dst, (const void *)src, len);
    user_access_end(old_satp, old_sstatus);
    return 0;
}

static int copy_to_user(Context *c, uintptr_t dst, const void *src, size_t len) {
    uint64_t old_satp, old_sstatus;
    user_access_begin(c, &old_satp, &old_sstatus);
    memcpy((void *)dst, src, len);
    user_access_end(old_satp, old_sstatus);
    return 0;
}

static int copy_user_cstr(Context *c, char *dst, uintptr_t src, size_t max_len) {
    if (max_len == 0) return -1;
    for (size_t i = 0; i < max_len; i++) {
        if (copy_from_user(c, &dst[i], src + i, 1) < 0) return -1;
        if (dst[i] == '\0') return 0;
    }
    dst[max_len - 1] = '\0';
    return 0;
}

static int alloc_fd(PCB *pcb, File *f) {
    for (int i = 0; i < MAX_FD; i++) {
        if (pcb->fd_table[i] == 0) {
            pcb->fd_table[i] = f;
            return i;
        }
    }
    return -1;
}

static File *get_fd_file(PCB *pcb, int fd) {
    if (fd < 0 || fd >= MAX_FD) return 0;
    return pcb->fd_table[fd];
}

static int sys_open(Context *c) {
    char path[MAX_PATH_LEN];
    uintptr_t path_u = (uintptr_t)c->GPR2;
    if (path_u == 0) return -1;

    if (copy_user_cstr(c, path, path_u, sizeof(path)) < 0) return -1;

    File *f = fs_open(path);
    if (!f) return -1;

    int fd = alloc_fd(current_proc, f);
    if (fd < 0) {
        fs_close(f);
        return -1;
    }
    return fd;
}

static int sys_read(Context *c) {
    int fd = (int)c->GPR2;
    uintptr_t buf_u = (uintptr_t)c->GPR3;
    size_t len = (size_t)c->GPR4;
    File *f = get_fd_file(current_proc, fd);
    if (!f || buf_u == 0) return -1;

    char kbuf[USER_COPY_CHUNK];
    size_t total = 0;
    while (total < len) {
        size_t chunk = len - total;
        if (chunk > sizeof(kbuf)) chunk = sizeof(kbuf);

        int r = fs_read(f, kbuf, chunk);
        if (r <= 0) break;

        if (copy_to_user(c, buf_u + total, kbuf, (size_t)r) < 0) {
            return -1;
        }
        total += (size_t)r;
        if ((size_t)r < chunk) break;
    }
    return (int)total;
}

static int sys_write(Context *c) {
    int fd = (int)c->GPR2;
    uintptr_t buf = (uintptr_t)c->GPR3;
    size_t len = (size_t)c->GPR4;
    File *f = get_fd_file(current_proc, fd);
    if (!f || buf == 0) return -1;

    char kbuf[USER_COPY_CHUNK];
    size_t total = 0;
    while (total < len) {
        size_t chunk = len - total;
        if (chunk > sizeof(kbuf)) chunk = sizeof(kbuf);

        if (copy_from_user(c, kbuf, buf + total, chunk) < 0) return -1;

        int r = fs_write(f, kbuf, chunk);
        if (r <= 0) break;
        total += (size_t)r;
        if ((size_t)r < chunk) break;
    }

    return (int)total;
}

static int sys_close(Context *c) {
    int fd = (int)c->GPR2;
    if (fd < 0 || fd >= MAX_FD) return -1;
    File *f = current_proc->fd_table[fd];
    if (!f) return -1;

    fs_close(f);
    current_proc->fd_table[fd] = 0;
    return 0;
}

static long calc_seek_off(File *f, long offset, int whence) {
    long base = 0;
    switch (whence) {
        case SEEK_SET: base = 0; break;
        case SEEK_CUR: base = (long)f->off; break;
        case SEEK_END: base = (long)fs_filesize(f); break;
        default: return -1;
    }

    long target = base + offset;
    if (target < 0) return -1;
    return target;
}

static int sys_lseek(Context *c) {
    int fd = (int)c->GPR2;
    long offset = (long)c->GPR3;
    int whence = (int)c->GPR4;
    File *f = get_fd_file(current_proc, fd);
    if (!f) return -1;

    long new_off = calc_seek_off(f, offset, whence);
    if (new_off < 0) return -1;
    if (fs_seek(f, (uint32_t)new_off) < 0) return -1;
    return (int)new_off;
}

// Simple brk: grow the process heap by mapping new pages.
static int sys_brk(Context *c) {
    uintptr_t new_brk = (uintptr_t)c->GPR2;  // a0
    PCB *pcb = current_proc;

    uintptr_t brk_page = ROUNDUP(new_brk, PAGE_SIZE);
    uintptr_t cur_page = pcb->max_brk;

    for (uintptr_t a = cur_page; a < brk_page; a += PAGE_SIZE) {
        void *page = new_page(1);
        map(&pcb->as, (void *)a, page, 0);
    }

    if (brk_page > pcb->max_brk)
        pcb->max_brk = brk_page;

    return 0;  // success
}

static int sys_getpid(void) {
    if (current_proc >= &PCBs[0] && current_proc < &PCBs[MAX_PROCS]) {
        return (int)(current_proc - &PCBs[0]) + 1;
    }
    return 0;
}

static int sys_gettimeofday(Context *c) {
    uintptr_t tv_u = (uintptr_t)c->GPR2;
    if (tv_u == 0) return -1;

    uint64_t ticks = timer_get_time();
    KTimeVal tv;
    tv.tv_sec = (long)(ticks / 10000000UL);
    tv.tv_usec = (long)((ticks % 10000000UL) / 10UL);

    if (copy_to_user(c, tv_u, &tv, sizeof(tv)) < 0) return -1;
    return 0;
}

static Context *sys_execve(Context *c) {
    char path[MAX_PATH_LEN];
    char arg_store[MAX_EXEC_ARGS][MAX_EXEC_ARG_LEN];
    const char *kargv[MAX_EXEC_ARGS];
    const char *kenvp[1] = {0};

    uintptr_t path_u = (uintptr_t)c->GPR2;
    uintptr_t argv_u = (uintptr_t)c->GPR3;
    if (path_u == 0) {
        c->GPRx = -1;
        return c;
    }

    if (copy_user_cstr(c, path, path_u, sizeof(path)) < 0) {
        c->GPRx = -1;
        return c;
    }

    int argc = 0;
    if (argv_u != 0) {
        for (; argc < MAX_EXEC_ARGS - 1; argc++) {
            uintptr_t argp = 0;
            if (copy_from_user(c, &argp, argv_u + argc * sizeof(uintptr_t), sizeof(uintptr_t)) < 0) {
                c->GPRx = -1;
                return c;
            }
            if (argp == 0) break;
            if (copy_user_cstr(c, arg_store[argc], argp, MAX_EXEC_ARG_LEN) < 0) {
                c->GPRx = -1;
                return c;
            }
            kargv[argc] = arg_store[argc];
        }
    }

    if (argc == 0) {
        strncpy(arg_store[0], path, MAX_EXEC_ARG_LEN);
        arg_store[0][MAX_EXEC_ARG_LEN - 1] = '\0';
        kargv[0] = arg_store[0];
        argc = 1;
    }
    kargv[argc] = 0;

    proc_exec_reclaim(current_proc);
    context_uload(current_proc, path, kargv, kenvp);
    return current_proc->cp;
}

Context* syscall_handler(Context *c) {
    int syscall_id = c->GPR1;  
    switch (syscall_id) {
        case SYS_open:
            c->GPRx = sys_open(c);
            return c;
        case SYS_read:
            c->GPRx = sys_read(c);
            return c;
        case SYS_yield:
            return schedule(c);
        case SYS_exit:
            printf("exit(%ld)\n", c->GPR2);
            sys_exit(c->GPR2);
            return schedule(c);
        case SYS_write:
            c->GPRx = sys_write(c);
            return c;
        case SYS_close:
            c->GPRx = sys_close(c);
            return c;
        case SYS_lseek:
            c->GPRx = sys_lseek(c);
            return c;
        case SYS_brk:
            c->GPRx = sys_brk(c);
            return c;
        case SYS_execve:
            return sys_execve(c);
        case SYS_getpid:
            c->GPRx = sys_getpid();
            return c;
        case SYS_gettimeofday:
            c->GPRx = sys_gettimeofday(c);
            return c;
        default:
            printf("unhandled syscall id=%d\n", syscall_id);
            c->GPRx = -1;
            return c;
    }
}
