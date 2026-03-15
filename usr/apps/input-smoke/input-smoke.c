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

static void play_square_beep(int audiofd, int hz, int ms) {
  if (audiofd < 0 || hz <= 0 || ms <= 0) return;

  const int sample_rate = 44100;
  const int total_samples = (sample_rate * ms) / 1000;
  const int frames_per_chunk = 256;
  int16_t pcm[frames_per_chunk * 2];

  int half_period = sample_rate / (hz * 2);
  if (half_period < 1) half_period = 1;

  int phase_count = 0;
  int amp = 6000;
  int generated = 0;

  while (generated < total_samples) {
    int frames = total_samples - generated;
    if (frames > frames_per_chunk) frames = frames_per_chunk;

    for (int i = 0; i < frames; i++) {
      int16_t v = (phase_count < half_period) ? (int16_t)amp : (int16_t)-amp;
      pcm[i * 2] = v;
      pcm[i * 2 + 1] = v;
      phase_count++;
      if (phase_count >= half_period * 2) phase_count = 0;
    }

    write(audiofd, pcm, frames * 2 * (int)sizeof(int16_t));
    generated += frames;
  }
}

static int clampi(int v, int lo, int hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
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

  int audiofd = open("/device/audio", O_WRONLY, 0);
  if (audiofd < 0) {
    printf("[input-smoke] open /device/audio failed (audio disabled)\n");
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
  const int cursor = 8;
  int x = 10, y = 10;
  int vx = 3, vy = 2;
  int prev_x = x, prev_y = y;
  int mouse_x = screen_w / 2;
  int mouse_y = screen_h / 2;
  int prev_mouse_x = mouse_x;
  int prev_mouse_y = mouse_y;
  uint32_t black[24], red[24];
  uint32_t cblack[8], green[8];
  for (int i = 0; i < box; i++) {
    black[i] = 0x00000000;
    red[i] = 0x00ff3030;
  }
  for (int i = 0; i < cursor; i++) {
    cblack[i] = 0x00000000;
    green[i] = 0x0000ff00;
  }

  struct timeval last_tv;
  gettimeofday(&last_tv, 0);

  printf("[input-smoke] waiting keyboard/mouse events...\n");
  printf("[input-smoke] animating a red box on /device/fb (%dx%d)\n", screen_w, screen_h);
  if (audiofd >= 0) {
    printf("[input-smoke] playing startup beep via /device/audio\n");
    play_square_beep(audiofd, 660, 120);
  }
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
          if (b == 0) mouse_x += c;
          if (b == 1) mouse_y += c;
          mouse_x = clampi(mouse_x, 0, screen_w - cursor);
          mouse_y = clampi(mouse_y, 0, screen_h - cursor);
          printf("[input-smoke] mouse pos=(%d,%d) delta(%s=%+d)\n", mouse_x, mouse_y,
                 (b == 0) ? "x" : (b == 1) ? "y" : "?", c);
        } else if (a == 3) {
          if (b == 0) {
            mouse_x = (int)((long long)c * (screen_w - cursor) / 32767LL);
          } else if (b == 1) {
            mouse_y = (int)((long long)c * (screen_h - cursor) / 32767LL);
          }
          mouse_x = clampi(mouse_x, 0, screen_w - cursor);
          mouse_y = clampi(mouse_y, 0, screen_h - cursor);
          printf("[input-smoke] mouse pos=(%d,%d) abs(%s=%d)\n", mouse_x, mouse_y,
                 (b == 0) ? "x" : (b == 1) ? "y" : "?", c);
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
          if (audiofd >= 0 && (a == 28 || a == 57)) {
            play_square_beep(audiofd, (a == 57) ? 880 : 523, 90);
          }
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

      for (int row = 0; row < cursor; row++) {
        lseek(fbfd, ((prev_mouse_y + row) * screen_w + prev_mouse_x) * 4, SEEK_SET);
        write(fbfd, cblack, cursor * 4);
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

      for (int row = 0; row < cursor; row++) {
        lseek(fbfd, ((mouse_y + row) * screen_w + mouse_x) * 4, SEEK_SET);
        write(fbfd, green, cursor * 4);
      }

      prev_x = x;
      prev_y = y;
      prev_mouse_x = mouse_x;
      prev_mouse_y = mouse_y;
      last_tv = now;
    }
  }

  return 0;
}
