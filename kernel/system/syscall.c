#include "syscall.h"

#include "../device/device.h"
#include "../fs/fs.h"
#include "../libc/string.h"

#define USER_COPY_CHUNK 4096
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

typedef struct {
    uint32_t inum;
    uint16_t type;
    uint32_t size;
    uint16_t nlink;
} KStatLite;

typedef struct {
    int fd;
    short events;
    short revents;
} KPollFd;

typedef struct {
    uintptr_t fds;
    uint64_t nfds;
    int timeout_ms;
} KPollReq;

typedef struct {
    int nfds;
    uintptr_t readfds;
    uintptr_t writefds;
    uintptr_t exceptfds;
    int timeout_ms;
} KSelectReq;

typedef struct {
    uintptr_t addr;
    uint64_t len;
    int prot;
    int flags;
    int fd;
    uint64_t offset;
} KMmapReq;

#define KPROT_READ  0x1
#define KPROT_WRITE 0x2
#define KPROT_EXEC  0x4

static uint64_t mmap_pte_perm(int prot, int shared) {
    uint64_t perm = PTE_U;
    if (prot == 0) {
        perm |= PTE_R | PTE_W;
    } else {
        if (prot & KPROT_READ) perm |= PTE_R;
        if (prot & KPROT_WRITE) perm |= PTE_R | PTE_W;
        if (prot & KPROT_EXEC) perm |= PTE_X;
    }
    if (shared) perm |= PTE_SHARED;
    return perm;
}

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

static int sys_fstat(Context *c) {
    int fd = (int)c->GPR2;
    uintptr_t st_u = (uintptr_t)c->GPR3;
    if (st_u == 0) return -1;

    File *f = get_fd_file(current_proc, fd);
    if (!f) return -1;

    Stat st;
    if (fs_stat_file(f, &st) < 0) return -1;

    KStatLite out;
    out.inum = st.inum;
    out.type = st.type;
    out.size = st.size;
    out.nlink = st.nlink;
    return copy_to_user(c, st_u, &out, sizeof(out));
}

static int sys_stat(Context *c) {
    char path[MAX_PATH_LEN];
    uintptr_t path_u = (uintptr_t)c->GPR2;
    uintptr_t st_u = (uintptr_t)c->GPR3;
    if (path_u == 0 || st_u == 0) return -1;

    if (copy_user_cstr(c, path, path_u, sizeof(path)) < 0) return -1;

    Stat st;
    if (fs_stat_path(path, &st) < 0) return -1;

    KStatLite out;
    out.inum = st.inum;
    out.type = st.type;
    out.size = st.size;
    out.nlink = st.nlink;
    return copy_to_user(c, st_u, &out, sizeof(out));
}

static int sys_ioctl(Context *c) {
    int fd = (int)c->GPR2;
    uint64_t req = (uint64_t)c->GPR3;
    uint64_t arg = (uint64_t)c->GPR4;
    File *f = get_fd_file(current_proc, fd);
    if (!f) return -1;

    int ret = fs_ioctl(f, req, arg);
    if (req == 0x541B && arg != 0 && ret >= 0) {
        int val = ret;
        if (copy_to_user(c, (uintptr_t)arg, &val, sizeof(val)) < 0) return -1;
        return 0;
    }
    return ret;
}

static int sys_pipe(Context *c) {
    uintptr_t pipefd_u = (uintptr_t)c->GPR2;
    if (pipefd_u == 0) return -1;

    File *r = 0;
    File *w = 0;
    if (fs_pipe_create(&r, &w) < 0) return -1;

    int rfd = alloc_fd(current_proc, r);
    if (rfd < 0) {
        fs_close(r);
        fs_close(w);
        return -1;
    }
    int wfd = alloc_fd(current_proc, w);
    if (wfd < 0) {
        current_proc->fd_table[rfd] = 0;
        fs_close(r);
        fs_close(w);
        return -1;
    }

    int out[2] = {rfd, wfd};
    if (copy_to_user(c, pipefd_u, out, sizeof(out)) < 0) {
        current_proc->fd_table[rfd] = 0;
        current_proc->fd_table[wfd] = 0;
        fs_close(r);
        fs_close(w);
        return -1;
    }
    return 0;
}

static int sys_dup(Context *c) {
    int oldfd = (int)c->GPR2;
    File *f = get_fd_file(current_proc, oldfd);
    if (!f) return -1;
    int newfd = alloc_fd(current_proc, f);
    if (newfd < 0) return -1;
    fs_dup(f);
    return newfd;
}

static int sys_dup2(Context *c) {
    int oldfd = (int)c->GPR2;
    int newfd = (int)c->GPR3;
    if (newfd < 0 || newfd >= MAX_FD) return -1;
    File *f = get_fd_file(current_proc, oldfd);
    if (!f) return -1;
    if (oldfd == newfd) return newfd;

    if (current_proc->fd_table[newfd]) {
        fs_close(current_proc->fd_table[newfd]);
        current_proc->fd_table[newfd] = 0;
    }
    fs_dup(f);
    current_proc->fd_table[newfd] = f;
    return newfd;
}

static int sys_poll(Context *c) {
    KPollReq req;
    if (copy_from_user(c, &req, (uintptr_t)c->GPR2, sizeof(req)) < 0) return -1;
    if (req.nfds > MAX_FD) req.nfds = MAX_FD;

    int ready = 0;
    for (uint64_t i = 0; i < req.nfds; i++) {
        KPollFd pfd;
        uintptr_t pfd_u = req.fds + i * sizeof(KPollFd);
        if (copy_from_user(c, &pfd, pfd_u, sizeof(pfd)) < 0) return -1;

        pfd.revents = 0;
        File *f = get_fd_file(current_proc, pfd.fd);
        if (f) {
            pfd.revents = (short)fs_poll_file(f, pfd.events);
            if (pfd.revents) ready++;
        }

        if (copy_to_user(c, pfd_u, &pfd, sizeof(pfd)) < 0) return -1;
    }
    return ready;
}

static int sys_select(Context *c) {
    KSelectReq req;
    if (copy_from_user(c, &req, (uintptr_t)c->GPR2, sizeof(req)) < 0) return -1;
    if (req.nfds < 0) return -1;
    if (req.nfds > MAX_FD) req.nfds = MAX_FD;

    uint64_t read_mask = 0, write_mask = 0;
    if (req.readfds) {
        if (copy_from_user(c, &read_mask, req.readfds, sizeof(read_mask)) < 0) return -1;
    }
    if (req.writefds) {
        if (copy_from_user(c, &write_mask, req.writefds, sizeof(write_mask)) < 0) return -1;
    }

    uint64_t out_read = 0, out_write = 0;
    int ready = 0;
    for (int fd = 0; fd < req.nfds; fd++) {
        File *f = get_fd_file(current_proc, fd);
        if (!f) continue;

        if (read_mask & (1UL << fd)) {
            int re = fs_poll_file(f, 0x001);
            if (re & 0x001) {
                out_read |= (1UL << fd);
                ready++;
            }
        }
        if (write_mask & (1UL << fd)) {
            int re = fs_poll_file(f, 0x004);
            if (re & 0x004) {
                out_write |= (1UL << fd);
                ready++;
            }
        }
    }

    if (req.readfds) {
        if (copy_to_user(c, req.readfds, &out_read, sizeof(out_read)) < 0) return -1;
    }
    if (req.writefds) {
        if (copy_to_user(c, req.writefds, &out_write, sizeof(out_write)) < 0) return -1;
    }
    if (req.exceptfds) {
        uint64_t zero = 0;
        if (copy_to_user(c, req.exceptfds, &zero, sizeof(zero)) < 0) return -1;
    }
    return ready;
}

static int64_t sys_mmap(Context *c) {
    KMmapReq req;
    if (copy_from_user(c, &req, (uintptr_t)c->GPR2, sizeof(req)) < 0) return -1;
    if (req.len == 0) return -1;
    if ((req.offset % PAGE_SIZE) != 0) return -1;

    uintptr_t map_len = ROUNDUP(req.len, PAGE_SIZE);
    if (map_len == 0) return -1;

    uintptr_t base = (uintptr_t)req.addr;
    if (base == 0) {
        if (current_proc->mmap_base == 0) {
            current_proc->mmap_base = (uintptr_t)current_proc->as.end - KSTACK_PAGENUM * PAGE_SIZE;
        }
        if (current_proc->mmap_base < map_len) return -1;
        base = (current_proc->mmap_base - map_len) & ~(uintptr_t)(PAGE_SIZE - 1);
        if (base < current_proc->max_brk || base < (uintptr_t)current_proc->as.start) return -1;
        current_proc->mmap_base = base;
    } else {
        base = ROUNDUP(base, PAGE_SIZE);
    }

    uintptr_t end = base + map_len;
    if (end <= base || end > (uintptr_t)current_proc->as.end) return -1;

    if (req.fd >= 0) {
        File *f = get_fd_file(current_proc, req.fd);
        if (!f) return -1;

        uint64_t size = fs_mmap_size(f);
        if (size == 0 || req.offset >= size) return -1;
        if (req.len > size - req.offset) return -1;

        uint64_t perm = mmap_pte_perm(req.prot, 1);
        uint64_t off = req.offset;
        for (uintptr_t va = base; va < end; va += PAGE_SIZE, off += PAGE_SIZE) {
            uint64_t pa = 0;
            if (fs_mmap_page(f, off, &pa) < 0) return -1;
            if (map_pages(current_proc->as.pgtable, va, pa, PAGE_SIZE, perm) != 0) return -1;
        }
    } else {
        uint64_t perm = mmap_pte_perm(req.prot, 0);
        for (uintptr_t va = base; va < end; va += PAGE_SIZE) {
            void *page = new_page(1);
            if (map_pages(current_proc->as.pgtable, va, (uint64_t)page, PAGE_SIZE, perm) != 0) return -1;
        }
    }

    return (int64_t)base;
}

static int sys_munmap(Context *c) {
    uintptr_t addr = (uintptr_t)c->GPR2;
    size_t len = (size_t)c->GPR3;
    if (addr == 0 || len == 0) return -1;

    uintptr_t base = addr & ~(uintptr_t)(PAGE_SIZE - 1);
    uintptr_t end = ROUNDUP(addr + len, PAGE_SIZE);
    if (end <= base) return -1;
    if (base < (uintptr_t)current_proc->as.start || end > (uintptr_t)current_proc->as.end) return -1;

    for (uintptr_t va = base; va < end; va += PAGE_SIZE) {
        uint64_t *pte = walk(current_proc->as.pgtable, va, 0);
        if (!pte || !(*pte & PTE_V)) continue;
        if (!(*pte & (PTE_R | PTE_W | PTE_X))) return -1;

        if (!(*pte & PTE_SHARED)) {
            kfree((void *)PTE2PA(*pte));
        }
        *pte = 0;
    }
    asm volatile("sfence.vma");
    return 0;
}

static int sys_kill(Context *c) {
    int pid = (int)c->GPR2;
    int sig = (int)c->GPR3;
    return proc_kill_pid(pid, sig);
}

static int sys_fork(Context *c) {
    return proc_fork_current(c);
}

static int sys_waitpid(Context *c) {
    int target_pid = (int)c->GPR2;
    uintptr_t status_u = (uintptr_t)c->GPR3;
    int options = (int)c->GPR4;

    int child_pid = -1;
    int exit_status = 0;
    int self_pid = sys_getpid();
    int ret = proc_try_waitpid(self_pid, target_pid, &child_pid, &exit_status);
    if (ret < 0) {
        return -1;
    }
    if (ret == 0) {
        if (options & 1) {
            return 0;
        }
        return -2;
    }

    if (status_u != 0) {
        if (copy_to_user(c, status_u, &exit_status, sizeof(exit_status)) < 0) {
            return -1;
        }
    }
    return child_pid;
}

static Context *sys_sleep(Context *c) {
    uint64_t seconds = (uint64_t)c->GPR2;
    if (seconds == 0) {
        c->GPRx = 0;
        return c;
    }

    proc_sleep_current(seconds);
    c->GPRx = 0;
    return schedule(c);
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
        case SYS_fstat:
            c->GPRx = sys_fstat(c);
            return c;
        case SYS_stat:
            c->GPRx = sys_stat(c);
            return c;
        case SYS_ioctl:
            c->GPRx = sys_ioctl(c);
            return c;
        case SYS_pipe:
            c->GPRx = sys_pipe(c);
            return c;
        case SYS_dup:
            c->GPRx = sys_dup(c);
            return c;
        case SYS_dup2:
            c->GPRx = sys_dup2(c);
            return c;
        case SYS_poll:
            c->GPRx = sys_poll(c);
            return c;
        case SYS_select:
            c->GPRx = sys_select(c);
            return c;
        case SYS_mmap:
            c->GPRx = sys_mmap(c);
            return c;
        case SYS_munmap:
            c->GPRx = sys_munmap(c);
            return c;
        case SYS_execve:
            return sys_execve(c);
        case SYS_fork:
            c->GPRx = sys_fork(c);
            return c;
        case SYS_wait:
            c->GPRx = sys_waitpid(c);
            return c;
        case SYS_sleep:
            return sys_sleep(c);
        case SYS_kill:
        {
            int r = sys_kill(c);
            if (r == 1) {
                return schedule(c);
            }
            c->GPRx = r;
            return c;
        }
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
