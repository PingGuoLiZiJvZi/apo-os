#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/time.h>

int main() {
    printf("[smoke] begin syscall smoke test\n");

    int fd = open("/bin/hello-apple", O_RDONLY, 0);
    if (fd < 0) {
        printf("[smoke] open failed, fd=%d errno=%d\n", fd, errno);
        return 1;
    }
    printf("[smoke] open ok, fd=%d\n", fd);

    unsigned char buf[16];
    int n = read(fd, buf, sizeof(buf));
    printf("[smoke] read ret=%d\n", n);

    off_t off = lseek(fd, 0, SEEK_SET);
    printf("[smoke] lseek ret=%ld\n", (long)off);

    struct timeval tv;
    int gtod = gettimeofday(&tv, 0);
    printf("[smoke] gettimeofday ret=%d sec=%ld usec=%ld\n", gtod, (long)tv.tv_sec, (long)tv.tv_usec);

    close(fd);

    printf("[smoke] execve -> /bin/hello-orange\n");
    char *const argv[] = {"hello-orange", 0};
    char *const envp[] = {0};
    int ex = execve("/bin/hello-orange", argv, envp);

    printf("[smoke] execve returned=%d errno=%d\n", ex, errno);
    return 2;
}
