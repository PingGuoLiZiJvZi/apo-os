#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdint.h>
#include <sys/time.h>

static const char *key_name(int code) {
  switch (code) {
    case 17: return "W";
    case 30: return "A";
    case 31: return "S";
    case 32: return "D";
    case 57: return "SPACE";
    case 28: return "ENTER";
    case 1:  return "ESC";
    default: return "KEY";
  }
}

static const char *btn_name(int code) {
  switch (code) {
    case 272: return "BTN_LEFT";
    case 273: return "BTN_RIGHT";
    case 274: return "BTN_MIDDLE";
    default: return "BTN";
  }
}

int main() {
  int evfd = open("/device/events", O_RDONLY, 0);
  if (evfd < 0) {
    printf("[input-smoke] open /device/events failed\n");
    return 1;
  }

  int fbfd = open("/device/fb", O_WRONLY, 0);
  if (fbfd < 0) {
    printf("[input-smoke] open /device/fb failed\n");
    return 1;
  }

  int infofd = open("/device/dispinfo", O_RDONLY, 0);
  int screen_w = 640, screen_h = 480;
  if (infofd >= 0) {
    char ibuf[64] = {0};
    int n = read(infofd, ibuf, sizeof(ibuf) - 1);
    if (n > 0) {
      ibuf[n] = '\0';
      sscanf(ibuf, "WIDTH:%d\nHEIGHT:%d\n", &screen_w, &screen_h);
    }
    close(infofd);
  }

  const int box = 24;
  int x = 10, y = 10;
  int vx = 3, vy = 2;
  int prev_x = x, prev_y = y;
  uint32_t black[24], red[24];
  for (int i = 0; i < box; i++) {
    black[i] = 0x00000000;
    red[i] = 0x00ff3030;
  }

  struct timeval last_tv;
  gettimeofday(&last_tv, 0);

  printf("[input-smoke] waiting keyboard/mouse events...\n");
  printf("[input-smoke] animating a red box on /device/fb (%dx%d)\n", screen_w, screen_h);
  char buf[64];
  while (1) {
    int n = read(evfd, buf, sizeof(buf) - 1);
    if (n > 0) {
      buf[n] = '\0';

      int a = 0, b = 0, c = 0;
      if (sscanf(buf, "m %d %d %d", &a, &b, &c) == 3) {
        if (a == 0 && b == 0 && c == 0) {
          continue;
        }
        if (a == 2) {
          const char *axis = (b == 0) ? "REL_X" : (b == 1) ? "REL_Y" : "REL";
          printf("[input-smoke] mouse %s %+d\n", axis, c);
        } else {
          printf("[input-smoke] mouse type=%d code=%d value=%d\n", a, b, c);
        }
        continue;
      }

      if (sscanf(buf, "kd %d", &a) == 1) {
        if (a >= 272) {
          printf("[input-smoke] mouse down %s(%d)\n", btn_name(a), a);
        } else {
          printf("[input-smoke] key down %s(%d)\n", key_name(a), a);
        }
        continue;
      }

      if (sscanf(buf, "ku %d", &a) == 1) {
        if (a >= 272) {
          printf("[input-smoke] mouse up %s(%d)\n", btn_name(a), a);
        } else {
          printf("[input-smoke] key up %s(%d)\n", key_name(a), a);
        }
        continue;
      }

      if (strlen(buf) > 0) {
        printf("[input-smoke] raw %s", buf);
      }
    }

    struct timeval now;
    gettimeofday(&now, 0);
    long dt_ms = (now.tv_sec - last_tv.tv_sec) * 1000L + (now.tv_usec - last_tv.tv_usec) / 1000L;
    if (dt_ms >= 16) {
      for (int row = 0; row < box; row++) {
        lseek(fbfd, ((prev_y + row) * screen_w + prev_x) * 4, SEEK_SET);
        write(fbfd, black, box * 4);
      }

      x += vx;
      y += vy;
      if (x < 0) { x = 0; vx = -vx; }
      if (y < 0) { y = 0; vy = -vy; }
      if (x + box >= screen_w) { x = screen_w - box; vx = -vx; }
      if (y + box >= screen_h) { y = screen_h - box; vy = -vy; }

      for (int row = 0; row < box; row++) {
        lseek(fbfd, ((y + row) * screen_w + x) * 4, SEEK_SET);
        write(fbfd, red, box * 4);
      }

      prev_x = x;
      prev_y = y;
      last_tv = now;
    }
  }

  return 0;
}
