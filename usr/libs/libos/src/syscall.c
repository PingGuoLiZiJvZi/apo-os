#include <unistd.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <assert.h>
#include <time.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <sys/select.h>
#include "syscall.h"
#include <errno.h>

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
	uint64_t fds;
	uint64_t nfds;
	int timeout_ms;
} KPollReq;

typedef struct {
	int nfds;
	uint64_t readfds;
	uint64_t writefds;
	uint64_t exceptfds;
	int timeout_ms;
} KSelectReq;

typedef struct {
	uint64_t addr;
	uint64_t len;
	int prot;
	int flags;
	int fd;
	uint64_t offset;
} KMmapReq;
// helper macros
#define _concat(x, y) x##y
#define concat(x, y) _concat(x, y)
#define _args(n, list) concat(_arg, n) list
#define _arg0(a0, ...) a0
#define _arg1(a0, a1, ...) a1
#define _arg2(a0, a1, a2, ...) a2
#define _arg3(a0, a1, a2, a3, ...) a3
#define _arg4(a0, a1, a2, a3, a4, ...) a4
#define _arg5(a0, a1, a2, a3, a4, a5, ...) a5

// extract an argument from the macro array
#define SYSCALL _args(0, ARGS_ARRAY)
#define GPR1 _args(1, ARGS_ARRAY)
#define GPR2 _args(2, ARGS_ARRAY)
#define GPR3 _args(3, ARGS_ARRAY)
#define GPR4 _args(4, ARGS_ARRAY)
#define GPRx _args(5, ARGS_ARRAY)

// ISA-depedent definitions
#if defined(__ISA_X86__)
#define ARGS_ARRAY ("int $0x80", "eax", "ebx", "ecx", "edx", "eax")
#elif defined(__ISA_MIPS32__)
#define ARGS_ARRAY ("syscall", "v0", "a0", "a1", "a2", "v0")
#elif defined(__riscv)
#ifdef __riscv_e
#define ARGS_ARRAY ("ecall", "a5", "a0", "a1", "a2", "a0")
#else
#define ARGS_ARRAY ("ecall", "a7", "a0", "a1", "a2", "a0")
#endif
#elif defined(__ISA_AM_NATIVE__)
#define ARGS_ARRAY ("call *0x100000", "rdi", "rsi", "rdx", "rcx", "rax")
#elif defined(__ISA_X86_64__)
#define ARGS_ARRAY ("int $0x80", "rdi", "rsi", "rdx", "rcx", "rax")
#elif defined(__ISA_LOONGARCH32R__)
#define ARGS_ARRAY ("syscall 0", "a7", "a0", "a1", "a2", "a0")
#else
#error _syscall_ is not implemented
#endif

intptr_t _syscall_(intptr_t type, intptr_t a0, intptr_t a1, intptr_t a2)
{
	register intptr_t _gpr1 asm(GPR1) = type;
	register intptr_t _gpr2 asm(GPR2) = a0;
	register intptr_t _gpr3 asm(GPR3) = a1;
	register intptr_t _gpr4 asm(GPR4) = a2;
	register intptr_t ret asm(GPRx);
	asm volatile(SYSCALL
	             : "=r"(ret)
	             : "r"(_gpr1), "r"(_gpr2), "r"(_gpr3), "r"(_gpr4)
	             : "memory");
	return ret;
}

void _exit(int status)
{
	_syscall_(SYS_exit, status, 0, 0);
	while (1)
		;
}

int _open(const char *path, int flags, mode_t mode)
{
	int fd = _syscall_(SYS_open, (intptr_t)path, flags, mode);
	return fd;
}

int _write(int fd, void *buf, size_t count)
{
	_syscall_(SYS_write, fd, (intptr_t)buf, count);
	return count;
}

void *_sbrk(intptr_t increment)
{
	extern char end;
	static intptr_t heap_end = (intptr_t)&end;
	intptr_t new_heap_end = heap_end + increment;

	size_t ret = _syscall_(SYS_brk, new_heap_end, 0, 0);
	if (ret == 0)
	{
		heap_end = new_heap_end;
		return (void *)(heap_end - increment);
	}
	return (void *)-1;
}

int _read(int fd, void *buf, size_t count)
{
	int bytes_read = _syscall_(SYS_read, fd, (intptr_t)buf, count);
	return bytes_read; // Return the number of bytes read
}

int _close(int fd)
{
	int ret = _syscall_(SYS_close, fd, 0, 0);
	return ret;
}

off_t _lseek(int fd, off_t offset, int whence)
{
	off_t off = _syscall_(SYS_lseek, fd, offset, whence);
	return off;
}

int _gettimeofday(struct timeval *tv, struct timezone *tz)
{
	size_t ret = _syscall_(SYS_gettimeofday, (intptr_t)tv, (intptr_t)tz, 0);
	return ret;
}

int _execve(const char *fname, char *const argv[], char *const envp[])
{
	int res = _syscall_(SYS_execve, (intptr_t)fname, (intptr_t)argv, (intptr_t)envp);
	errno = -res; // Set errno to the negative return value
	return -1;	  // This should not return
}

// Syscalls below are not used in Nanos-lite.
// But to pass linking, they are defined as dummy functions.

int _fstat(int fd, struct stat *buf)
{
	if (!buf)
		return -1;

	KStatLite st;
	int ret = _syscall_(SYS_fstat, fd, (intptr_t)&st, 0);
	if (ret < 0)
		return -1;

	memset(buf, 0, sizeof(*buf));
	buf->st_ino = st.inum;
	buf->st_nlink = st.nlink;
	buf->st_size = st.size;
	if (st.type == 1)
		buf->st_mode = S_IFDIR | 0755;
	else
		buf->st_mode = S_IFREG | 0644;
	return 0;
}

int _stat(const char *fname, struct stat *buf)
{
	if (!fname || !buf)
		return -1;

	KStatLite st;
	int ret = _syscall_(SYS_stat, (intptr_t)fname, (intptr_t)&st, 0);
	if (ret < 0)
		return -1;

	memset(buf, 0, sizeof(*buf));
	buf->st_ino = st.inum;
	buf->st_nlink = st.nlink;
	buf->st_size = st.size;
	if (st.type == 1)
		buf->st_mode = S_IFDIR | 0755;
	else
		buf->st_mode = S_IFREG | 0644;
	return 0;
}

int _kill(int pid, int sig)
{
	return _syscall_(SYS_kill, pid, sig, 0);
}

pid_t _getpid()
{
	return (pid_t)_syscall_(SYS_getpid, 0, 0, 0);
}

pid_t _fork()
{
	return (pid_t)_syscall_(SYS_fork, 0, 0, 0);
}

pid_t vfork()
{
	return _fork();
}

int _link(const char *d, const char *n)
{
	assert(0);
	return -1;
}

int _unlink(const char *n)
{
	assert(0);
	return -1;
}

pid_t _wait(int *status)
{
	for (;;)
	{
		int ret = _syscall_(SYS_wait, -1, (intptr_t)status, 0);
		if (ret == -2)
		{
			_syscall_(SYS_yield, 0, 0, 0);
			continue;
		}
		return (pid_t)ret;
	}
}

pid_t waitpid(pid_t pid, int *status, int options)
{
	for (;;)
	{
		int ret = _syscall_(SYS_wait, pid, (intptr_t)status, options);
		if (ret == -2)
		{
			_syscall_(SYS_yield, 0, 0, 0);
			continue;
		}
		return (pid_t)ret;
	}
}

clock_t _times(void *buf)
{
	assert(0);
	return 0;
}

int pipe(int pipefd[2])
{
	if (!pipefd)
		return -1;
	return _syscall_(SYS_pipe, (intptr_t)pipefd, 0, 0);
}

int dup(int oldfd)
{
	return _syscall_(SYS_dup, oldfd, 0, 0);
}

int dup2(int oldfd, int newfd)
{
	return _syscall_(SYS_dup2, oldfd, newfd, 0);
}

int _poll(void *fds, unsigned long nfds, int timeout)
{
	KPollReq req;
	req.fds = (uint64_t)(uintptr_t)fds;
	req.nfds = nfds;
	req.timeout_ms = timeout;
	return _syscall_(SYS_poll, (intptr_t)&req, 0, 0);
}

int poll(void *fds, unsigned long nfds, int timeout)
{
	return _poll(fds, nfds, timeout);
}

int _select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout)
{
	KSelectReq req;
	req.nfds = nfds;
	req.readfds = (uint64_t)(uintptr_t)readfds;
	req.writefds = (uint64_t)(uintptr_t)writefds;
	req.exceptfds = (uint64_t)(uintptr_t)exceptfds;
	req.timeout_ms = -1;
	if (timeout) {
		req.timeout_ms = (int)(timeout->tv_sec * 1000 + timeout->tv_usec / 1000);
	}
	return _syscall_(SYS_select, (intptr_t)&req, 0, 0);
}

int select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout)
{
	return _select(nfds, readfds, writefds, exceptfds, timeout);
}

void *mmap(void *addr, size_t len, int prot, int flags, int fd, off_t offset)
{
	KMmapReq req;
	req.addr = (uint64_t)(uintptr_t)addr;
	req.len = len;
	req.prot = prot;
	req.flags = flags;
	req.fd = fd;
	req.offset = (uint64_t)offset;
	intptr_t ret = _syscall_(SYS_mmap, (intptr_t)&req, 0, 0);
	if (ret < 0)
		return (void *)-1;
	return (void *)ret;
}

int munmap(void *addr, size_t len)
{
	return _syscall_(SYS_munmap, (intptr_t)addr, len, 0);
}

unsigned int sleep(unsigned int seconds)
{
	if (seconds == 0)
		return 0;

	int ret = _syscall_(SYS_sleep, seconds, 0, 0);
	if (ret < 0)
		return seconds;
	return 0;
}

ssize_t readlink(const char *pathname, char *buf, size_t bufsiz)
{
	assert(0);
	return -1;
}

int symlink(const char *target, const char *linkpath)
{
	assert(0);
	return -1;
}

int ioctl(int fd, unsigned long request, ...)
{
	va_list ap;
	va_start(ap, request);
	intptr_t arg = va_arg(ap, intptr_t);
	va_end(ap);
	return _syscall_(SYS_ioctl, fd, request, arg);
}
