#include <am.h>

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

Area heap = {0};

#define KEYDOWN_MASK 0x8000
#define KEY_QUEUE_LEN 128
#define GPU_DIRTY_QUEUE_LEN 64

typedef struct {
  int32_t x;
  int32_t y;
  int32_t w;
  int32_t h;
} GpuDirtyRect;

static int g_inited = 0;
static int g_evfd = -1;
static int g_fbfd = -1;
static int g_fbsyncfd = -1;
static int g_audiofd = -1;
static int g_serialfd = -1;
static int g_screen_w = 640;
static int g_screen_h = 480;
static uint32_t *g_fbmem = NULL;
static size_t g_fbmem_size = 0;
static GpuDirtyRect g_dirty_rects[GPU_DIRTY_QUEUE_LEN];
static int g_dirty_count = 0;
static uint64_t g_boot_us = 0;
static int g_key_queue[KEY_QUEUE_LEN];
static int g_key_head = 0;
static int g_key_tail = 0;

static int g_audio_freq = 44100;
static int g_audio_channels = 2;
static int g_audio_samples = 1024;
static int g_audio_pending_bytes = 0;
static uint64_t g_audio_last_us = 0;

static uint64_t now_us(void);
static int linux_keycode_to_am(int code);

static void audio_update_pending(void) {
  uint64_t now = now_us();
  if (g_audio_last_us == 0) {
    g_audio_last_us = now;
    return;
  }

  if (now <= g_audio_last_us) return;

  uint64_t delta = now - g_audio_last_us;
  uint64_t bytes_per_sec = (uint64_t)g_audio_freq * (uint64_t)g_audio_channels * 2ULL;
  uint64_t consumed = (delta * bytes_per_sec) / 1000000ULL;
  if ((int)consumed > g_audio_pending_bytes) {
    g_audio_pending_bytes = 0;
  } else {
    g_audio_pending_bytes -= (int)consumed;
  }
  g_audio_last_us = now;
}

static int keyq_empty(void) {
  return g_key_head == g_key_tail;
}

static void keyq_push(int value) {
  int next_tail = (g_key_tail + 1) % KEY_QUEUE_LEN;
  if (next_tail == g_key_head) return;
  g_key_queue[g_key_tail] = value;
  g_key_tail = next_tail;
}

static int keyq_pop(void) {
  if (keyq_empty()) return AM_KEY_NONE;
  int value = g_key_queue[g_key_head];
  g_key_head = (g_key_head + 1) % KEY_QUEUE_LEN;
  return value;
}

static void pump_input_events(void) {
  if (g_evfd < 0) return;

  for (int i = 0; i < 32; i++) {
    char ev[64] = {0};
    int n = read(g_evfd, ev, sizeof(ev) - 1);
    if (n <= 0) break;
    ev[n] = '\0';

    int code = 0;
    if (sscanf(ev, "kd %d", &code) == 1) {
      int am_code = linux_keycode_to_am(code);
      if (am_code != AM_KEY_NONE) keyq_push(am_code | KEYDOWN_MASK);
      continue;
    }
    if (sscanf(ev, "ku %d", &code) == 1) {
      int am_code = linux_keycode_to_am(code);
      if (am_code != AM_KEY_NONE) keyq_push(am_code);
      continue;
    }
  }
}

static uint64_t now_us(void) {
  struct timeval tv;
  if (gettimeofday(&tv, 0) != 0) return 0;
  return (uint64_t)tv.tv_sec * 1000000ULL + (uint64_t)tv.tv_usec;
}

static int linux_keycode_to_am(int code) {
  switch (code) {
    case 1: return AM_KEY_ESCAPE;
    case 2: return AM_KEY_1;
    case 3: return AM_KEY_2;
    case 4: return AM_KEY_3;
    case 5: return AM_KEY_4;
    case 6: return AM_KEY_5;
    case 7: return AM_KEY_6;
    case 8: return AM_KEY_7;
    case 9: return AM_KEY_8;
    case 10: return AM_KEY_9;
    case 11: return AM_KEY_0;
    case 12: return AM_KEY_MINUS;
    case 13: return AM_KEY_EQUALS;
    case 14: return AM_KEY_BACKSPACE;
    case 15: return AM_KEY_TAB;
    case 16: return AM_KEY_Q;
    case 17: return AM_KEY_W;
    case 18: return AM_KEY_E;
    case 19: return AM_KEY_R;
    case 20: return AM_KEY_T;
    case 21: return AM_KEY_Y;
    case 22: return AM_KEY_U;
    case 23: return AM_KEY_I;
    case 24: return AM_KEY_O;
    case 25: return AM_KEY_P;
    case 26: return AM_KEY_LEFTBRACKET;
    case 27: return AM_KEY_RIGHTBRACKET;
    case 28: return AM_KEY_RETURN;
    case 29: return AM_KEY_LCTRL;
    case 30: return AM_KEY_A;
    case 31: return AM_KEY_S;
    case 32: return AM_KEY_D;
    case 33: return AM_KEY_F;
    case 34: return AM_KEY_G;
    case 35: return AM_KEY_H;
    case 36: return AM_KEY_J;
    case 37: return AM_KEY_K;
    case 38: return AM_KEY_L;
    case 39: return AM_KEY_SEMICOLON;
    case 40: return AM_KEY_APOSTROPHE;
    case 41: return AM_KEY_GRAVE;
    case 42: return AM_KEY_LSHIFT;
    case 43: return AM_KEY_BACKSLASH;
    case 44: return AM_KEY_Z;
    case 45: return AM_KEY_X;
    case 46: return AM_KEY_C;
    case 47: return AM_KEY_V;
    case 48: return AM_KEY_B;
    case 49: return AM_KEY_N;
    case 50: return AM_KEY_M;
    case 51: return AM_KEY_COMMA;
    case 52: return AM_KEY_PERIOD;
    case 53: return AM_KEY_SLASH;
    case 54: return AM_KEY_RSHIFT;
    case 56: return AM_KEY_LALT;
    case 57: return AM_KEY_SPACE;
    case 58: return AM_KEY_CAPSLOCK;
    case 59: return AM_KEY_F1;
    case 60: return AM_KEY_F2;
    case 61: return AM_KEY_F3;
    case 62: return AM_KEY_F4;
    case 63: return AM_KEY_F5;
    case 64: return AM_KEY_F6;
    case 65: return AM_KEY_F7;
    case 66: return AM_KEY_F8;
    case 67: return AM_KEY_F9;
    case 68: return AM_KEY_F10;
    case 87: return AM_KEY_F11;
    case 88: return AM_KEY_F12;
    case 97: return AM_KEY_RCTRL;
    case 100: return AM_KEY_RALT;
    case 102: return AM_KEY_HOME;
    case 103: return AM_KEY_UP;
    case 104: return AM_KEY_PAGEUP;
    case 105: return AM_KEY_LEFT;
    case 106: return AM_KEY_RIGHT;
    case 107: return AM_KEY_END;
    case 108: return AM_KEY_DOWN;
    case 109: return AM_KEY_PAGEDOWN;
    case 110: return AM_KEY_INSERT;
    case 111: return AM_KEY_DELETE;
    case 117: return AM_KEY_APPLICATION;
    default: return AM_KEY_NONE;
  }
}

static void parse_dispinfo(void) {
  int infofd = open("/device/dispinfo", O_RDONLY, 0);
  if (infofd < 0) return;

  char buf[64] = {0};
  int n = read(infofd, buf, sizeof(buf) - 1);
  if (n > 0) {
    buf[n] = '\0';
    int w = 0, h = 0;
    if (sscanf(buf, "WIDTH:%d\nHEIGHT:%d\n", &w, &h) == 2 && w > 0 && h > 0) {
      g_screen_w = w;
      g_screen_h = h;
    }
  }
  close(infofd);
}

static int gpu_rect_x2(const GpuDirtyRect *r) {
  return r->x + r->w;
}

static int gpu_rect_y2(const GpuDirtyRect *r) {
  return r->y + r->h;
}

static int gpu_rect_contains(const GpuDirtyRect *a, const GpuDirtyRect *b) {
  return a->x <= b->x && a->y <= b->y &&
         gpu_rect_x2(a) >= gpu_rect_x2(b) && gpu_rect_y2(a) >= gpu_rect_y2(b);
}

static int gpu_rect_touch_or_overlap(const GpuDirtyRect *a, const GpuDirtyRect *b) {
  return a->x <= gpu_rect_x2(b) && gpu_rect_x2(a) >= b->x &&
         a->y <= gpu_rect_y2(b) && gpu_rect_y2(a) >= b->y;
}

static void gpu_rect_union_into(GpuDirtyRect *dst, const GpuDirtyRect *src) {
  int x1 = dst->x < src->x ? dst->x : src->x;
  int y1 = dst->y < src->y ? dst->y : src->y;
  int x2 = gpu_rect_x2(dst) > gpu_rect_x2(src) ? gpu_rect_x2(dst) : gpu_rect_x2(src);
  int y2 = gpu_rect_y2(dst) > gpu_rect_y2(src) ? gpu_rect_y2(dst) : gpu_rect_y2(src);
  dst->x = x1;
  dst->y = y1;
  dst->w = x2 - x1;
  dst->h = y2 - y1;
}

static void gpu_dirty_collapse(void) {
  if (g_dirty_count <= 1) return;
  for (int i = 1; i < g_dirty_count; i++) {
    gpu_rect_union_into(&g_dirty_rects[0], &g_dirty_rects[i]);
  }
  g_dirty_count = 1;
}

static void gpu_dirty_add(int x, int y, int w, int h) {
  int x1 = x;
  int y1 = y;
  int x2 = x + w;
  int y2 = y + h;
  if (x1 < 0) x1 = 0;
  if (y1 < 0) y1 = 0;
  if (x2 > g_screen_w) x2 = g_screen_w;
  if (y2 > g_screen_h) y2 = g_screen_h;
  if (x1 >= x2 || y1 >= y2) return;

  GpuDirtyRect nr = { x1, y1, x2 - x1, y2 - y1 };
  for (int i = 0; i < g_dirty_count; i++) {
    if (gpu_rect_contains(&g_dirty_rects[i], &nr)) return;
    if (gpu_rect_contains(&nr, &g_dirty_rects[i]) ||
        gpu_rect_touch_or_overlap(&g_dirty_rects[i], &nr)) {
      gpu_rect_union_into(&g_dirty_rects[i], &nr);
      for (int j = 0; j < g_dirty_count; j++) {
        if (j == i) continue;
        if (!gpu_rect_touch_or_overlap(&g_dirty_rects[i], &g_dirty_rects[j])) continue;
        gpu_rect_union_into(&g_dirty_rects[i], &g_dirty_rects[j]);
        g_dirty_rects[j] = g_dirty_rects[g_dirty_count - 1];
        g_dirty_count--;
        j--;
      }
      return;
    }
  }

  if (g_dirty_count < GPU_DIRTY_QUEUE_LEN) {
    g_dirty_rects[g_dirty_count++] = nr;
    return;
  }

  gpu_rect_union_into(&g_dirty_rects[0], &nr);
  gpu_dirty_collapse();
}

static void gpu_map_fb(void) {
  if (g_fbmem || g_fbfd < 0 || g_screen_w <= 0 || g_screen_h <= 0) return;
  size_t size = (size_t)g_screen_w * (size_t)g_screen_h * sizeof(uint32_t);
  void *p = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, g_fbfd, 0);
  if (p == MAP_FAILED) return;
  g_fbmem = (uint32_t *)p;
  g_fbmem_size = size;
}

static void gpu_sync(void) {
  if (g_fbsyncfd < 0) return;
  if (g_dirty_count > 0) {
    size_t bytes = (size_t)g_dirty_count * sizeof(g_dirty_rects[0]);
    int n = write(g_fbsyncfd, g_dirty_rects, bytes);
    if (n == (int)bytes) g_dirty_count = 0;
    return;
  }
  char ch = 0;
  write(g_fbsyncfd, &ch, 1);
}

void putch(char ch) {
  write(1, &ch, 1);
}

void halt(int code) {
  _exit(code);
}

bool ioe_init(void) {
  if (g_inited) return true;

  g_evfd = open("/device/events", O_RDONLY, 0);
  g_fbfd = open("/device/fb", O_RDWR, 0);
  g_fbsyncfd = open("/device/fbsync", O_WRONLY, 0);
  g_audiofd = open("/device/audio", O_WRONLY, 0);
  g_serialfd = open("/device/serial", O_RDONLY, 0);

  parse_dispinfo();
  gpu_map_fb();
  g_boot_us = now_us();
  g_audio_last_us = g_boot_us;
  g_key_head = g_key_tail = 0;
  g_inited = 1;
  return true;
}

static void am_uart_config(void *buf) {
  AM_UART_CONFIG_T *cfg = (AM_UART_CONFIG_T *)buf;
  cfg->present = true;
}

static void am_uart_tx(void *buf) {
  AM_UART_TX_T *tx = (AM_UART_TX_T *)buf;
  write(1, &tx->data, 1);
}

static void am_uart_rx(void *buf) {
  AM_UART_RX_T *rx = (AM_UART_RX_T *)buf;
  rx->data = 0;
  if (g_serialfd < 0) return;
  char c = 0;
  if (read(g_serialfd, &c, 1) == 1) rx->data = c;
}

static void am_timer_config(void *buf) {
  AM_TIMER_CONFIG_T *cfg = (AM_TIMER_CONFIG_T *)buf;
  cfg->present = true;
  cfg->has_rtc = true;
}

static void am_timer_rtc(void *buf) {
  AM_TIMER_RTC_T *rtc = (AM_TIMER_RTC_T *)buf;
  time_t t = time(0);
  struct tm *tmv = localtime(&t);
  if (!tmv) {
    rtc->year = rtc->month = rtc->day = 0;
    rtc->hour = rtc->minute = rtc->second = 0;
    return;
  }

  rtc->year = tmv->tm_year + 1900;
  rtc->month = tmv->tm_mon + 1;
  rtc->day = tmv->tm_mday;
  rtc->hour = tmv->tm_hour;
  rtc->minute = tmv->tm_min;
  rtc->second = tmv->tm_sec;
}

static void am_timer_uptime(void *buf) {
  AM_TIMER_UPTIME_T *uptime = (AM_TIMER_UPTIME_T *)buf;
  uint64_t now = now_us();
  uptime->us = (now >= g_boot_us) ? (now - g_boot_us) : 0;
}

static void am_input_config(void *buf) {
  AM_INPUT_CONFIG_T *cfg = (AM_INPUT_CONFIG_T *)buf;
  cfg->present = (g_evfd >= 0);
}

static void am_input_keybrd(void *buf) {
  AM_INPUT_KEYBRD_T *kbd = (AM_INPUT_KEYBRD_T *)buf;
  kbd->keydown = false;
  kbd->keycode = AM_KEY_NONE;

  pump_input_events();

  int value = keyq_pop();
  if (value == AM_KEY_NONE) return;

  kbd->keydown = ((value & KEYDOWN_MASK) != 0);
  kbd->keycode = (value & ~KEYDOWN_MASK);
}

static void am_gpu_config(void *buf) {
  AM_GPU_CONFIG_T *cfg = (AM_GPU_CONFIG_T *)buf;
  cfg->present = (g_fbfd >= 0);
  cfg->has_accel = false;
  cfg->width = g_screen_w;
  cfg->height = g_screen_h;
  cfg->vmemsz = g_screen_w * g_screen_h * 4;
}

static void am_gpu_status(void *buf) {
  AM_GPU_STATUS_T *st = (AM_GPU_STATUS_T *)buf;
  st->ready = true;
}

static void am_gpu_fbdraw(void *buf) {
  AM_GPU_FBDRAW_T *ctl = (AM_GPU_FBDRAW_T *)buf;
  if (g_fbfd >= 0 && ctl->pixels && ctl->w > 0 && ctl->h > 0) {
    gpu_map_fb();

    int px = ctl->x;
    int py = ctl->y;
    int copy_w = ctl->w;
    int copy_h = ctl->h;
    int src_x = 0;
    int src_y = 0;
    if (px < 0) {
      src_x = -px;
      copy_w += px;
      px = 0;
    }
    if (py < 0) {
      src_y = -py;
      copy_h += py;
      py = 0;
    }
    if (px + copy_w > g_screen_w) copy_w = g_screen_w - px;
    if (py + copy_h > g_screen_h) copy_h = g_screen_h - py;

    if (copy_w > 0 && copy_h > 0) {
      uint32_t *pixels = (uint32_t *)ctl->pixels;
      if (g_fbmem) {
        for (int row = 0; row < copy_h; row++) {
          uint32_t *dst = g_fbmem + (py + row) * g_screen_w + px;
          uint32_t *src = pixels + (src_y + row) * ctl->w + src_x;
          memcpy(dst, src, (size_t)copy_w * sizeof(uint32_t));
        }
        gpu_dirty_add(px, py, copy_w, copy_h);
      } else {
        for (int row = 0; row < copy_h; row++) {
          off_t off = (off_t)(((py + row) * g_screen_w + px) * 4);
          if (lseek(g_fbfd, off, SEEK_SET) < 0) return;
          uint32_t *src = pixels + (src_y + row) * ctl->w + src_x;
          write(g_fbfd, src, (size_t)copy_w * sizeof(uint32_t));
        }
      }
    }
  }

  if (ctl->sync) gpu_sync();
}

static void am_audio_config(void *buf) {
  AM_AUDIO_CONFIG_T *cfg = (AM_AUDIO_CONFIG_T *)buf;
  cfg->present = (g_audiofd >= 0);
  cfg->bufsize = 65536;
}

static void am_audio_ctrl(void *buf) {
  AM_AUDIO_CTRL_T *ctrl = (AM_AUDIO_CTRL_T *)buf;
  if (ctrl->freq > 0) g_audio_freq = ctrl->freq;
  if (ctrl->channels > 0) g_audio_channels = ctrl->channels;
  if (ctrl->samples > 0) g_audio_samples = ctrl->samples;
  g_audio_pending_bytes = 0;
  g_audio_last_us = now_us();
}

static void am_audio_status(void *buf) {
  AM_AUDIO_STATUS_T *st = (AM_AUDIO_STATUS_T *)buf;
  audio_update_pending();
  st->count = g_audio_pending_bytes;
}

static void am_audio_play(void *buf) {
  AM_AUDIO_PLAY_T *play = (AM_AUDIO_PLAY_T *)buf;
  if (g_audiofd < 0 || !play->buf.start || !play->buf.end) return;
  audio_update_pending();
  size_t len = (size_t)((uintptr_t)play->buf.end - (uintptr_t)play->buf.start);
  if ((intptr_t)len <= 0) return;
  int written = write(g_audiofd, play->buf.start, len);
  if (written > 0) g_audio_pending_bytes += written;
}

void ioe_read(int reg, void *buf) {
  if (!g_inited) ioe_init();

  switch (reg) {
    case AM_UART_CONFIG: am_uart_config(buf); break;
    case AM_UART_RX: am_uart_rx(buf); break;
    case AM_TIMER_CONFIG: am_timer_config(buf); break;
    case AM_TIMER_RTC: am_timer_rtc(buf); break;
    case AM_TIMER_UPTIME: am_timer_uptime(buf); break;
    case AM_INPUT_CONFIG: am_input_config(buf); break;
    case AM_INPUT_KEYBRD: am_input_keybrd(buf); break;
    case AM_GPU_CONFIG: am_gpu_config(buf); break;
    case AM_GPU_STATUS: am_gpu_status(buf); break;
    case AM_AUDIO_CONFIG: am_audio_config(buf); break;
    case AM_AUDIO_STATUS: am_audio_status(buf); break;
    case AM_DISK_CONFIG: {
      AM_DISK_CONFIG_T *cfg = (AM_DISK_CONFIG_T *)buf;
      cfg->present = false;
      cfg->blksz = 0;
      cfg->blkcnt = 0;
      break;
    }
    case AM_DISK_STATUS: {
      AM_DISK_STATUS_T *st = (AM_DISK_STATUS_T *)buf;
      st->ready = false;
      break;
    }
    case AM_NET_CONFIG: {
      AM_NET_CONFIG_T *cfg = (AM_NET_CONFIG_T *)buf;
      cfg->present = false;
      break;
    }
    case AM_NET_STATUS: {
      AM_NET_STATUS_T *st = (AM_NET_STATUS_T *)buf;
      st->rx_len = 0;
      st->tx_len = 0;
      break;
    }
    default: break;
  }
}

void ioe_write(int reg, void *buf) {
  if (!g_inited) ioe_init();

  switch (reg) {
    case AM_UART_TX: am_uart_tx(buf); break;
    case AM_GPU_FBDRAW: am_gpu_fbdraw(buf); break;
    case AM_AUDIO_CTRL: am_audio_ctrl(buf); break;
    case AM_AUDIO_PLAY: am_audio_play(buf); break;
    case AM_DISK_BLKIO:
    case AM_NET_TX:
    case AM_NET_RX:
      break;
    default: break;
  }
}

bool cte_init(Context *(*handler)(Event ev, Context *ctx)) {
  (void)handler;
  return false;
}

void yield(void) {
  register intptr_t a7 asm("a7") = 1;
  register intptr_t a0 asm("a0") = 0;
  register intptr_t a1 asm("a1") = 0;
  register intptr_t a2 asm("a2") = 0;
  asm volatile("ecall" : "+r"(a0) : "r"(a7), "r"(a1), "r"(a2) : "memory");
}

bool ienabled(void) {
  return false;
}

void iset(bool enable) {
  (void)enable;
}

Context *kcontext(Area kstack, void (*entry)(void *), void *arg) {
  (void)kstack;
  (void)entry;
  (void)arg;
  return 0;
}

bool vme_init(void *(*pgalloc)(int), void (*pgfree)(void *)) {
  (void)pgalloc;
  (void)pgfree;
  return false;
}

void protect(AddrSpace *as) {
  (void)as;
}

void unprotect(AddrSpace *as) {
  (void)as;
}

void map(AddrSpace *as, void *vaddr, void *paddr, int prot) {
  (void)as;
  (void)vaddr;
  (void)paddr;
  (void)prot;
}

Context *ucontext(AddrSpace *as, Area kstack, void *entry) {
  (void)as;
  (void)kstack;
  (void)entry;
  return 0;
}

bool mpe_init(void (*entry)()) {
  (void)entry;
  return true;
}

int cpu_count(void) {
  return 1;
}

int cpu_current(void) {
  return 0;
}

int atomic_xchg(int *addr, int newval) {
  int old;
  __atomic_exchange(addr, &newval, &old, __ATOMIC_SEQ_CST);
  return old;
}
