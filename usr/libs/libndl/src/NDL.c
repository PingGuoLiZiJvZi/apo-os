#include <NDL.h>
#include <ndl_ipc.h>

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

static int evtdev = -1;
static int fbdev = -1;
static int audiodev = -1;
static int ipc_mode = 0;
static int ipc_evfd = -1;
static int ipc_drawfd = -1;
static uint8_t ipc_evbuf[4096];
static int ipc_evlen = 0;
static int screen_w = 0;
static int screen_h = 0;
static int canvas_w = 0;
static int canvas_h = 0;
static int audio_freq = 44100;
static int audio_channels = 2;
static int audio_pending = 0;
static uint64_t audio_last_us = 0;
static struct timeval start_time;

static int write_full(int fd, const void *buf, size_t len) {
  const char *p = (const char *)buf;
  size_t done = 0;
  while (done < len) {
    int n = write(fd, p + done, len - done);
    if (n < 0) return -1;
    if (n == 0) continue;
    done += (size_t)n;
  }
  return 0;
}

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

  char probe = 0;
  int probe_ret = read(3, &probe, 0);
  if (probe_ret >= 0) {
    ipc_mode = 1;
    ipc_evfd = 3;
    ipc_drawfd = 4;
    screen_w = 640;
    screen_h = 480;
    canvas_w = 640;
    canvas_h = 480;
    audio_last_us = now_us();
    printf("[ndl][ipc] enabled evfd=%d drawfd=%d\n", ipc_evfd, ipc_drawfd);
    return 0;
  }

  read_dispinfo();
  evtdev = open("/device/events", O_RDONLY, 0);
  fbdev = open("/device/fb", O_WRONLY, 0);
  audiodev = open("/device/audio", O_WRONLY, 0);
  audio_last_us = now_us();
  return 0;
}

void NDL_Quit(void) {
  ipc_mode = 0;
  ipc_evfd = -1;
  ipc_drawfd = -1;
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
  if (!buf || len <= 0) return 0;

  if (ipc_mode) {
    if (ipc_evfd < 0) return 0;
    while (ipc_evlen < (int)sizeof(ipc_evbuf)) {
      int n = read(ipc_evfd, ipc_evbuf + ipc_evlen, sizeof(ipc_evbuf) - ipc_evlen);
      if (n <= 0) break;
      ipc_evlen += n;
    }

    while (ipc_evlen >= (int)sizeof(NDLIPCMsg)) {
      NDLIPCMsg msg;
      memcpy(&msg, ipc_evbuf, sizeof(msg));
      if (msg.magic != NDL_IPC_MAGIC || msg.reserved != NDL_IPC_TAG) {
        memmove(ipc_evbuf, ipc_evbuf + 1, (size_t)(ipc_evlen - 1));
        ipc_evlen--;
        continue;
      }
      if (msg.type != NDL_IPC_W2C_EVENT || msg.w <= 0 || msg.w > (int)sizeof(ipc_evbuf)) {
        memmove(ipc_evbuf, ipc_evbuf + sizeof(NDLIPCMsg), (size_t)(ipc_evlen - (int)sizeof(NDLIPCMsg)));
        ipc_evlen -= (int)sizeof(NDLIPCMsg);
        continue;
      }

      int total = (int)sizeof(NDLIPCMsg) + msg.w;
      if (ipc_evlen < total) break;

      int copy_len = msg.w;
      if (copy_len > len - 1) copy_len = len - 1;
      memcpy(buf, ipc_evbuf + sizeof(NDLIPCMsg), (size_t)copy_len);
      buf[copy_len] = '\0';

      if (ipc_evlen > total) {
        memmove(ipc_evbuf, ipc_evbuf + total, (size_t)(ipc_evlen - total));
      }
      ipc_evlen -= total;
      printf("[ndl][ipc] recv event '%s' len=%d payload=%d\n", buf, copy_len, msg.w);
      return copy_len;
    }

    buf[0] = '\0';
    return 0;
  }

  if (evtdev < 0) return 0;
  int n = read(evtdev, buf, len - 1);
  if (n <= 0) {
    if (len > 0) buf[0] = '\0';
    return 0;
  }
  buf[n] = '\0';
  return n;
}

void NDL_OpenCanvas(int *w, int *h) {
  if (ipc_mode) {
    if (w && *w > 0) canvas_w = *w;
    if (h && *h > 0) canvas_h = *h;
    if (canvas_w <= 0) canvas_w = 640;
    if (canvas_h <= 0) canvas_h = 480;
    if (w) *w = canvas_w;
    if (h) *h = canvas_h;

    NDLIPCMsg hello;
    memset(&hello, 0, sizeof(hello));
    hello.magic = NDL_IPC_MAGIC;
    hello.reserved = NDL_IPC_TAG;
    hello.type = NDL_IPC_C2W_HELLO;
    hello.w = canvas_w;
    hello.h = canvas_h;
    printf("[ndl][ipc] hello canvas=%dx%d\n", canvas_w, canvas_h);
    write_full(ipc_drawfd, &hello, sizeof(hello));
    return;
  }

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
  if (!pixels || w <= 0 || h <= 0) return;

  if (ipc_mode) {
    if (ipc_drawfd < 0) return;
    const int max_chunk_pixels = 1000;
    uint8_t packet[sizeof(NDLIPCMsg) + (size_t)max_chunk_pixels * sizeof(uint32_t)];
    static int draw_log_cnt = 0;
    for (int row = 0; row < h; row++) {
      int col = 0;
      while (col < w) {
        int cw = w - col;
        if (cw > max_chunk_pixels) cw = max_chunk_pixels;

        NDLIPCMsg msg;
        memset(&msg, 0, sizeof(msg));
        msg.magic = NDL_IPC_MAGIC;
        msg.reserved = NDL_IPC_TAG;
        msg.type = NDL_IPC_C2W_DRAW;
        msg.x = x + col;
        msg.y = y + row;
        msg.w = cw;
        msg.h = 1;

        size_t bytes = (size_t)cw * sizeof(uint32_t);
        if (draw_log_cnt < 20) {
          printf("[ndl][ipc] draw #%d x=%d y=%d w=%d h=1 bytes=%lu\n",
                 draw_log_cnt, msg.x, msg.y, cw, (unsigned long)bytes);
          draw_log_cnt++;
        }

        memcpy(packet, &msg, sizeof(msg));
        memcpy(packet + sizeof(msg),
               pixels + (size_t)row * (size_t)w + (size_t)col,
               bytes);
        if (write_full(ipc_drawfd, packet, sizeof(msg) + bytes) < 0) return;
        col += cw;
      }
    }
    return;
  }

  if (fbdev < 0) return;
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
