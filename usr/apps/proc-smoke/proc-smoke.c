#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>

int main() {
  printf("[proc-smoke] start, parent pid=%d\n", getpid());

  int st1 = -1;
  int child1 = fork();
  if (child1 < 0) {
    printf("[proc-smoke] fork #1 failed\n");
    return 1;
  }

  if (child1 == 0) {
    printf("[proc-smoke] child1 pid=%d sleep 1s then exit(42)\n", getpid());
    sleep(1);
    return 42;
  }

  int got1 = waitpid(child1, &st1, 0);
  printf("[proc-smoke] waitpid(child1=%d) -> pid=%d status=%d\n", child1, got1, st1);

  int st2 = -1;
  int child2 = fork();
  if (child2 < 0) {
    printf("[proc-smoke] fork #2 failed\n");
    return 1;
  }

  if (child2 == 0) {
    printf("[proc-smoke] child2 pid=%d looping, waiting to be killed\n", getpid());
    while (1) {
      sleep(1);
    }
  }

  sleep(1);
  int kr = kill(child2, 9);
  printf("[proc-smoke] kill(child2=%d, 9) -> %d\n", child2, kr);

  int got2 = waitpid(child2, &st2, 0);
  printf("[proc-smoke] waitpid(child2=%d) -> pid=%d status=%d\n", child2, got2, st2);

  printf("[proc-smoke] done\n");
  return 0;
}
