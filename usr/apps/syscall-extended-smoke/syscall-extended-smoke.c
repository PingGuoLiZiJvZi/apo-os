#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/select.h>

#ifndef MAP_PRIVATE
#define MAP_PRIVATE 0x02
#endif
#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS 0x20
#endif

#define POLLIN 0x001
#define POLLOUT 0x004

typedef struct {
  int fd;
  short events;
  short revents;
} LocalPollFd;

extern int ioctl(int fd, unsigned long request, ...);
extern int _poll(void *fds, unsigned long nfds, int timeout);
extern void *mmap(void *addr, size_t len, int prot, int flags, int fd, off_t offset);
extern int munmap(void *addr, size_t len);

static void test_stat(void) {
  struct stat st;
  int r1 = stat("/bin/hello-orange", &st);
  int fd = open("/bin/hello-orange", 0, 0);
  int r2 = -1;
  struct stat st2;
  if (fd >= 0) {
    r2 = fstat(fd, &st2);
    close(fd);
  }
  printf("[ext] stat=%d fstat=%d size=%ld\n", r1, r2, (long)st.st_size);

  assert(r1 == 0);
  assert(fd >= 0);
  assert(r2 == 0);
  assert(st.st_size > 0);
  assert(st2.st_size == st.st_size);
}

static void test_pipe_dup(void) {
  int p[2] = {-1, -1};
  int pr = pipe(p);
  assert(pr == 0);

  int d = dup2(p[1], 10);
  assert(d == 10);
  const char *msg = "pipe-ok";
  int wn = write(d, (void *)msg, strlen(msg));
  assert(wn == (int)strlen(msg));

  char buf[16] = {0};
  int n = read(p[0], buf, sizeof(buf) - 1);
  printf("[ext] pipe/dup2 pr=%d dup2=%d read=%d buf=%s\n", pr, d, n, buf);

  assert(n == (int)strlen(msg));
  assert(strcmp(buf, msg) == 0);

  assert(close(p[0]) == 0);
  assert(close(p[1]) == 0);
  assert(close(d) == 0);
}

static void test_ioctl(void) {
  int r = ioctl(1, 0, 0);
  printf("[ext] ioctl(stdout,0)=%d\n", r);
  assert(r == 0);
}

static void test_select_poll(void) {
  int p[2] = {-1, -1};
  assert(pipe(p) == 0);

  fd_set rfds;
  FD_ZERO(&rfds);
  FD_SET(p[0], &rfds);
  struct timeval tv = {0, 0};
  int s0 = select(p[0] + 1, &rfds, NULL, NULL, &tv);
  assert(s0 == 0);

  const char *msg = "x";
  assert(write(p[1], (void *)msg, 1) == 1);

  FD_ZERO(&rfds);
  FD_SET(p[0], &rfds);
  int s1 = select(p[0] + 1, &rfds, NULL, NULL, &tv);
  assert(s1 == 1);

  LocalPollFd pf;
  pf.fd = p[0];
  pf.events = POLLIN;
  pf.revents = 0;
  int p1 = _poll(&pf, 1, 0);
  assert(p1 == 1);
  assert((pf.revents & POLLIN) != 0);

  char ch = 0;
  assert(read(p[0], &ch, 1) == 1);

  printf("[ext] select before=%d after=%d poll=%d revents=0x%x ch=%c\n", s0, s1, p1, (unsigned)pf.revents, ch ? ch : '?');
  assert(ch == 'x');

  assert(close(p[0]) == 0);
  assert(close(p[1]) == 0);
}

static void test_mmap(void) {
  void *p = mmap(NULL, 4096, 0, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  assert((intptr_t)p != -1);
  char *c = (char *)p;
  c[0] = 'O';
  c[1] = 'K';
  c[2] = 0;
  printf("[ext] mmap ptr=%p str=%s\n", p, c);
  assert(strcmp(c, "OK") == 0);
  int ur = munmap(p, 4096);
  printf("[ext] munmap=%d\n", ur);
  assert(ur == 0);
}

int main() {
  printf("[ext] syscall-extended-smoke start\n");
  test_stat();
  test_pipe_dup();
  test_ioctl();
  test_select_poll();
  test_mmap();
  printf("[ext] syscall-extended-smoke done\n");
  return 0;
}
