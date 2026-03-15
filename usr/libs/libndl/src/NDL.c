#include <NDL.h>

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

static int evtdev = -1;
static int fbdev = -1;
static int audiodev = -1;
static int screen_w = 0;
static int screen_h = 0;
static int canvas_w = 0;
static int canvas_h = 0;
static int audio_freq = 44100;
static int audio_channels = 2;
static int audio_pending = 0;
static uint64_t audio_last_us = 0;
static struct timeval start_time;

static uint64_t now_us(void) {
  struct timeval tv;
  if (gettimeofday(&tv, 0) != 0) return 0;
  return (uint64_t)tv.tv_sec * 1000000ULL + (uint64_t)tv.tv_usec;
}

static void update_audio_pending(void) {
  uint64_t now = now_us();
  if (audio_last_us == 0) {
    audio_last_us = now;
    return;
  }
  if (now <= audio_last_us) return;

  uint64_t delta = now - audio_last_us;
  uint64_t bytes_per_sec = (uint64_t)audio_freq * (uint64_t)audio_channels * 2ULL;
  uint64_t consumed = delta * bytes_per_sec / 1000000ULL;
  if ((int)consumed > audio_pending) audio_pending = 0;
  else audio_pending -= (int)consumed;
  audio_last_us = now;
}

static void read_dispinfo(void) {
  int fd = open("/device/dispinfo", O_RDONLY, 0);
  if (fd < 0) return;
  char buf[64] = {0};
  int n = read(fd, buf, sizeof(buf) - 1);
  close(fd);
  if (n <= 0) return;
  buf[n] = '\0';
  int w = 0, h = 0;
  if (sscanf(buf, "WIDTH:%d\nHEIGHT:%d\n", &w, &h) == 2 && w > 0 && h > 0) {
    screen_w = w;
    screen_h = h;
  }
}

int NDL_Init(uint32_t flags) {
  (void)flags;
  gettimeofday(&start_time, 0);
  read_dispinfo();
  evtdev = open("/device/events", O_RDONLY, 0);
  fbdev = open("/device/fb", O_WRONLY, 0);
  audiodev = open("/device/audio", O_WRONLY, 0);
  audio_last_us = now_us();
  return 0;
}

void NDL_Quit(void) {
  if (evtdev >= 0) { close(evtdev); evtdev = -1; }
  if (fbdev >= 0) { close(fbdev); fbdev = -1; }
  if (audiodev >= 0) { close(audiodev); audiodev = -1; }
}

uint32_t NDL_GetTicks(void) {
  struct timeval now;
  gettimeofday(&now, 0);
  uint64_t elapsed = (uint64_t)(now.tv_sec - start_time.tv_sec) * 1000ULL +
                     (uint64_t)(now.tv_usec - start_time.tv_usec) / 1000ULL;
  return (uint32_t)elapsed;
}

int NDL_PollEvent(char *buf, int len) {
  if (!buf || len <= 0 || evtdev < 0) return 0;
  int n = read(evtdev, buf, len - 1);
  if (n <= 0) {
    if (len > 0) buf[0] = '\0';
    return 0;
  }
  buf[n] = '\0';
  return n;
}

void NDL_OpenCanvas(int *w, int *h) {
  read_dispinfo();
  if (screen_w <= 0 || screen_h <= 0) {
    screen_w = 640;
    screen_h = 480;
  }

  if (!w || !h) {
    canvas_w = screen_w;
    canvas_h = screen_h;
    return;
  }

  if (*w == 0 || *h == 0) {
    *w = screen_w;
    *h = screen_h;
  }
  if (*w > screen_w) *w = screen_w;
  if (*h > screen_h) *h = screen_h;
  canvas_w = *w;
  canvas_h = *h;
}

void NDL_DrawRect(uint32_t *pixels, int x, int y, int w, int h) {
  if (!pixels || w <= 0 || h <= 0 || fbdev < 0) return;
  if (screen_w <= 0 || screen_h <= 0) read_dispinfo();
  if (canvas_w <= 0 || canvas_h <= 0) {
    canvas_w = screen_w;
    canvas_h = screen_h;
  }

  int base_x = (screen_w - canvas_w) / 2;
  int base_y = (screen_h - canvas_h) / 2;
  int draw_x = x + base_x;
  int draw_y = y + base_y;

  for (int row = 0; row < h; row++) {
    int py = draw_y + row;
    if (py < 0 || py >= screen_h) continue;

    int px = draw_x;
    int copy_w = w;
    int src_off = row * w;
    if (px < 0) {
      src_off += -px;
      copy_w += px;
      px = 0;
    }
    if (px + copy_w > screen_w) copy_w = screen_w - px;
    if (copy_w <= 0) continue;

    off_t off = (off_t)((py * screen_w + px) * 4);
    if (lseek(fbdev, off, SEEK_SET) < 0) return;
    write(fbdev, &pixels[src_off], (size_t)copy_w * 4);
  }
}

void NDL_OpenAudio(int freq, int channels, int samples) {
  (void)samples;
  if (freq > 0) audio_freq = freq;
  if (channels > 0) audio_channels = channels;
  audio_pending = 0;
  audio_last_us = now_us();
}

void NDL_CloseAudio(void) {
  audio_pending = 0;
}

int NDL_PlayAudio(void *buf, int len) {
  if (!buf || len <= 0 || audiodev < 0) return 0;
  update_audio_pending();
  int n = write(audiodev, buf, (size_t)len);
  if (n > 0) audio_pending += n;
  return (n > 0) ? n : 0;
}

int NDL_QueryAudio(void) {
  update_audio_pending();
  return audio_pending;
}
